#pragma once

#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <exception>
#include <memory>
#include <new>
#include <optional>
#include <thread>
#include <type_traits>
#include <utility>
#include "scorpio_utils/decorators.hpp"
#include "scorpio_utils/defer.hpp"
#include "scorpio_utils/misc.hpp"
#include "scorpio_utils/threading/signal.hpp"
#include "scorpio_utils/types.hpp"


namespace scorpio_utils::threading {
/**
 * Exception indication that channel has been closed during blocking receive or send
 */
struct ClosedChannelException : public std::exception {
  SCU_COLD SCU_CONST_FUNC SCU_ALWAYS_INLINE const char * what() const noexcept override {
    return "Channel has been closed";
  }
};

template<typename T>
struct ChannelDestructor {
  ~ChannelDestructor() {
    using U = typename T::value_type;
    for (auto& item : static_cast<T*>(this)->_buffer) {
      if (item.ready.load(std::memory_order_acquire)) {
        reinterpret_cast<U*>(&*item.data)->~U();
      }
    }
  }
};

constexpr static inline int64_t channel_default_buffer_size = 1024;

/**
 * A thread-safe channel for sending and receiving items of type T.
 * The channel has a fixed size and uses a circular buffer to store items.
 * It supports both copy and move semantics.
 *
 * \tparam T The type of items to be sent and received - it is required to be copy of move constructible.
 * \tparam Size The size of the channel buffer. Default is 1024. Size must be greater than zero.
 * \tparam Eager If true, the channel will use busy-waiting for send and receive operations.
 *
 * \warning The channel keeps items within it (Channel size grows linearly with `Size`).
 *          If `Size` is too large it can lead to stack overflow.
 *          If you need your channel keep big amount of items consider wrapping it in a heap allocated container
 *          like `std::shared_ptr`.
 * \todo Add `emplace()` method to allow in-place construction of items.
 *       This will allow to avoid unnecessary copies and moves.
 *       However, the hard pard is to return values on failure.
 *       Probably this will enforce usage of `void` as a return type and potentially losing moved item.
 */
template<typename T, int64_t Size = channel_default_buffer_size>
class Channel : public
  std::conditional_t<
    std::is_trivially_destructible_v<T>,
    Empty,
    ChannelDestructor<Channel<T, Size>>
  > {
  friend struct ChannelDestructor<Channel<T, Size>>;
  static_assert(Size > 0, "Channel size must be greater than zero");
  static_assert(std::is_copy_constructible_v<T>|| std::is_move_constructible_v<T>,
                "Channel type must be copy constructible");

public:
  // Value type of the channel.
  using value_type = T;

  // Size of the channel buffer.
  constexpr static int64_t buffer_size = Size;

private:
  struct alignas (SCU_HARDWARE_DESTRUCTIVE_INTERFERENCE_SIZE) Item {
    alignas(T) std::byte data[sizeof(T)];
    std::atomic<size_t> seq;
    std::atomic<bool> ready;
  };

  std::array<Item, static_cast<size_t>(Size)> _buffer;
  alignas(SCU_HARDWARE_DESTRUCTIVE_INTERFERENCE_SIZE) std::atomic<size_t> _head;
  alignas(SCU_HARDWARE_DESTRUCTIVE_INTERFERENCE_SIZE) std::atomic<size_t> _tail;
  Signal _send_waiter;
  Signal _receive_waiter;

  template<bool Wait>
  std::conditional_t<Wait, T, std::optional<T>> get_value() {
    const auto head = _head.fetch_add(1, std::memory_order_relaxed);
    const auto idx = head % Size;
    while (_buffer[idx].seq.load(std::memory_order_relaxed) != head) {
      std::this_thread::yield();
    }
    while (!_buffer[idx].ready.load(std::memory_order_acquire)) {
      std::this_thread::yield();
    }
    SCU_DEFER(([this, idx]() {
        if constexpr (!std::is_trivially_destructible_v<T>) {
          reinterpret_cast<T*>(&_buffer[idx].data)->~T();
        }
        _buffer[idx].ready.store(false, std::memory_order_relaxed);
        _buffer[idx].seq.fetch_add(Size, std::memory_order_relaxed);
        _send_waiter.notify_one();
      }));
    auto ret_val = reinterpret_cast<T*>(&_buffer[idx].data);
    if constexpr (Wait) {
      if constexpr (std::is_move_constructible_v<T>) {
        return std::move(*ret_val);
      } else {
        return *ret_val;
      }
    } else {
      if constexpr (std::is_move_constructible_v<T>) {
        return std::optional<T>(std::move(*ret_val));
      } else {
        return std::optional<T>(*ret_val);
      }
    }
  }

  template<typename, int64_t>
  friend class ChannelReader;

public:
  /**
   * Checks if the channel is ready to receive an item and reserves it.
   * \return true if the channel has an item and it has been reserved, false otherwise.
   * \throws ClosedChannelException if the channel has been closed.
   * \warning This function reserves an item in the channel.
   *          The reserved item must be retrieved using `SCU_EAGER_SELECT_GET_VALUE()
   *          or some undefined may occur. Function is intended to be used only within eager_select().
   */
  SCU_ALWAYS_INLINE auto SCU_EAGER_SELECT_IS_READY() {
    try {
      return _receive_waiter.try_take();
    } catch (const SignalException&) {
      throw ClosedChannelException();
    }
  }

  /**
   * Retrieves the reserved item from the channel.
   * \return The reserved item.
   * \warning This function must be called only after a successful call to `SCU_EAGER_SELECT_IS_READY()`.
   *          Otherwise, undefined behavior may occur.
   *          Function is intended to be used only within eager_select().
   */
  SCU_ALWAYS_INLINE decltype(auto) SCU_EAGER_SELECT_GET_VALUE() noexcept {
    return get_value<true>();
  }

  Channel()
  : _head{0}, _tail{0}, _send_waiter(Size), _receive_waiter(0) {
    size_t i = 0;
    for (auto& item : _buffer) {
      item.seq.store(i++, std::memory_order_relaxed);
      item.ready.store(false, std::memory_order_relaxed);
    }
  }

  SCU_UNCOPYBLE(Channel);
  SCU_UNMOVABLE(Channel);

  /**
   * Send an item to the channel.
   *
   * \tparam Force If true send will block until the item is sent, even if the channel is full.
   *               If false, the function will return false if the channel is full.
   * \param item The item to send.
   * \return void if Force is true, or bool indicating failure if Force is false.
   *         `false` if the send was successful, or `true` if the channel is full.
   * \note If Force is true, the function will block until an item is send.
   *       While blocked thread is not waiting but it is actively trying to send an item.
   *       It can generate high CPU usage if the channel is full for a long time.
   *       If this is a case consider increasing the channel size or using channel with unlimited size (`Channel<T, 0>`).
   */
  template<bool Force = false, typename U = T>
  std::enable_if_t<std::is_copy_constructible_v<U>, std::conditional_t<Force, void, bool>> send(const T& item) {
    try {
      if constexpr (Force) {
        _send_waiter.wait();
      } else if (!_send_waiter.try_take()) {
        return true;
      }
    } catch (const SignalException&) {
      throw ClosedChannelException();
    }
    const auto tail = _tail.fetch_add(1, std::memory_order_relaxed);
    const auto idx = tail % Size;
    while (_buffer[idx].seq.load(std::memory_order_relaxed) != tail) {
      std::this_thread::yield();
    }
    new (&_buffer[idx].data) T(item);
    _buffer[idx].ready.store(true, std::memory_order_release);
    _receive_waiter.notify_one();
    if constexpr (!Force) {
      return false;
    }
  }

  /**
   * Send an item to the channel.
   *
   * \tparam Force If true send will block until the item is sent, even if the channel is full.
   *               If false, the function will return false if the channel is full.
   * \param item The item to send.
   * \return void if Force is true, or bool indicating failure if Force is false.
   *         `false` if the send was successful, or `true` if the channel is full.
   * \note If Force is true, the function will block until an item is send.
   *       While blocked thread is not waiting but it is actively trying to send an item.
   *       It can generate high CPU usage if the channel is full for a long time.
   *       If this is a case consider increasing the channel size or using channel with unlimited size (`Channel<T, 0>`).
   */
  template<bool Force = false, typename U = T>
  std::enable_if_t<std::is_move_constructible_v<U>, std::conditional_t<Force, void, std::optional<T>>> send(T&& item) {
    try {
      if constexpr (Force) {
        _send_waiter.wait();
      } else if (!_send_waiter.try_take()) {
        return std::optional<T>(std::forward<T>(item));
      }
    } catch (const SignalException&) {
      throw ClosedChannelException();
    }
    const auto tail = _tail.fetch_add(1, std::memory_order_relaxed);
    const auto idx = tail % Size;
    while (_buffer[idx].seq.load(std::memory_order_relaxed) != tail) {
      std::this_thread::yield();
    }
    new(&_buffer[idx].data) T(std::forward<T>(item));
    _buffer[idx].ready.store(true, std::memory_order_release);
    _receive_waiter.notify_one();
    if constexpr (!Force) {
      return std::nullopt;
    }
  }

  /**
   * Receive an item from the channel.
   * Returns an optional containing the item if the receive was successful,
   * or an empty optional if the channel is empty.
   *
   * \tparam Wait If true, the function will block until an item is received.
   * \return If Wait is true, T is returned, otherwise an optional containing the item if the receive was successful,
   *         or an empty optional if the channel is empty.
   * \note If Force is true, the function will block until an item is received.
   *       While blocked thread is not waiting but it is actively trying to receive an item.
   *       It can generate high CPU usage if the channel is full for a long time.
   *       If this is a case consider increasing the channel size or using channel with unlimited size (`Channel<T, 0>`).
   */
  template<bool Wait = false>
  std::conditional_t<Wait, T, std::optional<T>> receive() {
    try {
      if constexpr (Wait) {
        _receive_waiter.wait();
      } else if (!_receive_waiter.try_take()) {
        return std::nullopt;
      }
    } catch (const SignalException&) {
      throw ClosedChannelException();
    }
    return get_value<Wait>();
  }

  /**
   * Close the channel.
   * This will unblock any threads waiting on send or receive operations by throwing a `ClosedChannelException`.
   * After the channel is closed, any further send or receive operations will throw a `ClosedChannelException`.
   *
   * \note This operation is idempotent and can be called multiple times safely. However, once closed,
   *       the channel cannot be reopened.
   */
  SCU_ALWAYS_INLINE void close() noexcept {
    _send_waiter.close();
    _receive_waiter.close();
  }

  /**
   * Check if the channel is empty - no message to be received.
   *
   * \return true if the channel is empty, false otherwise.
   */
  constexpr SCU_ALWAYS_INLINE bool is_empty() const {
    return _receive_waiter.count() <= 0;
  }

  /**
   * Check if the channel is full - no message can be send.
   *
   * \return true if the channel is full, false otherwise.
   */
  constexpr SCU_ALWAYS_INLINE bool is_full() const {
    return _send_waiter.count() <= 0;
  }

  /**
   * Get the number of items available to be received.
   *
   * \return The number of items available to be received.
   */
  constexpr SCU_ALWAYS_INLINE auto available() const {
    return _receive_waiter.count();
  }

  /**
   * Get the size of the channel buffer.
   *
   * \return The size of the channel buffer.
   */
  constexpr static SCU_ALWAYS_INLINE auto size() noexcept {
    return Size;
  }

  /**
   * Check if the channel is ready for writing.
   * This means that there is space available in the channel buffer to write more items.
   *
   * \return true if the channel is ready for writing, false otherwise.
   */
  constexpr SCU_ALWAYS_INLINE bool is_write_ready() const {
    return _send_waiter.count() > 0;
  }

  /**
   * Get the number of free spaces in the channel buffer.
   * This is the number of items that can be sent to the channel without blocking.
   *
   * \return The number of free spaces in the channel buffer.
   */
  constexpr SCU_ALWAYS_INLINE auto free_space() const {
    return _send_waiter.count();
  }
};

template<typename T, int64_t Size>
class ChannelSideBase {
protected:
  std::shared_ptr<Channel<T, Size>> _channel;

public:
  ChannelSideBase(std::shared_ptr<Channel<T, Size>> channel)  // NOLINT
  : _channel(std::move(channel)) { }
  /**
   * Close the channel.
   * This will unblock any threads waiting on send or receive operations by throwing a `ClosedChannelException`.
   * After the channel is closed, any further send or receive operations will throw a `ClosedChannelException`.
   *
   * \note This operation is idempotent and can be called multiple times safely. However, once closed,
   *       the channel cannot be reopened.
   */
  SCU_ALWAYS_INLINE decltype(auto) close() noexcept {
    return _channel->close();
  }

