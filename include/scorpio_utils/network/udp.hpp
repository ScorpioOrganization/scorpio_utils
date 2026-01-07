#pragma once

#include <array>
#include <atomic>
#include <cstdint>
#include <string>
#include <tuple>

#include "scorpio_utils/expected.hpp"
#include "scorpio_utils/network/ip.hpp"
#include "scorpio_utils/types.hpp"

namespace scorpio_utils::network {
struct UdpMessageInfo {
  size_t byte_count;
  Ipv4 remote_ip;
  Port remote_port;
};

class UdpSocket {
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

  UdpSocket(const UdpSocket&) = delete;
  UdpSocket& operator=(const UdpSocket&) = delete;

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
};
}  // namespace scorpio_utils::network
