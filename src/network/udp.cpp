#include "scorpio_utils/network/udp.hpp"

extern "C" {
  #include <netinet/in.h>
  #include <sys/socket.h>
  #include <unistd.h>
};

#include <cerrno>
#include <chrono>
#include <cstring>
#include <string>
#include <thread>
#include <utility>

#include "scorpio_utils/decorators.hpp"

using std::literals::string_literals::operator""s;
using scorpio_utils::Expected;
using scorpio_utils::Success;
using scorpio_utils::network::UdpMessageInfo;
using scorpio_utils::network::UdpSocket;

UdpSocket::UdpSocket(bool open)
: UdpSocket() {
  if (open) {
    std::ignore = this->open();
  }
}

UdpSocket::UdpSocket(Ipv4 local_ip, Port local_port)
: UdpSocket() {
  if (open().is_ok()) {
    std::ignore = bind(local_ip, local_port);
  }
}

Expected<Success, std::string> UdpSocket::open() {
  std::ignore = close();
  _socket_fd.store(::socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP), std::memory_order_relaxed);
  if (SCU_UNLIKELY(!is_open())) {
    return Unexpected("Failed to create UDP socket: "s + std::strerror(errno));
  }
  return Success::instance();
}

bool UdpSocket::close() noexcept {
  int expected = _socket_fd.load(std::memory_order_relaxed);
  if (expected < 0 ||
    !_socket_fd.compare_exchange_strong(expected, -1, std::memory_order_relaxed, std::memory_order_relaxed)) {
    return false;
  }
  std::ignore = ::shutdown(expected, SHUT_RDWR);
  // The descriptor itself must be released too, or every open/close cycle
  // (e.g. an application recreating the stack after a panic) leaks an fd until
  // the process runs out of them.
  std::ignore = ::close(expected);
  _is_bound.store(false, std::memory_order_relaxed);
  return true;
}

Expected<Success, std::string> UdpSocket::bind(
  Ipv4 local_ip,
  Port local_port) {
  const auto fd = _socket_fd.load(std::memory_order_relaxed);
  if (SCU_UNLIKELY(fd < 0)) {
    return Unexpected("Socket is not valid"s);
  }
  if (_is_bound.load(std::memory_order_relaxed)) {
    return Unexpected("Socket is already bound"s);
  }
  struct sockaddr_in addr;
  std::memset(&addr, 0, sizeof(addr));
  addr.sin_family = AF_INET;
  addr.sin_addr.s_addr = local_ip.ip_network();
  addr.sin_port = htons(local_port);
  if (::bind(fd, reinterpret_cast<struct sockaddr*>(&addr), sizeof(addr))) {
    return Unexpected("Failed to bind UDP socket: "s + std::strerror(errno));
  }
  _is_bound.store(true, std::memory_order_relaxed);
  return Success::instance();
}

UdpSocket::UdpSocket(UdpSocket&& other)
: _socket_fd(other._socket_fd.exchange(-1, std::memory_order_relaxed)),
  _is_bound(other._is_bound.exchange(false, std::memory_order_relaxed)) {
}

UdpSocket& UdpSocket::operator=(UdpSocket&& other) {
  if (this != &other) {
    std::ignore = close();
    _socket_fd.store(other._socket_fd.exchange(-1, std::memory_order_relaxed));
    _is_bound.store(other._is_bound.exchange(false, std::memory_order_relaxed));
  }
  return *this;
}

SCU_HOT Expected<size_t, std::string> UdpSocket::send(
  const uint8_t* data,
  size_t size,
  Ipv4 remote_ip,
  Port remote_port) const {
  const auto fd = _socket_fd.load(std::memory_order_relaxed);
  if (SCU_UNLIKELY(fd < 0)) {
    return Unexpected("Socket is not valid"s);
  }
  struct sockaddr_in addr;
  std::memset(&addr, 0, sizeof(addr));
  addr.sin_family = AF_INET;
  addr.sin_addr.s_addr = remote_ip.ip_network();
  addr.sin_port = htons(remote_port);
  for (int attempt = 0; ; ++attempt) {
    const auto count = ::sendto(fd, data, size, 0, reinterpret_cast<struct sockaddr*>(&addr), sizeof(addr));
    if (SCU_LIKELY(count >= 0)) {
      return SCU_AS(size_t, count);
    }
    switch (errno) {
      case EINTR:
        continue;
      case EAGAIN:
      case ENOBUFS:
        // Local buffers momentarily full - give the kernel a moment, then treat
        // the packet as lost if it still does not fit.
        if (attempt < 3) {
          std::this_thread::sleep_for(std::chrono::microseconds(100));
          continue;
        }
        [[fallthrough]];
      case ENETUNREACH:
      case EHOSTUNREACH:
      case ENETDOWN:
      case EPERM:
      case ECONNREFUSED:
        // Transient network conditions (interface flap, firewall hiccup, ICMP
        // bounce). Report the datagram as sent: for UDP a dropped packet is
        // normal loss the reliable layer recovers from, while surfacing an error
        // here would panic and kill the whole stack.
        return size;
      default:
        return Unexpected("Failed to send data: "s + std::strerror(errno));
    }
  }
}

SCU_HOT Expected<UdpMessageInfo, std::string> UdpSocket::receive(
  uint8_t* data,
  size_t size) const {
  const auto fd = _socket_fd.load(std::memory_order_relaxed);
  if (SCU_UNLIKELY(fd < 0)) {
    return Unexpected("Socket is closed"s);
  }
  struct sockaddr_in addr;
  socklen_t addr_len = sizeof(addr);
  auto count = ::recvfrom(fd, data, size, 0, reinterpret_cast<struct sockaddr*>(&addr), &addr_len);
  if (SCU_UNLIKELY(count < 0)) {
    switch (errno) {
      case EINTR:
      case EAGAIN:
      case ECONNREFUSED: {
          // Transient (signal, timeout, ICMP bounce from an earlier send): report
          // an empty datagram so the receiver loop just carries on, instead of
          // panicking the whole stack over a recoverable condition.
          UdpMessageInfo empty_info;
          empty_info.byte_count = 0;
          empty_info.remote_ip = Ipv4();
          empty_info.remote_port = 0;
          return empty_info;
        }
      default:
        return Unexpected("Failed to receive data: "s + std::strerror(errno));
    }
  }
  UdpMessageInfo msg_info;
  msg_info.remote_ip = Ipv4::from_network(addr.sin_addr.s_addr);
  msg_info.remote_port = ntohs(addr.sin_port);
  msg_info.byte_count = SCU_AS(size_t, count);
  return msg_info;
}