  /**
   * Check if the channel is empty - no message to be received.
   *
   * \return true if the channel is empty, false otherwise.
   */
  constexpr SCU_ALWAYS_INLINE decltype(auto) is_empty() const {
    return _channel->is_empty();
  }

  /**
   * Check if the channel is full - no message can be send.
   *
   * \return true if the channel is full, false otherwise.
   */
  constexpr SCU_ALWAYS_INLINE decltype(auto) is_full() const {
    return _channel->is_full();
  }

  /**
   * Get the number of items available to be received.
   *
   * \return The number of items available to be received.
   */
  constexpr SCU_ALWAYS_INLINE decltype(auto) available() const {
    return _channel->available();
  }

  /**
   * Get the size of the channel buffer.
   *
   * \return The size of the channel buffer.
   */
  constexpr static SCU_ALWAYS_INLINE auto size() noexcept {
    return Size;
  }

  /**
   * Check if the channel is ready for writing.
   * This means that there is space available in the channel buffer to write more items.
   *
   * \return true if the channel is ready for writing, false otherwise.
   */
  constexpr SCU_ALWAYS_INLINE decltype(auto) is_write_ready() const {
    return _channel->is_write_ready();
  }

  /**
   * Get the number of free spaces in the channel buffer.
   * This is the number of items that can be sent to the channel without blocking.
   *
   * \return The number of free spaces in the channel buffer.
   */
  constexpr SCU_ALWAYS_INLINE decltype(auto) free_space() const {
    return _channel->free_space();
  }
};

template<typename T, int64_t Size>
class ChannelWriter : public ChannelSideBase<T, Size> {
public:
  ChannelWriter(std::shared_ptr<Channel<T, Size>> channel)  // NOLINT
  : ChannelSideBase<T, Size>(std::move(channel)) { }

