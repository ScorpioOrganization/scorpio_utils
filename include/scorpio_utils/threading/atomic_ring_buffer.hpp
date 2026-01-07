#pragma once

#include <gtest/gtest_prod.h>

#include <atomic>
#include <cstddef>
#include <memory>
#include <optional>
#include <type_traits>
#include <utility>

#include "scorpio_utils/assert.hpp"
#include "scorpio_utils/decorators.hpp"
#include "scorpio_utils/misc.hpp"

namespace scorpio_utils::threading {
/**
 * A lock-free atomic ring buffer.
 *
 * \tparam T The type of elements stored in the ring buffer.
 * \note The size of the ring buffer does not need to be a power of two, but
 *       using a power of two size can improve performance by avoiding the
 *       modulo operation.
 * \warning This implementation is designed for single-producer, single-consumer scenarios.
 */
template<typename T>
class AtomicRingBuffer {
public:
  SCU_ALWAYS_INLINE explicit AtomicRingBuffer(size_t size)
  : _size(size),
    _mask(is_power_of_two(_size) ? _size - 1 : 0),
    _power_of_two(is_power_of_two(_size)),
    _head(0),
    _tail(0) {
    SCU_ASSERT((_size > 0), "Size must be greater than 0");
    _raw_buffer = std::make_unique<StorageType[]>(_size);
  }

  ~AtomicRingBuffer() {
    const auto tail = _tail.load(std::memory_order_relaxed);
    const auto head = _head.load(std::memory_order_relaxed);
    for (size_t i = tail; i < head; ++i) {
      destroy_at(i);
    }
  }

  /**
   * Pushes an element to the ring buffer. If the buffer is full, the oldest element is overwritten.
   *
   * \param value The element to push.
   * \return true if the element was added without overwriting, false if an overwrite occurred.
   */
  bool push(const T& value) noexcept(std::is_nothrow_copy_constructible_v<T>) {
    const auto head = _head.load(std::memory_order_relaxed);
    const auto tail = _tail.load(std::memory_order_acquire);
    const auto next = head + 1;
    bool ret = false;
    if ((head - tail) == _size) {
      destroy_at(tail);
      _tail.fetch_add(1, std::memory_order_release);
    } else {
      ret = true;
    }
    new (get_ptr(head)) T(value);
    _head.store(next, std::memory_order_release);
    return ret;
  }

  /**
   * Pushes an element to the ring buffer. If the buffer is full, the oldest element is overwritten.
   *
   * \param value The element to push.
   * \return true if the element was added without overwriting, false if an overwrite occurred.
   */
  bool push(T&& value) noexcept {
    const auto head = _head.load(std::memory_order_relaxed);
    const auto tail = _tail.load(std::memory_order_acquire);
    const auto next = head + 1;
    bool ret = false;
    if ((head - tail) == _size) {
      destroy_at(tail);
      _tail.fetch_add(1, std::memory_order_release);
    } else {
      ret = true;
    }
    new (get_ptr(head)) T(std::move(value));
    _head.store(next, std::memory_order_release);
    return ret;
  }

  /**
   * Pops an element from the ring buffer. If the buffer is empty, returns std::nullopt.
   *
   * \return An optional containing the popped element, or std::nullopt if the buffer is empty.
   */
  std::optional<T> pop() noexcept {
    const auto tail = _tail.load(std::memory_order_relaxed);
    const auto head = _head.load(std::memory_order_acquire);
    if (tail == head) { return std::nullopt; }
    T* element_ptr = get_ptr(tail);
    auto value = std::move(*element_ptr);
    destroy_at(tail);
    _tail.store(tail + 1, std::memory_order_release);
    return value;
  }

  /**
   * Checks if the ring buffer is empty.
   *
   * \return true if the buffer is empty, false otherwise.
   */
  bool empty() const noexcept {
    return _tail.load(std::memory_order_acquire) ==
           _head.load(std::memory_order_acquire);
  }

  /**
   * Checks if the ring buffer is full.
   *
   * \return true if the buffer is full, false otherwise.
   */
  bool full() const noexcept {
    return (_head.load(std::memory_order_acquire) -
           _tail.load(std::memory_order_acquire)) >= _size;
  }

  /**
   * Returns the declared size of the ring buffer.
   *
   * \return The declared size of the ring buffer.
   */
  size_t size() const noexcept {
    return _size;
  }

private:
  const size_t _size;
  const size_t _mask;
  const bool _power_of_two;

  alignas(SCU_HARDWARE_DESTRUCTIVE_INTERFERENCE_SIZE) std::atomic<size_t> _head;
  alignas(SCU_HARDWARE_DESTRUCTIVE_INTERFERENCE_SIZE) std::atomic<size_t> _tail;

  using StorageType = std::aligned_storage_t<sizeof(T), alignof(T)>;
  std::unique_ptr<StorageType[]> _raw_buffer;

  SCU_ALWAYS_INLINE size_t index(size_t i) const noexcept {
    return _power_of_two ? (i & _mask) : (i % _size);
  }

  SCU_ALWAYS_INLINE T * get_ptr(size_t i) const noexcept {
    return std::launder(reinterpret_cast<T*>(&_raw_buffer[index(i)]));
  }

  SCU_ALWAYS_INLINE void destroy_at(size_t i) noexcept {
    T* ptr = get_ptr(i);
    if constexpr (!std::is_trivially_destructible_v<T>) {
      ptr->~T();
    }
  }

  static constexpr bool is_power_of_two(size_t x) noexcept {
    return x > 0 && (x & (x - 1)) == 0;
  }
};
}  // namespace scorpio_utils::threading
