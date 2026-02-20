#pragma once

#include <array>
#include <atomic>
#include <cstdint>
#include <string>
#include <tuple>

#include "scorpio_utils/expected.hpp"
#include "scorpio_utils/network/ip.hpp"
#include "scorpio_utils/types.hpp"
#if defined(SCORPIO_UTILS_UDP_GMOCK) && SCORPIO_UTILS_UDP_GMOCK == 1
#include "scorpio_utils/testing/gmock.hpp"
#elif defined(SCORPIO_UTILS_FRAMEWORK) && SCORPIO_UTILS_FRAMEWORK == 1
#include <cstring>
#include <utility>
#include <vector>
#include "scorpio_utils/assert.hpp"
#include "scorpio_utils/threading/channel.hpp"
using std::literals::string_literals::operator""s;
using scorpio_utils::threading::Channel;
#endif

namespace scorpio_utils::network {
struct UdpMessageInfo {
  size_t byte_count;
  Ipv4 remote_ip;
  Port remote_port;
};

class UdpSocket {
  UdpSocket(const UdpSocket&) = delete;
  UdpSocket& operator=(const UdpSocket&) = delete;

#if defined(SCORPIO_UTILS_UDP_GMOCK) && SCORPIO_UTILS_UDP_GMOCK == 1

public:
  SCU_ALWAYS_INLINE UdpSocket() { }
  explicit UdpSocket(bool) { }
  UdpSocket(Ipv4, Port) { }

  MOCK_METHOD(bool, is_open, (), (const noexcept));
  MOCK_METHOD((Expected<Success, std::string>), open, (), ());
  MOCK_METHOD(bool, close, (), (noexcept));
  MOCK_METHOD((Expected<Success, std::string>), bind, (
      Ipv4 local_ip,
      Port local_port), ());
  MOCK_METHOD((Expected<size_t, std::string>), send,
    (const uint8_t * data, size_t size, Ipv4 remote_ip, Port remote_port),
    (const));
  MOCK_METHOD((Expected<UdpMessageInfo, std::string>), receive, (uint8_t * data, size_t size), (const));
  MOCK_METHOD(bool, is_bound, (), (const noexcept));
  virtual ~UdpSocket() = default;
#elif defined(SCORPIO_UTILS_FRAMEWORK) && SCORPIO_UTILS_FRAMEWORK == 1
  mutable Channel<std::tuple<Ipv4, Port, std::vector<uint8_t>>, 1024 * 1024> _send_queue;
  mutable Channel<std::tuple<Expected<UdpMessageInfo, std::string>, std::vector<uint8_t>>, 1024 * 1024> _receive_queue;
  Channel<Expected<Success, std::string>, 1024 * 1024> _next_open_result;
  mutable Channel<Expected<size_t, std::string>, 1024 * 1024> _next_send_result;
  Channel<Expected<Success, std::string>, 1024 * 1024> _next_bind_result;
  std::atomic<uint32_t> _bound_ip;
  std::atomic<uint16_t> _bound_port;
  std::atomic<bool> _is_open;
  std::atomic<bool> _is_bound;

public:
  SCU_ALWAYS_INLINE UdpSocket()
  : _is_open(false), _is_bound(false) { }
  explicit UdpSocket(bool open)
  : _is_open(open), _is_bound(false) { }
  virtual ~UdpSocket() = default;

  SCU_ALWAYS_INLINE auto is_open() const noexcept {
    return _is_open.load(std::memory_order_relaxed);
  }
  SCU_ALWAYS_INLINE auto is_bound() const noexcept {
    return _is_bound.load(std::memory_order_relaxed);
  }
  Expected<Success, std::string> open() {
    if (auto result = _next_open_result.receive()) {
      if (result->is_ok()) {
        _is_open.store(true, std::memory_order_relaxed);
      }
      return std::move(*result);
    }
    if (is_open()) {
      return "Already opened"s;
    }
    _is_open.store(true, std::memory_order_relaxed);
    return Success();
  }

  bool close() noexcept {
    return _is_open.exchange(false);
  }