  /**
   * Send an item to the channel.
   *
   * \tparam Force If true send will block until the item is sent, even if the channel is full.
   *               If false, the function will return false if the channel is full.
   * \param item The item to send.
   * \return void if Force is true, or bool indicating failure if Force is false.
   *         `false` if the send was successful, or `true` if the channel is full.
   * \note If Force is true, the function will block until an item is send.
   *       While blocked thread is not waiting but it is actively trying to send an item.
   *       It can generate high CPU usage if the channel is full for a long time.
   *       If this is a case consider increasing the channel size or using channel with unlimited size (`Channel<T, 0>`).
   */
  template<bool Force = false, typename U = T>
  std::enable_if_t<std::is_copy_constructible_v<U>, std::conditional_t<Force, void, bool>> send(const T& item) {
    return this->_channel->template send<Force>(item);
  }

  /**
   * Send an item to the channel.
   *
   * \tparam Force If true send will block until the item is sent, even if the channel is full.
   *               If false, the function will return false if the channel is full.
   * \param item The item to send.
   * \return void if Force is true, or bool indicating failure if Force is false.
   *         `false` if the send was successful, or `true` if the channel is full.
   * \note If Force is true, the function will block until an item is send.
   *       While blocked thread is not waiting but it is actively trying to send an item.
   *       It can generate high CPU usage if the channel is full for a long time.
   *       If this is a case consider increasing the channel size or using channel with unlimited size (`Channel<T, 0>`).
   */
  template<bool Force = false, typename U = T>
  std::enable_if_t<std::is_move_constructible_v<U>, std::conditional_t<Force, void, std::optional<T>>> send(T&& item) {
    return this->_channel->template send<Force>(std::forward<T>(item));
  }
};

template<typename T, int64_t Size>
class ChannelReader : public ChannelSideBase<T, Size> {
public:
  SCU_ALWAYS_INLINE auto SCU_EAGER_SELECT_IS_READY() {
    return this->_channel->SCU_EAGER_SELECT_IS_READY();
  }

