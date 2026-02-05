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
#if !defined(SCORPIO_UTILS_UDP_GMOCK) || SCORPIO_UTILS_UDP_GMOCK != 1
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
    uint8_t* data,
    size_t size,
    Ipv4 remote_ip,
    Port remote_port) const;

  Expected<UdpMessageInfo, std::string> receive(uint8_t* data, size_t size) const;

  SCU_ALWAYS_INLINE auto is_bound() const noexcept {
    return _is_bound.load(std::memory_order_relaxed);
  }
#else

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
    (uint8_t * data, size_t size, Ipv4 remote_ip, Port remote_port),
    (const));
  MOCK_METHOD((Expected<UdpMessageInfo, std::string>), receive, (uint8_t * data, size_t size), (const));
  MOCK_METHOD(bool, is_bound, (), (const noexcept));
  virtual ~UdpSocket() = default;
#endif
};
}  // namespace scorpio_utils::network