  Expected<Success, std::string> bind(
    Ipv4 local_ip,
    Port local_port) {
    if (SCU_UNLIKELY(!is_open())) {
      return Unexpected("Socket is not valid"s);
    }
    if (is_bound()) {
      return Unexpected("Socket is already bound"s);
    }
    if (auto result = _next_bind_result.receive()) {
      if (result->is_ok()) {
        _bound_ip.store(local_ip.ip(), std::memory_order_relaxed);
        _bound_port.store(local_port, std::memory_order_relaxed);
        _is_bound.store(true, std::memory_order_relaxed);
      }
      return std::move(*result);
    }
    _bound_ip.store(local_ip.ip(), std::memory_order_relaxed);
    _bound_port.store(local_port, std::memory_order_relaxed);
    _is_bound.store(true, std::memory_order_relaxed);
    return Success();
  }

  Expected<size_t, std::string> send(
    const uint8_t* data,
    size_t size,
    Ipv4 remote_ip,
    Port remote_port) const {
    if (SCU_UNLIKELY(!is_open())) {
      return Unexpected("Socket is not valid"s);
    }
    _send_queue.send<true>({ remote_ip, remote_port, std::vector<uint8_t>(data, data + size) });
    if (auto result = _next_send_result.receive()) {
      return *result;
    }
    return size;
  }

  Expected<UdpMessageInfo, std::string> receive(uint8_t* data, size_t size) const {
    if (SCU_UNLIKELY(!is_open())) {
      return Unexpected("Socket is closed"s);
    }
    auto [return_value, data_to_send] = _receive_queue.receive<true>();
    SCU_ASSERT(size >= data_to_send.size(),
        "Artificial packet is larger than receiver buffer: " << size << " < " << data_to_send.size());
    std::memcpy(data, data_to_send.data(), data_to_send.size());
    return return_value;
  }

  template<bool Wait = false>
  auto add_to_receive_queue(Expected<UdpMessageInfo, std::string> return_value, std::vector<uint8_t> data) {
    return _receive_queue.send<Wait>({ std::move(return_value), std::move(data) });
  }

  template<bool Wait = false>
  auto add_next_open_result(Expected<Success, std::string> return_value) {
    return _next_open_result.send<Wait>(std::move(return_value));
  }

  template<bool Wait = false>
  auto add_next(Expected<size_t, std::string> value) {
    return _next_send_result.send<Wait>(std::move(value));
  }

  template<bool Wait = false>
  auto add_next_bind_result(Expected<Success, std::string> return_value) {
    return _next_bind_result.send<Wait>(std::move(return_value));
  }

  template<bool Wait = false>
  auto get_from_send_queue() {
    return _send_queue.receive<Wait>();
  }

  void close_channels() noexcept {
    _send_queue.close();
    _receive_queue.close();
    _next_open_result.close();
    _next_send_result.close();
  }

  auto get_send_queue_available() const noexcept {
    return _send_queue.available();
  }

  auto get_bound_ip() const noexcept {
    return _bound_ip.load(std::memory_order_relaxed);
  }

  auto get_bound_port() const noexcept {
    return _bound_port.load(std::memory_order_relaxed);
  }

#else
  std::atomic<int> _socket_fd;
  std::atomic<bool> _is_bound;

public:
  SCU_ALWAYS_INLINE UdpSocket()
  : _socket_fd(-1), _is_bound(false) { }

  explicit UdpSocket(bool open);

  UdpSocket(Ipv4 local_ip, Port local_port);

  UdpSocket(UdpSocket&&);
  UdpSocket& operator=(UdpSocket&&);
  SCU_ALWAYS_INLINE  ~UdpSocket() {
    std::ignore = close();
  }

  SCU_ALWAYS_INLINE auto is_open() const noexcept {
    return _socket_fd.load(std::memory_order_relaxed) >= 0;
  }

  Expected<Success, std::string> open();

  bool close() noexcept;

  Expected<Success, std::string> bind(
    Ipv4 local_ip,
    Port local_port);

  Expected<size_t, std::string> send(
    const uint8_t* data,
    size_t size,
    Ipv4 remote_ip,
    Port remote_port) const;

  Expected<UdpMessageInfo, std::string> receive(uint8_t* data, size_t size) const;

  SCU_ALWAYS_INLINE auto is_bound() const noexcept {
    return _is_bound.load(std::memory_order_relaxed);
  }
#endif
};
}  // namespace scorpio_utils::network
