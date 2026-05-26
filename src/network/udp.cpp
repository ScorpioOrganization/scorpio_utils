#include "scorpio_utils/network/udp.hpp"

extern "C" {
  #include <netinet/in.h>
  #include <sys/socket.h>
  #include <unistd.h>
};

#include <cstring>
#include <string>
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
  if (!_socket_fd.compare_exchange_strong(expected, -1, std::memory_order_relaxed, std::memory_order_relaxed)) {
    return false;
  }
  std::ignore = ::shutdown(expected, SHUT_RDWR);
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
  auto count = ::sendto(fd, data, size, 0, reinterpret_cast<struct sockaddr*>(&addr), sizeof(addr));
  if (SCU_UNLIKELY(count < 0)) {
    return Unexpected("Failed to send data: "s + std::strerror(errno));
  }
  return SCU_AS(size_t, count);
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
    return Unexpected("Failed to receive data: "s + std::strerror(errno));
  }
  UdpMessageInfo msg_info;
  msg_info.remote_ip = Ipv4::from_network(addr.sin_addr.s_addr);
  msg_info.remote_port = ntohs(addr.sin_port);
  msg_info.byte_count = SCU_AS(size_t, count);
  return msg_info;
}