  SCU_ALWAYS_INLINE decltype(auto) SCU_EAGER_SELECT_GET_VALUE() noexcept {
    return this->_channel->SCU_EAGER_SELECT_GET_VALUE();
  }

  ChannelReader(std::shared_ptr<Channel<T, Size>> channel)  // NOLINT
  : ChannelSideBase<T, Size>(std::move(channel)) { }

  /**
   * Receive an item from the channel.
   * Returns an optional containing the item if the receive was successful,
   * or an empty optional if the channel is empty.
   *
   * \tparam Wait If true, the function will block until an item is received.
   * \return If Wait is true, T is returned, otherwise an optional containing the item if the receive was successful,
   *         or an empty optional if the channel is empty.
   * \note If Force is true, the function will block until an item is received.
   *       While blocked thread is not waiting but it is actively trying to receive an item.
   *       It can generate high CPU usage if the channel is full for a long time.
   *       If this is a case consider increasing the channel size or using channel with unlimited size (`Channel<T, 0>`).
   */
  template<bool Wait = false>
  decltype(auto) receive() {
    return this->_channel->template receive<Wait>();
  }
};

template<typename T, int64_t Size>
SCU_ALWAYS_INLINE SCU_CONST_FUNC std::pair<ChannelWriter<T, Size>, ChannelReader<T, Size>> split_channel(
  std::shared_ptr<Channel<T, Size>> channel) {
  return { ChannelWriter<T, Size>(channel), ChannelReader<T, Size>(channel) };
}
}  // namespace scorpio_utils::threading
