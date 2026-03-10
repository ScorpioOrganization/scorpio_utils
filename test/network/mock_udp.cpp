#include "scorpio_utils/network/udp.hpp"  // NOLINT(build/include_order)

#include <chrono>                         // NOLINT(build/include_order)
#include <cstring>                        // NOLINT(build/include_order)
#include <memory>                         // NOLINT(build/include_order)
#include <mutex>                          // NOLINT(build/include_order)
#include <random>                         // NOLINT(build/include_order)
#include <set>                            // NOLINT(build/include_order)
#include <string>                         // NOLINT(build/include_order)
#include <thread>                         // NOLINT(build/include_order)
#include <unordered_map>                  // NOLINT(build/include_order)
#include <unordered_set>                  // NOLINT(build/include_order)
#include <utility>                        // NOLINT(build/include_order)

#include "scorpio_utils/assert.hpp"
#include "scorpio_utils/compare.hpp"
#include "scorpio_utils/decorators.hpp"

using std::literals::string_literals::operator""s;
using scorpio_utils::Expected;
using scorpio_utils::network::Ipv4;
using scorpio_utils::network::Port;
using scorpio_utils::Success;
using scorpio_utils::network::UdpMessageInfo;
using scorpio_utils::network::UdpSocket;

#ifndef SCU_MOCK_UDP_DELAY_MIN_MS
  #define SCU_MOCK_UDP_DELAY_MIN_MS (10)
#endif

#ifndef SCU_MOCK_UDP_DELAY_MAX_MS
  #define SCU_MOCK_UDP_DELAY_MAX_MS (100)
#endif

#ifndef SCU_MOCK_UDP_PACKET_LOSS_PERCENTAGE
  #define SCU_MOCK_UDP_PACKET_LOSS_PERCENTAGE (0)
#endif

struct Packet {
  size_t arrival_time;
  std::vector<uint8_t> data;
  Ipv4 remote_ip;
  Port remote_port;

  bool operator<(const Packet& other) const noexcept {
    return arrival_time < other.arrival_time;
  }
  SCU_EQ_FROM_LT(Packet)
};

struct SocketState {
  Ipv4 local_ip;
  Port local_port;
};

static std::mutex mock_udp_sockets_mutex;
static std::unordered_map<std::pair<Ipv4, Port>, std::set<Packet>> mock_udp_sent_messages{ };
static std::mt19937 rng{ std::random_device{ }() };

static std::unordered_set<uint64_t> used_addresses{ };
static uint32_t next_mock_ip = 0;
static uint16_t next_mock_port = 0;

static int next_fd = 0;
static std::unordered_map<int, std::shared_ptr<SocketState>> mock_udp_sockets{ };

static void cleanup(int fd) {
  if (fd < 0) {
    return;
  }
  SCU_ASSERT(used_addresses.erase((SCU_AS(uint64_t,
    mock_udp_sockets[fd]->local_ip.ip()) << 32) | mock_udp_sockets[fd]->local_port) == 1,
    "Address was not found on socket close - internal error");
  SCU_ASSERT(mock_udp_sockets.erase(fd) == 1,
      "Socket FD was not found on socket close - internal error");
}

static void bind_to(int fd, Ipv4 local_ip, Port local_port) {
  SCU_ASSERT(mock_udp_sockets.find(
    fd) == mock_udp_sockets.end(), "Socket FD not found during bind - internal error " << fd);
  SCU_ASSERT(used_addresses.insert((SCU_AS(uint64_t, local_ip.ip()) << 32) | local_port).second,
    "Address was occupied");
  mock_udp_sockets.insert({ fd, std::make_shared<SocketState>(SocketState{ local_ip, local_port }) });
  mock_udp_sent_messages[{ local_ip, local_port }].clear();
}

UdpSocket::UdpSocket(bool open)
: UdpSocket() {
  if (open) {
    std::ignore = this->open();
  }
}

UdpSocket::UdpSocket(Ipv4 local_ip, Port local_port)
: UdpSocket() {
  SCU_DO_AND_ASSERT(open(), "Failed to open mock UDP socket");
  SCU_DO_AND_ASSERT(bind(local_ip, local_port), "Failed to bind mock UDP socket");
}

Expected<Success, std::string> UdpSocket::open() {
  std::lock_guard lock(mock_udp_sockets_mutex);
  const auto fd = next_fd++;
  SCU_ASSERT(fd >= 0, "File descriptor overflow");
  _socket_fd.store(fd, std::memory_order_relaxed);
  bind_to(fd, Ipv4(next_mock_ip++), next_mock_port++);
  return Success::instance();
}

bool UdpSocket::close() noexcept {
  // This noexcept is actually a lie in this context but, it is needed to match the interface
  std::lock_guard lock(mock_udp_sockets_mutex);
  if (_socket_fd.load(std::memory_order_relaxed) < 0) {
    return false;
  }
  cleanup(_socket_fd.load(std::memory_order_relaxed));
  _socket_fd.store(-1, std::memory_order_relaxed);
  _is_bound.store(false, std::memory_order_relaxed);
  return true;
}

Expected<Success, std::string> UdpSocket::bind(
  Ipv4 local_ip,
  Port local_port) {
  std::lock_guard lock(mock_udp_sockets_mutex);
  const auto fd = _socket_fd.load(std::memory_order_relaxed);
  if (SCU_UNLIKELY(fd < 0)) {
    return Unexpected("Socket is not valid"s);
  }
  if (_is_bound.load(std::memory_order_relaxed)) {
    return Unexpected("Socket is already bound"s);
  }
  cleanup(fd);
  bind_to(fd, local_ip, local_port);
  _is_bound.store(true, std::memory_order_relaxed);
  return Success::instance();
}

UdpSocket::UdpSocket(UdpSocket&& other)
: _socket_fd(other._socket_fd.exchange(-1, std::memory_order_relaxed)),
  _is_bound(other._is_bound.exchange(false, std::memory_order_relaxed)) { }

UdpSocket& UdpSocket::operator=(UdpSocket&& other) {
  std::lock_guard lock(mock_udp_sockets_mutex);
  cleanup(_socket_fd.load(std::memory_order_relaxed));
  _socket_fd.store(other._socket_fd.exchange(-1, std::memory_order_relaxed),
                   std::memory_order_relaxed);
  _is_bound.store(other._is_bound.exchange(false, std::memory_order_relaxed), std::memory_order_relaxed);
  return *this;
}

SCU_HOT Expected<size_t, std::string> UdpSocket::send(
  const uint8_t* data,
  size_t size,
  Ipv4 remote_ip,
  Port remote_port) const {
  std::lock_guard lock(mock_udp_sockets_mutex);
  const auto fd = _socket_fd.load(std::memory_order_relaxed);
  if (SCU_UNLIKELY(fd < 0)) {
    return Unexpected("Socket is not valid"s);
  }
  auto it = mock_udp_sockets.find(fd);
  SCU_ASSERT(it != mock_udp_sockets.end(), "Socket FD not found during send - internal error");
  if (SCU_AS(double, SCU_MOCK_UDP_PACKET_LOSS_PERCENTAGE) > SCU_AS(double, rng() % 100000) / 1000.0) {
    return size;
  }
  Packet packet;
  const auto delay = (SCU_MOCK_UDP_DELAY_MIN_MS * 1000000) +
    (rng() % ((SCU_MOCK_UDP_DELAY_MAX_MS - SCU_MOCK_UDP_DELAY_MIN_MS) * 1000000));
  packet.arrival_time =
    SCU_AS(size_t, std::chrono::system_clock::now().time_since_epoch().count()) + delay;
  packet.remote_ip = it->second->local_ip;
  packet.remote_port = it->second->local_port;
  packet.data.resize(size);
  std::memcpy(packet.data.data(), data, size);
  mock_udp_sent_messages[{ remote_ip, remote_port }].insert(std::move(packet));
  return size;
}

SCU_HOT Expected<UdpMessageInfo, std::string> UdpSocket::receive(
  uint8_t* data,
  size_t size) const {
  std::unique_lock lock(mock_udp_sockets_mutex, std::defer_lock);
  while (true) {
    lock.lock();
    const auto fd = _socket_fd.load(std::memory_order_relaxed);
    if (SCU_UNLIKELY(fd < 0)) {
      return Unexpected("Socket is not valid"s);
    }
    auto it = mock_udp_sockets.find(fd);
    SCU_ASSERT(it != mock_udp_sockets.end(), "Socket FD not found during receive - internal error");
    auto& sent_messages = mock_udp_sent_messages[{ it->second->local_ip, it->second->local_port }];
    if (sent_messages.empty() || sent_messages.begin()->arrival_time >
      SCU_AS(size_t, std::chrono::system_clock::now().time_since_epoch().count())) {
      lock.unlock();
      std::this_thread::sleep_for(std::chrono::microseconds(250));
      continue;
    }
    auto packet_it = sent_messages.begin();
    auto& packet = *packet_it;
    if (packet.data.size() > size) {
      return Unexpected("Buffer is too small to receive message"s);
    }
    std::memcpy(data, packet.data.data(), packet.data.size());
    UdpMessageInfo info;
    info.byte_count = packet.data.size();
    info.remote_ip = packet.remote_ip;
    info.remote_port = packet.remote_port;
    sent_messages.erase(packet_it);
    return info;
  }
}
