#include <gtest/gtest.h>
#include <atomic>
#include <cstdint>
#include <chrono>
#include <optional>
#include <string>
#include <thread>
#include <utility>
#include <vector>

#if !defined(SCORPIO_UTILS_FRAMEWORK) || SCORPIO_UTILS_FRAMEWORK != 1
# error "SCORPIO_UTILS_FRAMEWORK must be defined to 1 to compile this test"
# ifdef SCORPIO_UTILS_FRAMEWORK
#  undef SCORPIO_UTILS_FRAMEWORK
# endif
# define SCORPIO_UTILS_FRAMEWORK 1
#endif
#include "scorpio_utils/assert.hpp"
#include "scorpio_utils/decorators.hpp"
#include "scorpio_utils/expected.hpp"
#include "scorpio_utils/network/scorpio_udp.hpp"
#include "scorpio_utils/network/udp.hpp"
#include "scorpio_utils/types.hpp"

using scorpio_utils::Expected;
using scorpio_utils::network::Code;
using scorpio_utils::network::Ipv4;
using scorpio_utils::network::Port;
using scorpio_utils::network::SeqNumber;
using scorpio_utils::network::StreamNumber;
using scorpio_utils::network::ScorpioUdp;
using scorpio_utils::network::ScorpioUdpConnection;
using scorpio_utils::network::ScorpioUdpStream;
using scorpio_utils::network::UdpData;
using scorpio_utils::network::UdpMessageInfo;
using scorpio_utils::network::UdpSocket;
using scorpio_utils::Success;
using scorpio_utils::Unexpected;
using scorpio_utils::testing::MockTimeProvider;

std::shared_ptr<scorpio_utils::testing::MockTimeProvider> get_time_provider();

#define AS_BYTE(x) (SCU_AS(uint8_t, x))
#define TICK_TIME (SCU_UDP_HEARTBEAT_PERIOD)
#define WHERE (__FILE__ ":" + std::to_string(__LINE__))

auto generate_single_packet(
  Code code, const std::vector<uint8_t>& data,
  SeqNumber sequence_number = 0,
  std::optional<StreamNumber> stream_number = std::nullopt) {
  std::atomic<size_t> sequence_number_atomic{ sequence_number };
  auto result = generate_packets(stream_number, sequence_number_atomic, code, data);
  SCU_ASSERT(result.has_value(), "Failed to generate single packet message");
  SCU_ASSERT(result->first == sequence_number,
             "Sequence number of generated packet does not match the provided sequence number");
  SCU_ASSERT(result->second.size() <= SCU_UDP_MAX_PACKET_SIZE,
             "Generated packet is too large: " << result->second.size() << " bytes");
  SCU_ASSERT(result->second[0].size() != 0, "Generated packet is empty");
  return std::move(result->second.front());
}

std::string packet_to_string(const std::vector<uint8_t>& packet) {
  std::stringstream ss;
  ss << "Packet(" << packet.size() << " bytes): [";
  for (size_t i = 0; i < packet.size(); ++i) {
    ss << std::hex << static_cast<int>(packet[i]);
    if (i != packet.size() - 1) {
      ss << " ";
    }
  }
  ss << "]";
  return ss.str();
}

auto generate_all_packets(
  Code code, const std::vector<uint8_t>& data,
  SeqNumber sequence_number = 0,
  std::optional<StreamNumber> stream_number = std::nullopt) {
  std::atomic<size_t> sequence_number_atomic{ sequence_number };
  auto result = generate_packets(stream_number, sequence_number_atomic, code, data);
  SCU_ASSERT(result.has_value(), "Failed to generate packets");
  SCU_ASSERT(result->first == sequence_number,
             "First sequence number of generated packets does not match the provided sequence number");
  return std::move(result->second);
}

SCU_ALWAYS_INLINE void write_be16(std::vector<uint8_t>& dst, uint16_t v) {
  dst.push_back(static_cast<uint8_t>((v >> 8) & 0xff));
  dst.push_back(static_cast<uint8_t>(v & 0xff));
}

SCU_ALWAYS_INLINE void write_be32(std::vector<uint8_t>& dst, uint32_t v) {
  dst.push_back(static_cast<uint8_t>((v >> 24) & 0xff));
  dst.push_back(static_cast<uint8_t>((v >> 16) & 0xff));
  dst.push_back(static_cast<uint8_t>((v >> 8) & 0xff));
  dst.push_back(static_cast<uint8_t>(v & 0xff));
}

std::vector<uint8_t> generate_heartbeat_body(
  StreamNumber stream_id, SeqNumber initial_end,
  const std::vector<std::pair<SeqNumber, SeqNumber>>& held_ranges = { }) {
  std::vector<uint8_t> body;
  body.reserve(2 + 1 + 4 + held_ranges.size() * 8);
  write_be16(body, stream_id);
  body.push_back(static_cast<uint8_t>(held_ranges.size()));
  write_be32(body, initial_end);
  for (const auto& [begin, end] : held_ranges) {
    write_be32(body, begin);
    write_be32(body, end);
  }
  return body;
}

auto stream_data_packet(
  StreamNumber stream_id, SeqNumber seq, const std::vector<uint8_t>& data) {
  return generate_single_packet(Code::STREAM_DATA, data, seq, stream_id);
}

std::vector<uint8_t> create_stream_payload(
  StreamNumber stream_id,
  ScorpioUdpStream::StreamQoS qos,
  Code::CreateStreamSubCommands subcommand = Code::CreateStreamSubCommands::CREATE) {
  std::vector<uint8_t> payload;
  payload.push_back(static_cast<uint8_t>(subcommand));
  write_be16(payload, stream_id);
  payload.push_back(static_cast<uint8_t>(qos.reliability));
  if (qos.is_reliable()) {
    write_be16(payload, qos.depth);
  }
  return payload;
}

std::vector<uint8_t> close_stream_payload(
  StreamNumber stream_id, Code::CloseStreamSubCommands subcommand) {
  std::vector<uint8_t> payload;
  payload.push_back(static_cast<uint8_t>(subcommand));
  write_be16(payload, stream_id);
  return payload;
}
class EventInTime {
public:
  virtual Expected<Success, std::string> execute(
    int64_t time,
    UdpSocket& socket,
    std::shared_ptr<ScorpioUdp> connection
  ) = 0;
  virtual std::string name() = 0;
  virtual ~EventInTime() = default;
};

class NoOpEvent final : public EventInTime {
public:
  Expected<Success, std::string> execute(
    int64_t,
    UdpSocket&,
    std::shared_ptr<ScorpioUdp>
  ) override {
    return Success();
  }
  std::string name() override {
    return "NoOpEvent";
  }
  ~NoOpEvent() override = default;
};

class SleepEvent final : public EventInTime {
  const int64_t _sleep_time;

public:
  Expected<Success, std::string> execute(
    int64_t,
    UdpSocket&,
    std::shared_ptr<ScorpioUdp>
  ) override {
    std::this_thread::sleep_for(std::chrono::nanoseconds(_sleep_time));
    return Success();
  }
  std::string name() override {
    return "SleepEvent";
  }
  explicit SleepEvent(int64_t sleep_time)
  : _sleep_time(sleep_time) { }
  ~SleepEvent() override = default;
};

class StartScorpioUdp final : public EventInTime {
public:
  Expected<Success, std::string> execute(
    int64_t,
    UdpSocket&,
    std::shared_ptr<ScorpioUdp> connection
  ) override {
    if (connection->start()) {
      return Success();
    } else {
      return Unexpected("Failed to start connection"s);
    }
  }
  std::string name() override {
    return "StartScorpioUdp";
  }
  ~StartScorpioUdp() override = default;
};

class SetAutoAccept final : public EventInTime {
  const bool _auto_accept;

public:
  explicit SetAutoAccept(bool auto_accept)
  : _auto_accept(auto_accept) { }

  Expected<Success, std::string> execute(
    int64_t,
    UdpSocket&,
    std::shared_ptr<ScorpioUdp> connection
  ) override {
    connection->set_auto_accept(_auto_accept);
    return Success();
  }
  std::string name() override {
    return "SetAutoAccept(" + std::to_string(_auto_accept) + ")";
  }
  ~SetAutoAccept() override = default;
};

class StartListening final : public EventInTime {
  const Ipv4 _ip;
  const Port _port;

public:
  StartListening(Ipv4 ip, Port port)
  : _ip(ip), _port(port) { }

  Expected<Success, std::string> execute(
    int64_t,
    UdpSocket&,
    std::shared_ptr<ScorpioUdp> connection
  ) override {
    if (connection->listen(_ip, _port)) {
      return Success();
    } else {
      return Unexpected("Failed to start listening"s);
    }
  }
  std::string name() override {
    return "StartListening(" + std::to_string(_ip.ip()) + ":" + std::to_string(_port) + ")";
  }
  ~StartListening() override = default;
};

class SendPacket final : public EventInTime {
  const Expected<UdpMessageInfo, std::string> _return_value;
  const std::vector<uint8_t> _data;

public:
  SendPacket(Ipv4 remote_ip, Port remote_port, std::vector<uint8_t> data)
  : _return_value{{ data.size(), remote_ip, remote_port }}, _data(std::move(data)) { }

  Expected<Success, std::string> execute(
    int64_t,
    UdpSocket& socket,
    std::shared_ptr<ScorpioUdp>
  ) override {
    socket.add_to_receive_queue<true>(_return_value, std::move(_data));
    return Success();
  }
  std::string name() override {
    return "SendPacket(" + std::to_string(_return_value.ok_value().remote_ip.ip()) + ":" +
           std::to_string(_return_value.ok_value().remote_port) + ", " +
           std::to_string(_return_value.ok_value().byte_count) +
           " bytes)";
  }
  ~SendPacket() override = default;
};

class ExpectPacket final : public EventInTime {
  const Ipv4 _remote_ip;
  const Port _remote_port;
  const std::vector<uint8_t> _data;

public:
  ExpectPacket(Ipv4 remote_ip, Port remote_port, std::vector<uint8_t> data)
  : _remote_ip(remote_ip), _remote_port(remote_port), _data(std::move(data)) { }

  Expected<Success, std::string> execute(
    int64_t,
    UdpSocket& socket,
    std::shared_ptr<ScorpioUdp>
  ) override {
    if (auto result = socket.get_from_send_queue()) {
      auto [ip, port, data] = *std::move(result);
      if (SCU_UNLIKELY(ip != _remote_ip)) {
        return Unexpected("Expected remote IP "s + _remote_ip.str() + " but got " +
          std::to_string(ip.ip()));
      }
      if (SCU_UNLIKELY(port != _remote_port)) {
        return Unexpected("Expected remote port " + std::to_string(_remote_port) + " but got " + std::to_string(port));
      }
      if (SCU_UNLIKELY(data != _data)) {
        return Unexpected("Expected data " + packet_to_string(_data) + " but got " + packet_to_string(data));
      }
      return Success();
    }
    return Unexpected("No packet was sent"s);
  }
  std::string name() override {
    return "ExpectPacket(" + _remote_ip.str() + ":" + std::to_string(_remote_port) + ", " +
           std::to_string(_data.size()) + " bytes)";
  }
  ~ExpectPacket() override = default;
};

// Returns true if `packet` is a "background" packet that ExpectPacketTimeout
// should silently drain when waiting for some *other* packet. This includes:
// - HEARTBEAT (always periodic; no meaningful "wait for heartbeat" except
// a test explicitly expecting one)
// - CONNECT::CONNECT (retransmitted by the connection's CONNECTING-state
// loop until ACCEPTED is observed)
// - CREATE_STREAM::CREATE (retransmitted on every heartbeat tick while
// a locally-created stream sits in CREATING state)
// A test that *does* expect one of these packets passes `expected` so the
// filter knows not to drop the very thing it's looking for.
static bool is_background_packet(
  const std::vector<uint8_t>& packet,
  const std::vector<uint8_t>& expected) noexcept {
  if (packet.empty()) {
    return false;
  }
  const uint8_t command = packet[0] & 0x0f;
  const uint8_t expected_command =
    expected.empty() ? 0xffu : static_cast<uint8_t>(expected[0] & 0x0f);
  if (command == Code::HEARTBEAT && expected_command != Code::HEARTBEAT) {
    return true;
  }
  if (packet.size() < 2) {
    return false;
  }
  if (command == Code::CONNECT &&
    packet[1] == static_cast<uint8_t>(Code::ConnectionSubCommands::CONNECT) &&
    !(expected_command == Code::CONNECT && expected.size() >= 2 &&
    expected[1] == static_cast<uint8_t>(Code::ConnectionSubCommands::CONNECT))) {
    return true;
  }
  if (command == Code::CREATE_STREAM &&
    packet[1] == static_cast<uint8_t>(Code::CreateStreamSubCommands::CREATE) &&
    !(expected_command == Code::CREATE_STREAM && expected.size() >= 2 &&
    expected[1] == static_cast<uint8_t>(Code::CreateStreamSubCommands::CREATE))) {
    return true;
  }
  return false;
}

class ExpectPacketTimeout final : public EventInTime {
  const Ipv4 _remote_ip;
  const Port _remote_port;
  const std::vector<uint8_t> _data;
  const int64_t _period;
  const size_t _max_attempts;

public:
  ExpectPacketTimeout(
    Ipv4 remote_ip, Port remote_port, std::vector<uint8_t> data,
    int64_t period = TICK_TIME, size_t max_attempts = 20)
  : _remote_ip(remote_ip), _remote_port(remote_port), _data(std::move(data)),
    _period(period), _max_attempts(max_attempts) { }

  Expected<Success, std::string> execute(
    int64_t,
    UdpSocket& socket,
    std::shared_ptr<ScorpioUdp>
  ) override {
    const auto time_provider = get_time_provider();
    for (size_t attempt = 0; attempt < _max_attempts; ++attempt) {
      while (auto result = socket.get_from_send_queue<false>()) {
        auto [ip, port, data] = *std::move(result);
        if (is_background_packet(data, _data)) {
          continue;
        }
        if (SCU_UNLIKELY(ip != _remote_ip)) {
          return Unexpected("Expected remote IP "s + _remote_ip.str() + " but got " +
            std::to_string(ip.ip()));
        }
        if (SCU_UNLIKELY(port != _remote_port)) {
          return Unexpected("Expected remote port " + std::to_string(_remote_port) +
            " but got " + std::to_string(port));
        }
        if (SCU_UNLIKELY(data != _data)) {
          return Unexpected("Expected data " + packet_to_string(_data) + " but got " + packet_to_string(data));
        }
        return Success();
      }
      time_provider->advance_time(_period);
      std::this_thread::sleep_for(std::chrono::nanoseconds(_period));
    }
    return Unexpected("No packet received after "s + std::to_string(_max_attempts) +
      " attempts (period=" + std::to_string(_period) + "ns)");
  }
  std::string name() override {
    return "ExpectPacketTimeout(" + _remote_ip.str() + ":" + std::to_string(_remote_port) + ", " +
           std::to_string(_data.size()) + " bytes, period=" + std::to_string(_period) +
           "ns, max_attempts=" + std::to_string(_max_attempts) + ")";
  }
  ~ExpectPacketTimeout() override = default;
};

class DrainSendQueueEvent final : public EventInTime {
public:
  Expected<Success, std::string> execute(
    int64_t,
    UdpSocket& socket,
    std::shared_ptr<ScorpioUdp>
  ) override {
    while (socket.get_from_send_queue<false>()) { }
    return Success();
  }
  std::string name() override {
    return "DrainSendQueueEvent";
  }
  ~DrainSendQueueEvent() override = default;
};

// Like ExpectPacketTimeout but masks the seq_number field (bytes 3..6) when
// the expected packet is a stream-bound command (STREAM_DATA / CLOSE_STREAM).
// Useful when the seq is allocated by the connection's shared sequence
// counter and its value depends on heartbeat/connect-retransmit timing.
class ExpectPacketAnySeqTimeout final : public EventInTime {
  const Ipv4 _remote_ip;
  const Port _remote_port;
  const std::vector<uint8_t> _data;
  const int64_t _period;
  const size_t _max_attempts;

  static bool matches_ignoring_seq(
    const std::vector<uint8_t>& got, const std::vector<uint8_t>& want) {
    if (got.size() != want.size() || want.size() < 7) {
      return got == want;
    }
    const uint8_t cmd = want[0] & 0x0f;
    if (cmd != Code::STREAM_DATA && cmd != Code::CLOSE_STREAM) {
      return got == want;
    }
    // Compare header (byte 0..2 = code + stream_number) and payload (byte 7+),
    // skip seq_number at bytes 3..6.
    if (!std::equal(got.begin(), got.begin() + 3, want.begin())) {
      return false;
    }
    return std::equal(got.begin() + 7, got.end(), want.begin() + 7);
  }

public:
  ExpectPacketAnySeqTimeout(
    Ipv4 remote_ip, Port remote_port, std::vector<uint8_t> data,
    int64_t period = TICK_TIME, size_t max_attempts = 20)
  : _remote_ip(remote_ip), _remote_port(remote_port), _data(std::move(data)),
    _period(period), _max_attempts(max_attempts) { }

  Expected<Success, std::string> execute(
    int64_t,
    UdpSocket& socket,
    std::shared_ptr<ScorpioUdp>
  ) override {
    const auto time_provider = get_time_provider();
    for (size_t attempt = 0; attempt < _max_attempts; ++attempt) {
      while (auto result = socket.get_from_send_queue<false>()) {
        auto [ip, port, data] = *std::move(result);
        if (is_background_packet(data, _data)) {
          continue;
        }
        if (SCU_UNLIKELY(ip != _remote_ip)) {
          return Unexpected("Expected remote IP "s + _remote_ip.str() + " but got " +
            std::to_string(ip.ip()));
        }
        if (SCU_UNLIKELY(port != _remote_port)) {
          return Unexpected("Expected remote port " + std::to_string(_remote_port) +
            " but got " + std::to_string(port));
        }
        if (SCU_UNLIKELY(!matches_ignoring_seq(data, _data))) {
          return Unexpected("Expected data " + packet_to_string(_data) + " but got " + packet_to_string(data));
        }
        return Success();
      }
      time_provider->advance_time(_period);
      std::this_thread::sleep_for(std::chrono::nanoseconds(_period));
    }
    return Unexpected("No packet received after "s + std::to_string(_max_attempts) +
      " attempts (period=" + std::to_string(_period) + "ns)");
  }
  std::string name() override {
    return "ExpectPacketAnySeqTimeout(" + _remote_ip.str() + ":" + std::to_string(_remote_port) + ", " +
           std::to_string(_data.size()) + " bytes)";
  }
  ~ExpectPacketAnySeqTimeout() override = default;
};

class AdvanceTimeEvent final : public EventInTime {
  const int64_t _ns;

public:
  explicit AdvanceTimeEvent(int64_t ns)
  : _ns(ns) { }
  Expected<Success, std::string> execute(
    int64_t,
    UdpSocket&,
    std::shared_ptr<ScorpioUdp>
  ) override {
    const auto time_provider = get_time_provider();
    int64_t remaining = _ns;
    while (remaining > 0) {
      const auto step = std::min<int64_t>(remaining, TICK_TIME);
      time_provider->advance_time(step);
      std::this_thread::sleep_for(std::chrono::nanoseconds(step));
      remaining -= step;
    }
    return Success();
  }
  std::string name() override {
    return "AdvanceTimeEvent(" + std::to_string(_ns) + "ns)";
  }
  ~AdvanceTimeEvent() override = default;
};

// Drains the send queue for `_max_attempts` ticks. If `_only_stream_data`
// is true, periodic HEARTBEAT/CONNECT keep-alive packets are tolerated and
// only STREAM_DATA-bearing packets cause failure. Otherwise any packet fails.
class ExpectNoPacket final : public EventInTime {
  const int64_t _period;
  const size_t _max_attempts;
  const bool _only_stream_data;

public:
  explicit ExpectNoPacket(
    int64_t period = TICK_TIME, size_t max_attempts = 10,
    bool only_stream_data = false)
  : _period(period), _max_attempts(max_attempts), _only_stream_data(only_stream_data) { }

  Expected<Success, std::string> execute(
    int64_t,
    UdpSocket& socket,
    std::shared_ptr<ScorpioUdp>
  ) override {
    const auto time_provider = get_time_provider();
    for (size_t attempt = 0; attempt < _max_attempts; ++attempt) {
      while (auto result = socket.get_from_send_queue<false>()) {
        auto [ip, port, data] = *std::move(result);
        if (SCU_UNLIKELY(data.empty())) {
          return Unexpected("Empty packet observed in send queue"s);
        }
        if (_only_stream_data) {
          const auto command = static_cast<uint8_t>(data[0] & 0x0f);
          if (command != Code::STREAM_DATA) {
            continue;  // ignore non-STREAM_DATA traffic (heartbeats etc.)
          }
        }
        return Unexpected("Unexpected packet observed in send queue: " + packet_to_string(data));
      }
      time_provider->advance_time(_period);
      std::this_thread::sleep_for(std::chrono::nanoseconds(_period));
    }
    return Success();
  }
  std::string name() override {
    return "ExpectNoPacket(period=" + std::to_string(_period) + "ns, attempts=" +
           std::to_string(_max_attempts) + ", only_stream_data=" + std::to_string(_only_stream_data) + ")";
  }
  ~ExpectNoPacket() override = default;
};

class ConnectionHandle final : public std::enable_shared_from_this<ConnectionHandle> {
  friend class StreamHandle;
  std::optional<std::shared_ptr<ScorpioUdpConnection>> _connection;

  ConnectionHandle() = default;

public:
  static std::shared_ptr<ConnectionHandle> create() {
    return std::shared_ptr<ConnectionHandle>(new ConnectionHandle());
  }

  class GetConnection final : public EventInTime {
    friend class ConnectionHandle;
    const std::shared_ptr<ConnectionHandle> _handle;
    const bool _expect_success;

    explicit GetConnection(std::shared_ptr<ConnectionHandle> handle, bool expect_success)
    : _handle(std::move(handle)), _expect_success{expect_success} { }

public:
    Expected<Success, std::string> execute(
      int64_t,
      UdpSocket&,
      std::shared_ptr<ScorpioUdp> _scorpio_udp
    ) override {
      SCU_ASSERT(!_handle->_connection.has_value(), "Handle already contains a connection");
      _handle->_connection = _scorpio_udp->get_accepted_connection();
      if (SCU_UNLIKELY(_expect_success != _handle->_connection.has_value())) {
        return "Expectation of success not met"s;
      }
      return Success();
    }
    std::string name() override {
      return "GetConnection(expect_success=" + std::to_string(_expect_success) + ")";
    }
    ~GetConnection() override = default;
  };

  std::unique_ptr<GetConnection> get_connection(bool expect_success = true) {
    return std::unique_ptr<GetConnection>(new GetConnection(shared_from_this(), expect_success));
  }

  class CloseConnection final : public EventInTime {
    friend class ConnectionHandle;
    const std::shared_ptr<ConnectionHandle> _handle;
    const bool _expect_success;

    explicit CloseConnection(std::shared_ptr<ConnectionHandle> handle, bool expect_success)
    : _handle(std::move(handle)), _expect_success{expect_success} { }

public:
    Expected<Success, std::string> execute(
      int64_t,
      UdpSocket&,
      std::shared_ptr<ScorpioUdp>
    ) override {
      SCU_ASSERT(_handle->_connection.has_value(), "Handle does not contain a connection");
      if (SCU_UNLIKELY((*(_handle->_connection))->close() != _expect_success)) {
        return "Expectation of success not met"s;
      }
      return Success();
    }
    std::string name() override {
      return "CloseConnection(expect_success=" + std::to_string(_expect_success) + ")";
    }
    ~CloseConnection() override = default;
  };

  std::unique_ptr<CloseConnection> close_connection(bool expect_success = true) {
    return std::unique_ptr<CloseConnection>(new CloseConnection(shared_from_this(), expect_success));
  }

  class CreateConnectionEvent final : public EventInTime {
    friend class ConnectionHandle;
    const std::shared_ptr<ConnectionHandle> _handle;
    const Ipv4 _remote_ip;
    const Port _remote_port;

    CreateConnectionEvent(std::shared_ptr<ConnectionHandle> handle, Ipv4 remote_ip, Port remote_port)
    : _handle(std::move(handle)), _remote_ip(remote_ip), _remote_port(remote_port) { }

public:
    Expected<Success, std::string> execute(
      int64_t,
      UdpSocket&,
      std::shared_ptr<ScorpioUdp> _scorpio_udp
    ) override {
      SCU_ASSERT(!_handle->_connection.has_value(), "Handle already contains a connection");
      _handle->_connection = _scorpio_udp->connect(_remote_ip, _remote_port);
      return Success();
    }
    std::string name() override {
      return "CreateConnectionEvent(" + _remote_ip.str() + ":" + std::to_string(_remote_port) + ")";
    }
    ~CreateConnectionEvent() override = default;
  };

  std::unique_ptr<CreateConnectionEvent> create_connection(Ipv4 remote_ip, Port remote_port) {
    return std::unique_ptr<CreateConnectionEvent>(new CreateConnectionEvent(shared_from_this(), remote_ip,
      remote_port));
  }

  class ConnectionIsAlive final : public EventInTime {
    friend class ConnectionHandle;
    const std::shared_ptr<ConnectionHandle> _handle;
    const bool _expect_alive;
    const int64_t _period;
    const size_t _max_attempts;

    ConnectionIsAlive(
      std::shared_ptr<ConnectionHandle> handle, bool expect_alive,
      int64_t period, size_t max_attempts)
    : _handle(std::move(handle)), _expect_alive{expect_alive},
      _period(period), _max_attempts(max_attempts) { }

public:
    Expected<Success, std::string> execute(
      int64_t,
      UdpSocket&,
      std::shared_ptr<ScorpioUdp>
    ) override {
      SCU_ASSERT(_handle->_connection.has_value(), "Handle does not contain a connection");
      const auto& conn = *(_handle->_connection);
      const auto time_provider = get_time_provider();
      for (size_t attempt = 0; attempt < _max_attempts; ++attempt) {
        if (conn->is_alive() == _expect_alive) {
          return Success();
        }
        time_provider->advance_time(_period);
        std::this_thread::sleep_for(std::chrono::nanoseconds(_period));
      }
      return Unexpected("Connection is_alive expectation not met (expected: "s +
        std::to_string(_expect_alive) + ", got: " + std::to_string(conn->is_alive()) + ")");
    }
    std::string name() override {
      return "ConnectionIsAlive(expect_alive=" + std::to_string(_expect_alive) + ")";
    }
    ~ConnectionIsAlive() override = default;
  };

  std::unique_ptr<ConnectionIsAlive> connection_is_alive(
    bool expect_alive = true, int64_t period = TICK_TIME, size_t max_attempts = 20) {
    return std::unique_ptr<ConnectionIsAlive>(
      new ConnectionIsAlive(shared_from_this(), expect_alive, period, max_attempts));
  }

  class ConnectionAutoAcceptStream final : public EventInTime {
    friend class ConnectionHandle;
    const std::shared_ptr<ConnectionHandle> _handle;
    const bool _auto_accept;

    explicit ConnectionAutoAcceptStream(std::shared_ptr<ConnectionHandle> handle, bool auto_accept)
    : _handle(std::move(handle)), _auto_accept{auto_accept} { }

public:
    Expected<Success, std::string> execute(
      int64_t,
      UdpSocket&,
      std::shared_ptr<ScorpioUdp>
    ) override {
      SCU_ASSERT(_handle->_connection.has_value(), "Handle does not contain a connection");
      (*(_handle->_connection))->set_auto_accept_stream(_auto_accept);
      return Success();
    }
    std::string name() override {
      return "ConnectionAutoAcceptStream(auto_accept=" + std::to_string(_auto_accept) + ")";
    }
    ~ConnectionAutoAcceptStream() override = default;
  };

  std::unique_ptr<ConnectionAutoAcceptStream> connection_auto_accept_streams(bool auto_accept = true) {
    return std::unique_ptr<ConnectionAutoAcceptStream>(new ConnectionAutoAcceptStream(shared_from_this(), auto_accept));
  }

  class ConnectionIsPanic final : public EventInTime {
    friend class ConnectionHandle;
    const std::shared_ptr<ConnectionHandle> _handle;
    const bool _expect_panic;
    const int64_t _period;
    const size_t _max_attempts;

    ConnectionIsPanic(
      std::shared_ptr<ConnectionHandle> handle, bool expect_panic,
      int64_t period, size_t max_attempts)
    : _handle(std::move(handle)), _expect_panic(expect_panic),
      _period(period), _max_attempts(max_attempts) { }

public:
    Expected<Success, std::string> execute(
      int64_t,
      UdpSocket&,
      std::shared_ptr<ScorpioUdp>
    ) override {
      SCU_ASSERT(_handle->_connection.has_value(), "Handle does not contain a connection");
      const auto& conn = *(_handle->_connection);
      const auto time_provider = get_time_provider();
      for (size_t attempt = 0; attempt < _max_attempts; ++attempt) {
        if (conn->is_panic() == _expect_panic) {
          return Success();
        }
        time_provider->advance_time(_period);
        std::this_thread::sleep_for(std::chrono::nanoseconds(_period));
      }
      return Unexpected("Connection panic expectation not met (expected: "s +
        std::to_string(_expect_panic) + ", got: " + std::to_string(conn->is_panic()) + ")");
    }
    std::string name() override {
      return "ConnectionIsPanic(expect_panic=" + std::to_string(_expect_panic) + ")";
    }
    ~ConnectionIsPanic() override = default;
  };

  std::unique_ptr<ConnectionIsPanic> connection_is_panic(
    bool expect_panic = true, int64_t period = TICK_TIME, size_t max_attempts = 20) {
    return std::unique_ptr<ConnectionIsPanic>(
      new ConnectionIsPanic(shared_from_this(), expect_panic, period, max_attempts));
  }
};

class StreamHandle final : public std::enable_shared_from_this<StreamHandle> {
  const std::shared_ptr<ConnectionHandle> _connection_handle;
  std::optional<std::shared_ptr<ScorpioUdpStream>> _stream;

  auto get_connection() const {
    SCU_ASSERT(_connection_handle->_connection.has_value(), "Connection handle does not contain a connection");
    return *_connection_handle->_connection;
  }

  explicit StreamHandle(const std::shared_ptr<ConnectionHandle>& connection_handle)
  : _connection_handle(connection_handle) { }

public:
  static std::shared_ptr<StreamHandle> create(const std::shared_ptr<ConnectionHandle>& connection_handle) {
    return std::shared_ptr<StreamHandle>(new StreamHandle(connection_handle));
  }

  class GetStream final : public EventInTime {
    friend class StreamHandle;
    const std::shared_ptr<StreamHandle> _handle;
    const bool _expect_success;

    explicit GetStream(std::shared_ptr<StreamHandle> handle, bool expect_success)
    : _handle(std::move(handle)), _expect_success{expect_success} { }

public:
    Expected<Success, std::string> execute(
      int64_t,
      UdpSocket&,
      std::shared_ptr<ScorpioUdp>
    ) override {
      SCU_ASSERT(!_handle->_stream.has_value(), "Handle already contains a stream");
      _handle->_stream = _handle->get_connection()->get_accepted_stream();
      if (SCU_UNLIKELY(_expect_success != _handle->_stream.has_value())) {
        return "Expectation of success not met"s;
      }
      return Success();
    }
    std::string name() override {
      return "GetStream(expect_success=" + std::to_string(_expect_success) + ")";
    }
    ~GetStream() override = default;
  };

  std::unique_ptr<GetStream> get_stream(bool expect_success = true) {
    return std::unique_ptr<GetStream>(new GetStream(shared_from_this(), expect_success));
  }

  class CreateStreamEvent final : public EventInTime {
    friend class StreamHandle;
    const std::shared_ptr<StreamHandle> _handle;
    const StreamNumber _stream_id;
    const ScorpioUdpStream::StreamQoS _stream_qos;

    explicit CreateStreamEvent(
      std::shared_ptr<StreamHandle> handle, StreamNumber stream_id,
      ScorpioUdpStream::StreamQoS stream_qos)
    : _handle(std::move(handle)), _stream_id(stream_id), _stream_qos(stream_qos) { }

public:
    Expected<Success, std::string> execute(
      int64_t,
      UdpSocket&,
      std::shared_ptr<ScorpioUdp>
    ) override {
      SCU_ASSERT(!_handle->_stream.has_value(), "Handle already contains a stream");
      _handle->_stream = _handle->get_connection()->create_stream(_stream_id, _stream_qos);
      return Success();
    }
    std::string name() override {
      return "CreateStreamEvent(stream_id=" + std::to_string(_stream_id) + ", stream_qos={depth=" + std::to_string(
        _stream_qos.depth) + ", reliability=" + std::to_string(SCU_AS(int, _stream_qos.reliability)) + "})";
    }
    ~CreateStreamEvent() override = default;
  };

  std::unique_ptr<CreateStreamEvent> create_stream(StreamNumber stream_id, ScorpioUdpStream::StreamQoS stream_qos) {
    return std::unique_ptr<CreateStreamEvent>(new CreateStreamEvent(shared_from_this(), stream_id, stream_qos));
  }

  class CloseStream final : public EventInTime {
    friend class StreamHandle;
    const std::shared_ptr<StreamHandle> _handle;
    const bool _expect_success;

    explicit CloseStream(std::shared_ptr<StreamHandle> handle, bool expect_success)
    : _handle(std::move(handle)), _expect_success{expect_success} { }

public:
    Expected<Success, std::string> execute(
      int64_t,
      UdpSocket&,
      std::shared_ptr<ScorpioUdp>) override {
      SCU_ASSERT(_handle->_stream.has_value(), "Handle does not contain a stream");
      if (SCU_UNLIKELY((*(_handle->_stream))->close() != _expect_success)) {
        return "Expectation of success not met"s;
      }
      return Success();
    }
    std::string name() override {
      return "CloseStream(expect_success=" + std::to_string(_expect_success) + ")";
    }
    ~CloseStream() override = default;
  };

  std::unique_ptr<CloseStream> close_stream(bool expect_success = true) {
    return std::unique_ptr<CloseStream>(new CloseStream(shared_from_this(), expect_success));
  }

  class StreamSend final : public EventInTime {
    friend class StreamHandle;
    const std::shared_ptr<StreamHandle> _handle;
    const std::vector<uint8_t> _data;
    const bool _expect_success;

    StreamSend(std::shared_ptr<StreamHandle> handle, std::vector<uint8_t> data, bool expect_success)
    : _handle(std::move(handle)), _data(std::move(data)), _expect_success(expect_success) { }

public:
    Expected<Success, std::string> execute(
      int64_t,
      UdpSocket&,
      std::shared_ptr<ScorpioUdp>
    ) override {
      SCU_ASSERT(_handle->_stream.has_value(), "Handle does not contain a stream");
      if (SCU_UNLIKELY((*(_handle->_stream))->send(_data) != _expect_success)) {
        return "Stream send expectation not met"s;
      }
      return Success();
    }
    std::string name() override {
      return "StreamSend(" + std::to_string(_data.size()) + " bytes, expect_success=" +
             std::to_string(_expect_success) + ")";
    }
    ~StreamSend() override = default;
  };

  std::unique_ptr<StreamSend> stream_send(std::vector<uint8_t> data, bool expect_success = true) {
    return std::unique_ptr<StreamSend>(new StreamSend(shared_from_this(), std::move(data), expect_success));
  }

  class StreamReceive final : public EventInTime {
    friend class StreamHandle;
    const std::shared_ptr<StreamHandle> _handle;
    const std::vector<uint8_t> _expected;
    const int64_t _period;
    const size_t _max_attempts;

    StreamReceive(
      std::shared_ptr<StreamHandle> handle, std::vector<uint8_t> expected,
      int64_t period, size_t max_attempts)
    : _handle(std::move(handle)), _expected(std::move(expected)),
      _period(period), _max_attempts(max_attempts) { }

public:
    Expected<Success, std::string> execute(
      int64_t,
      UdpSocket&,
      std::shared_ptr<ScorpioUdp>
    ) override {
      SCU_ASSERT(_handle->_stream.has_value(), "Handle does not contain a stream");
      const auto& stream = *(_handle->_stream);
      const auto time_provider = get_time_provider();
      for (size_t attempt = 0; attempt < _max_attempts; ++attempt) {
        if (auto result = stream->receive<false>()) {
          if (SCU_UNLIKELY(*result != _expected)) {
            return Unexpected("Expected data "s + packet_to_string(_expected) +
              " but got " + packet_to_string(*result));
          }
          return Success();
        }
        time_provider->advance_time(_period);
        std::this_thread::sleep_for(std::chrono::nanoseconds(_period));
      }
      return Unexpected("No data received after "s + std::to_string(_max_attempts) +
        " attempts (period=" + std::to_string(_period) + "ns)");
    }
    std::string name() override {
      return "StreamReceive(" + std::to_string(_expected.size()) +
             " bytes, period=" + std::to_string(_period) + "ns, attempts=" +
             std::to_string(_max_attempts) + ")";
    }
    ~StreamReceive() override = default;
  };

  std::unique_ptr<StreamReceive> stream_receive(
    std::vector<uint8_t> expected, int64_t period = TICK_TIME, size_t max_attempts = 20) {
    return std::unique_ptr<StreamReceive>(
      new StreamReceive(shared_from_this(), std::move(expected), period, max_attempts));
  }

  class StreamExpectNoReceive final : public EventInTime {
    friend class StreamHandle;
    const std::shared_ptr<StreamHandle> _handle;
    const int64_t _period;
    const size_t _max_attempts;

    StreamExpectNoReceive(std::shared_ptr<StreamHandle> handle, int64_t period, size_t max_attempts)
    : _handle(std::move(handle)), _period(period), _max_attempts(max_attempts) { }

public:
    Expected<Success, std::string> execute(
      int64_t,
      UdpSocket&,
      std::shared_ptr<ScorpioUdp>
    ) override {
      SCU_ASSERT(_handle->_stream.has_value(), "Handle does not contain a stream");
      const auto& stream = *(_handle->_stream);
      const auto time_provider = get_time_provider();
      for (size_t attempt = 0; attempt < _max_attempts; ++attempt) {
        if (auto result = stream->receive<false>()) {
          return Unexpected("Unexpected data received on stream: "s + packet_to_string(*result));
        }
        time_provider->advance_time(_period);
        std::this_thread::sleep_for(std::chrono::nanoseconds(_period));
      }
      return Success();
    }
    std::string name() override {
      return "StreamExpectNoReceive(period=" + std::to_string(_period) + "ns, attempts=" +
             std::to_string(_max_attempts) + ")";
    }
    ~StreamExpectNoReceive() override = default;
  };

  std::unique_ptr<StreamExpectNoReceive> stream_expect_no_receive(
    int64_t period = TICK_TIME, size_t max_attempts = 10) {
    return std::unique_ptr<StreamExpectNoReceive>(
      new StreamExpectNoReceive(shared_from_this(), period, max_attempts));
  }

  class StreamIsActive final : public EventInTime {
    friend class StreamHandle;
    const std::shared_ptr<StreamHandle> _handle;
    const bool _expect_active;
    const int64_t _period;
    const size_t _max_attempts;

    StreamIsActive(
      std::shared_ptr<StreamHandle> handle, bool expect_active,
      int64_t period, size_t max_attempts)
    : _handle(std::move(handle)), _expect_active(expect_active),
      _period(period), _max_attempts(max_attempts) { }

public:
    Expected<Success, std::string> execute(
      int64_t,
      UdpSocket&,
      std::shared_ptr<ScorpioUdp>
    ) override {
      SCU_ASSERT(_handle->_stream.has_value(), "Handle does not contain a stream");
      const auto& stream = *(_handle->_stream);
      const auto time_provider = get_time_provider();
      for (size_t attempt = 0; attempt < _max_attempts; ++attempt) {
        if (stream->is_active() == _expect_active) {
          return Success();
        }
        time_provider->advance_time(_period);
        std::this_thread::sleep_for(std::chrono::nanoseconds(_period));
      }
      return Unexpected("Stream is_active expectation not met (expected: "s +
        std::to_string(_expect_active) + ", got: " + std::to_string(stream->is_active()) +
        ", state=" + std::to_string(static_cast<int>(stream->state())) + ")");
    }
    std::string name() override {
      return "StreamIsActive(expect_active=" + std::to_string(_expect_active) + ")";
    }
    ~StreamIsActive() override = default;
  };

  std::unique_ptr<StreamIsActive> stream_is_active(
    bool expect_active = true, int64_t period = TICK_TIME, size_t max_attempts = 20) {
    return std::unique_ptr<StreamIsActive>(
      new StreamIsActive(shared_from_this(), expect_active, period, max_attempts));
  }

  class StreamIsPanic final : public EventInTime {
    friend class StreamHandle;
    const std::shared_ptr<StreamHandle> _handle;
    const bool _expect_panic;
    const int64_t _period;
    const size_t _max_attempts;

    StreamIsPanic(
      std::shared_ptr<StreamHandle> handle, bool expect_panic,
      int64_t period, size_t max_attempts)
    : _handle(std::move(handle)), _expect_panic(expect_panic),
      _period(period), _max_attempts(max_attempts) { }

public:
    Expected<Success, std::string> execute(
      int64_t,
      UdpSocket&,
      std::shared_ptr<ScorpioUdp>
    ) override {
      SCU_ASSERT(_handle->_stream.has_value(), "Handle does not contain a stream");
      const auto& stream = *(_handle->_stream);
      const auto time_provider = get_time_provider();
      for (size_t attempt = 0; attempt < _max_attempts; ++attempt) {
        if (stream->is_panic() == _expect_panic) {
          return Success();
        }
        time_provider->advance_time(_period);
        std::this_thread::sleep_for(std::chrono::nanoseconds(_period));
      }
      return Unexpected("Stream is_panic expectation not met (expected: "s +
        std::to_string(_expect_panic) + ", got: " + std::to_string(stream->is_panic()) + ")");
    }
    std::string name() override {
      return "StreamIsPanic(expect_panic=" + std::to_string(_expect_panic) + ")";
    }
    ~StreamIsPanic() override = default;
  };

  std::unique_ptr<StreamIsPanic> stream_is_panic(
    bool expect_panic = true, int64_t period = TICK_TIME, size_t max_attempts = 20) {
    return std::unique_ptr<StreamIsPanic>(
      new StreamIsPanic(shared_from_this(), expect_panic, period, max_attempts));
  }
};

struct EventQueueItem {
  std::string where;
  int64_t time_offset;
  std::unique_ptr<EventInTime> event;
};

class ScorpioUdpTester : public ::testing::Test {
  std::shared_ptr<MockTimeProvider> _time_provider;
  std::unique_ptr<UdpSocket> _socket;
  std::shared_ptr<ScorpioUdp> _connection;
  size_t _task_execution;

  static SCU_ALWAYS_INLINE void stabilize_delay() {
    std::this_thread::sleep_for(std::chrono::nanoseconds(TICK_TIME));
  }

protected:
  void SetUp() override {
    _task_execution = 0;
    _socket = std::make_unique<decltype(_socket)::element_type>();
    _time_provider = get_time_provider();
    _time_provider->set_time(0);
    _connection = ScorpioUdp::create(*_socket);
    stabilize_delay();
  }

  void TearDown() override {
    _time_provider->advance_time(SCU_UDP_TIMEOUT * 1000000000);
    stabilize_delay();
    _socket->close_channels();
    _connection.reset();
    _time_provider->advance_time(SCU_UDP_TIMEOUT * 1000000000);
    stabilize_delay();
    _socket.reset();
    _time_provider.reset();
  }

  void execute_test(const std::vector<EventQueueItem>& events) {
    // Check event list is not broken
    for (const auto& event : events) {
      SCU_ASSERT(event.time_offset >= 0, "Time offset shall not be less than 0 but it is: " << event.time_offset);
      SCU_ASSERT(event.event.get() != nullptr, "Event shall not be a nullptr");
    }

    size_t i = 0;
    for (auto&& [where, time_offset, event] : events) {
      _time_provider->advance_time(time_offset);
      if (time_offset != 0) {
        stabilize_delay();
      }
      auto result = event->execute(_time_provider->get_time(), *_socket, _connection);
      if (SCU_UNLIKELY(result.is_err())) {
        FAIL() << "Test: " << event->name() << " no.: " << i << " in execution: " << _task_execution <<
          " failed: " <<
          result.err_value() << "\nat: " << where;
      }
      ++i;
    }
    ++_task_execution;
  }
};

auto create_connection(std::vector<EventQueueItem>& events) {
  std::shared_ptr<ConnectionHandle> connection_handle = ConnectionHandle::create();
  events.push_back({ WHERE, 0, std::make_unique<StartScorpioUdp>() });
  events.push_back({ WHERE, 0, connection_handle->create_connection(Ipv4(127, 0, 0, 1), 12345) });
  events.push_back({ WHERE, 0, std::make_unique<ExpectPacketTimeout>(Ipv4(127, 0, 0, 1), 12345,
  generate_single_packet(Code::CONNECT, { AS_BYTE(Code::ConnectionSubCommands::CONNECT) })) });
  events.push_back({ WHERE, 0, std::make_unique<SendPacket>(Ipv4(127, 0, 0, 1), 12345,
  generate_single_packet(Code::CONNECT, { AS_BYTE(Code::ConnectionSubCommands::ACCEPTED) })) });
  events.push_back({ WHERE, 0, connection_handle->connection_is_alive(true) });
  return connection_handle;
}

void close_connection(std::vector<EventQueueItem>& events) {
  events.push_back({ WHERE, 0, std::make_unique<SendPacket>(Ipv4(127, 0, 0, 1), 12345,
  generate_single_packet(Code::DISCONNECT, { AS_BYTE(Code::DisconnectSubCommands::DISCONNECT) })) });
  events.push_back({ WHERE, 0,
      std::make_unique<ExpectPacketTimeout>(Ipv4(127, 0, 0, 1), 12345,
  generate_single_packet(Code::DISCONNECT, { AS_BYTE(Code::DisconnectSubCommands::ACCEPTED) })) });
}

TEST_F(ScorpioUdpTester, connect_and_get_closed) {
  std::vector<EventQueueItem> events;
  auto connection_handle = create_connection(events);
  close_connection(events);
  execute_test(events);
}

TEST_F(ScorpioUdpTester, accept_connection_and_close) {
  std::shared_ptr<ConnectionHandle> connection_handle = ConnectionHandle::create();
  std::vector<EventQueueItem> events;
  events.push_back({ WHERE, 0, std::make_unique<StartScorpioUdp>() });
  events.push_back({ WHERE, 0, std::make_unique<StartListening>(Ipv4(127, 0, 0, 1), 10001) });
  events.push_back({ WHERE, 0, std::make_unique<SetAutoAccept>(true) });
  events.push_back({ WHERE, TICK_TIME, std::make_unique<SendPacket>(Ipv4(127, 0, 0, 1), 12345,
  generate_single_packet(Code::CONNECT, { AS_BYTE(Code::ConnectionSubCommands::CONNECT) })) });
  events.push_back({ WHERE, 0, std::make_unique<ExpectPacketTimeout>(Ipv4(127, 0, 0, 1), 12345,
  generate_single_packet(Code::CONNECT, { AS_BYTE(Code::ConnectionSubCommands::ACCEPTED) })) });
  events.push_back({ WHERE, 0, connection_handle->get_connection(true) });
  events.push_back({ WHERE, 0, connection_handle->connection_is_alive(true) });
  events.push_back({ WHERE, 0, connection_handle->close_connection(true) });
  events.push_back({ WHERE, 0, std::make_unique<ExpectPacketTimeout>(Ipv4(127, 0, 0, 1), 12345,
  generate_single_packet(Code::DISCONNECT, { AS_BYTE(Code::DisconnectSubCommands::DISCONNECT) })) });
  execute_test(events);
}

TEST_F(ScorpioUdpTester, reject_connection) {
  std::shared_ptr<ConnectionHandle> connection_handle = ConnectionHandle::create();
  std::vector<EventQueueItem> events;
  events.push_back({ WHERE, 0, std::make_unique<StartScorpioUdp>() });
  events.push_back({ WHERE, 0, std::make_unique<StartListening>(Ipv4(127, 0, 0, 1), 10001) });
  events.push_back({ WHERE, 0, std::make_unique<SetAutoAccept>(false) });
  events.push_back({ WHERE, 0, std::make_unique<SendPacket>(Ipv4(127, 0, 0, 1), 12345,
  generate_single_packet(Code::CONNECT, { AS_BYTE(Code::ConnectionSubCommands::CONNECT) })) });
  events.push_back({ WHERE, 0, std::make_unique<ExpectPacketTimeout>(Ipv4(127, 0, 0, 1), 12345,
  generate_single_packet(Code::CONNECT, { AS_BYTE(Code::ConnectionSubCommands::REJECTED) })) });
  execute_test(events);
}

TEST_F(ScorpioUdpTester, accept_connection_and_get_stream) {
  std::vector<EventQueueItem> events;
  auto connection_handle = create_connection(events);
  std::shared_ptr<StreamHandle> stream_handle = StreamHandle::create(connection_handle);
  events.push_back({ WHERE, 0, connection_handle->connection_auto_accept_streams(true) });
  events.push_back({ WHERE, 0, std::make_unique<SendPacket>(Ipv4(127, 0, 0, 1), 12345,
  generate_single_packet(Code::CREATE_STREAM,
      { AS_BYTE(Code::CreateStreamSubCommands::CREATE), 0x01, 0x00, 0x00, 0x00 })) });
  events.push_back({ WHERE, TICK_TIME, std::make_unique<SleepEvent>(TICK_TIME) });
  events.push_back({ WHERE, TICK_TIME, stream_handle->get_stream(true) });
  execute_test(events);
}

// =====================================================================
// Suite 1 — connection lifecycle
// =====================================================================

TEST_F(ScorpioUdpTester, connect_rejected_by_peer) {
  std::vector<EventQueueItem> events;
  auto connection_handle = ConnectionHandle::create();
  events.push_back({ WHERE, 0, std::make_unique<StartScorpioUdp>() });
  events.push_back({ WHERE, 0, connection_handle->create_connection(Ipv4(127, 0, 0, 1), 12345) });
  events.push_back({ WHERE, 0, std::make_unique<ExpectPacketTimeout>(Ipv4(127, 0, 0, 1), 12345,
    generate_single_packet(Code::CONNECT, { AS_BYTE(Code::ConnectionSubCommands::CONNECT) })) });
  events.push_back({ WHERE, 0, std::make_unique<SendPacket>(Ipv4(127, 0, 0, 1), 12345,
    generate_single_packet(Code::CONNECT, { AS_BYTE(Code::ConnectionSubCommands::REJECTED) })) });
  events.push_back({ WHERE, 0, connection_handle->connection_is_alive(false) });
  // Explicit local close synchronously joins the connection's processing thread
  // before the test ends, avoiding a race in scorpio_udp.cpp:1104 where the
  // thread accesses _time_provider after the CONNECTING loop without holding
  // self_weak; without this, the leftover thread can segfault on subsequent
  // tests' time-provider lifecycles.
  events.push_back({ WHERE, 0, connection_handle->close_connection(true) });
  execute_test(events);
}

TEST_F(ScorpioUdpTester, connection_panics_on_peer_silence) {
  std::vector<EventQueueItem> events;
  auto connection_handle = create_connection(events);
  events.push_back({ WHERE, 0, std::make_unique<DrainSendQueueEvent>() });
  // Advance mock time well past SCU_UDP_TIMEOUT (5s) so the next heartbeat tick
  // detects "no packets received" and panics.
  events.push_back({ WHERE, 0, std::make_unique<AdvanceTimeEvent>(SCU_UDP_TIMEOUT + TICK_TIME * 4) });
  events.push_back({ WHERE, 0, connection_handle->connection_is_panic(true) });
  // Explicit close — see comment in connect_rejected_by_peer.
  events.push_back({ WHERE, 0, connection_handle->close_connection(true) });
  execute_test(events);
}

TEST_F(ScorpioUdpTester, heartbeat_emitted_periodically) {
  std::vector<EventQueueItem> events;
  auto connection_handle = create_connection(events);
  events.push_back({ WHERE, 0, std::make_unique<DrainSendQueueEvent>() });
  // A bare heartbeat with no streams: just the code byte (HEARTBEAT|FIRST=0x45)
  events.push_back({ WHERE, 0, std::make_unique<ExpectPacketTimeout>(Ipv4(127, 0, 0, 1), 12345,
    generate_single_packet(Code::HEARTBEAT, { })) });
  close_connection(events);
  execute_test(events);
}

// =====================================================================
// Suite 2 — stream creation
// =====================================================================

// Establish stream we initiated. Returns stream_handle pre-loaded with events.
static std::shared_ptr<StreamHandle> create_outgoing_reliable_stream(
  std::vector<EventQueueItem>& events,
  std::shared_ptr<ConnectionHandle> connection_handle,
  StreamNumber stream_id,
  uint16_t depth) {
  auto stream_handle = StreamHandle::create(connection_handle);
  const ScorpioUdpStream::StreamQoS qos{
    depth, ScorpioUdpStream::StreamQoS::Reliability::RELIABLE_ORDERED };
  events.push_back({ WHERE, 0, stream_handle->create_stream(stream_id, qos) });
  events.push_back({ WHERE, 0, std::make_unique<ExpectPacketTimeout>(Ipv4(127, 0, 0, 1), 12345,
    generate_single_packet(Code::CREATE_STREAM,
      create_stream_payload(stream_id, qos, Code::CreateStreamSubCommands::CREATE))) });
  events.push_back({ WHERE, 0, std::make_unique<SendPacket>(Ipv4(127, 0, 0, 1), 12345,
    generate_single_packet(Code::CREATE_STREAM,
      create_stream_payload(stream_id, qos, Code::CreateStreamSubCommands::ACCEPT))) });
  events.push_back({ WHERE, 0, stream_handle->stream_is_active(true) });
  return stream_handle;
}

TEST_F(ScorpioUdpTester, outgoing_stream_creation_accepted) {
  std::vector<EventQueueItem> events;
  auto connection_handle = create_connection(events);
  auto stream_handle = create_outgoing_reliable_stream(events, connection_handle, 1, 16);
  close_connection(events);
  execute_test(events);
}

TEST_F(ScorpioUdpTester, outgoing_stream_creation_rejected) {
  std::vector<EventQueueItem> events;
  auto connection_handle = create_connection(events);
  auto stream_handle = StreamHandle::create(connection_handle);
  const ScorpioUdpStream::StreamQoS qos{
    16, ScorpioUdpStream::StreamQoS::Reliability::RELIABLE_ORDERED };
  events.push_back({ WHERE, 0, stream_handle->create_stream(1, qos) });
  events.push_back({ WHERE, 0, std::make_unique<ExpectPacketTimeout>(Ipv4(127, 0, 0, 1), 12345,
    generate_single_packet(Code::CREATE_STREAM,
      create_stream_payload(1, qos, Code::CreateStreamSubCommands::CREATE))) });
  events.push_back({ WHERE, 0, std::make_unique<SendPacket>(Ipv4(127, 0, 0, 1), 12345,
    generate_single_packet(Code::CREATE_STREAM,
      create_stream_payload(1, qos, Code::CreateStreamSubCommands::REJECT))) });
  events.push_back({ WHERE, 0, stream_handle->stream_is_active(false) });
  close_connection(events);
  execute_test(events);
}

TEST_F(ScorpioUdpTester, incoming_stream_rejected_when_not_auto_accept) {
  std::vector<EventQueueItem> events;
  auto connection_handle = create_connection(events);
  // Default auto-accept-stream is false; do NOT set it.
  events.push_back({ WHERE, 0, std::make_unique<DrainSendQueueEvent>() });
  const ScorpioUdpStream::StreamQoS qos{
    16, ScorpioUdpStream::StreamQoS::Reliability::RELIABLE_ORDERED };
  events.push_back({ WHERE, 0, std::make_unique<SendPacket>(Ipv4(127, 0, 0, 1), 12345,
    generate_single_packet(Code::CREATE_STREAM,
      create_stream_payload(7, qos, Code::CreateStreamSubCommands::CREATE))) });
  events.push_back({ WHERE, 0, std::make_unique<ExpectPacketTimeout>(Ipv4(127, 0, 0, 1), 12345,
    generate_single_packet(Code::CREATE_STREAM,
      create_stream_payload(7, qos, Code::CreateStreamSubCommands::REJECT))) });
  close_connection(events);
  execute_test(events);
}

TEST_F(ScorpioUdpTester, stream_close_initiated_by_peer) {
  std::vector<EventQueueItem> events;
  auto connection_handle = create_connection(events);
  events.push_back({ WHERE, 0, connection_handle->connection_auto_accept_streams(true) });
  // Peer creates stream
  const ScorpioUdpStream::StreamQoS qos{
    16, ScorpioUdpStream::StreamQoS::Reliability::RELIABLE_ORDERED };
  events.push_back({ WHERE, 0, std::make_unique<SendPacket>(Ipv4(127, 0, 0, 1), 12345,
    generate_single_packet(Code::CREATE_STREAM,
      create_stream_payload(1, qos, Code::CreateStreamSubCommands::CREATE))) });
  events.push_back({ WHERE, 0, std::make_unique<ExpectPacketTimeout>(Ipv4(127, 0, 0, 1), 12345,
    generate_single_packet(Code::CREATE_STREAM,
      create_stream_payload(1, qos, Code::CreateStreamSubCommands::ACCEPT))) });
  auto stream_handle = StreamHandle::create(connection_handle);
  events.push_back({ WHERE, 0, stream_handle->get_stream(true) });
  events.push_back({ WHERE, 0, stream_handle->stream_is_active(true) });
  // Peer closes the stream
  events.push_back({ WHERE, 0, std::make_unique<SendPacket>(Ipv4(127, 0, 0, 1), 12345,
    generate_single_packet(Code::CLOSE_STREAM,
      close_stream_payload(1, Code::CloseStreamSubCommands::CLOSE), 0, 1)) });
  events.push_back({ WHERE, 0, std::make_unique<ExpectPacketAnySeqTimeout>(Ipv4(127, 0, 0, 1), 12345,
    generate_single_packet(Code::CLOSE_STREAM,
      close_stream_payload(1, Code::CloseStreamSubCommands::CLOSED), 0, 1)) });
  events.push_back({ WHERE, 0, stream_handle->stream_is_active(false) });
  close_connection(events);
  execute_test(events);
}

// =====================================================================
// Suite 3 — reliable stream data
// =====================================================================

// Sets up: outgoing connection, peer-side auto-accept, peer creates reliable
// stream -> we accept it. Returns the local StreamHandle for the accepted stream.
static std::shared_ptr<StreamHandle> establish_incoming_reliable_stream(
  std::vector<EventQueueItem>& events,
  std::shared_ptr<ConnectionHandle> connection_handle,
  StreamNumber stream_id,
  uint16_t depth) {
  events.push_back({ WHERE, 0, connection_handle->connection_auto_accept_streams(true) });
  const ScorpioUdpStream::StreamQoS qos{
    depth, ScorpioUdpStream::StreamQoS::Reliability::RELIABLE_ORDERED };
  events.push_back({ WHERE, 0, std::make_unique<SendPacket>(Ipv4(127, 0, 0, 1), 12345,
    generate_single_packet(Code::CREATE_STREAM,
      create_stream_payload(stream_id, qos, Code::CreateStreamSubCommands::CREATE))) });
  events.push_back({ WHERE, 0, std::make_unique<ExpectPacketTimeout>(Ipv4(127, 0, 0, 1), 12345,
    generate_single_packet(Code::CREATE_STREAM,
      create_stream_payload(stream_id, qos, Code::CreateStreamSubCommands::ACCEPT))) });
  auto stream_handle = StreamHandle::create(connection_handle);
  events.push_back({ WHERE, 0, stream_handle->get_stream(true) });
  events.push_back({ WHERE, 0, stream_handle->stream_is_active(true) });
  return stream_handle;
}

TEST_F(ScorpioUdpTester, reliable_stream_receive_in_order) {
  std::vector<EventQueueItem> events;
  auto connection_handle = create_connection(events);
  auto stream_handle = establish_incoming_reliable_stream(events, connection_handle, 1, 16);
  for (SeqNumber seq = 0; seq < 3; ++seq) {
    std::vector<uint8_t> payload{ static_cast<uint8_t>(0x10 + seq), 0x20, 0x30 };
    events.push_back({ WHERE, 0, std::make_unique<SendPacket>(Ipv4(127, 0, 0, 1), 12345,
      stream_data_packet(1, seq, payload)) });
  }
  for (SeqNumber seq = 0; seq < 3; ++seq) {
    std::vector<uint8_t> payload{ static_cast<uint8_t>(0x10 + seq), 0x20, 0x30 };
    events.push_back({ WHERE, 0, stream_handle->stream_receive(payload) });
  }
  close_connection(events);
  execute_test(events);
}

TEST_F(ScorpioUdpTester, reliable_stream_receive_out_of_order_is_reordered) {
  std::vector<EventQueueItem> events;
  auto connection_handle = create_connection(events);
  auto stream_handle = establish_incoming_reliable_stream(events, connection_handle, 1, 16);
  // Inject seq 2, then 0, then 1
  std::vector<std::vector<uint8_t>> payloads = {
    { 0x10, 0x20, 0x30 },  // seq 0
    { 0x11, 0x21, 0x31 },  // seq 1
    { 0x12, 0x22, 0x32 },  // seq 2
  };
  for (auto seq : { 2u, 0u, 1u }) {
    events.push_back({ WHERE, 0, std::make_unique<SendPacket>(Ipv4(127, 0, 0, 1), 12345,
      stream_data_packet(1, seq, payloads[seq])) });
  }
  // Receive in seq order (0, 1, 2) — the orderer should reassemble.
  for (SeqNumber seq = 0; seq < 3; ++seq) {
    events.push_back({ WHERE, 0, stream_handle->stream_receive(payloads[seq]) });
  }
  close_connection(events);
  execute_test(events);
}

TEST_F(ScorpioUdpTester, reliable_stream_duplicate_ignored) {
  std::vector<EventQueueItem> events;
  auto connection_handle = create_connection(events);
  auto stream_handle = establish_incoming_reliable_stream(events, connection_handle, 1, 16);
  std::vector<uint8_t> payload0{ 0xA0, 0xA1 };
  std::vector<uint8_t> payload1{ 0xB0, 0xB1 };
  events.push_back({ WHERE, 0, std::make_unique<SendPacket>(Ipv4(127, 0, 0, 1), 12345,
    stream_data_packet(1, 0, payload0)) });
  events.push_back({ WHERE, 0, std::make_unique<SendPacket>(Ipv4(127, 0, 0, 1), 12345,
    stream_data_packet(1, 0, payload0)) });  // duplicate of seq 0
  events.push_back({ WHERE, 0, std::make_unique<SendPacket>(Ipv4(127, 0, 0, 1), 12345,
    stream_data_packet(1, 1, payload1)) });
  events.push_back({ WHERE, 0, stream_handle->stream_receive(payload0) });
  events.push_back({ WHERE, 0, stream_handle->stream_receive(payload1) });
  // No further data — the duplicate of seq 0 must have been dropped.
  events.push_back({ WHERE, 0, stream_handle->stream_expect_no_receive() });
  close_connection(events);
  execute_test(events);
}

TEST_F(ScorpioUdpTester, reliable_stream_send_emits_stream_data) {
  std::vector<EventQueueItem> events;
  auto connection_handle = create_connection(events);
  auto stream_handle = create_outgoing_reliable_stream(events, connection_handle, 1, 16);
  events.push_back({ WHERE, 0, std::make_unique<DrainSendQueueEvent>() });
  std::vector<uint8_t> payload{ 0x01, 0x02, 0x03 };
  events.push_back({ WHERE, 0, stream_handle->stream_send(payload) });
  events.push_back({ WHERE, 0, std::make_unique<ExpectPacketTimeout>(Ipv4(127, 0, 0, 1), 12345,
    stream_data_packet(1, 0, payload)) });
  close_connection(events);
  execute_test(events);
}

TEST_F(ScorpioUdpTester, reliable_stream_send_fragmented_emits_multiple_packets) {
  std::vector<EventQueueItem> events;
  auto connection_handle = create_connection(events);
  auto stream_handle = create_outgoing_reliable_stream(events, connection_handle, 1, 64);
  events.push_back({ WHERE, 0, std::make_unique<DrainSendQueueEvent>() });
  // 1200-byte payload exceeds SCU_UDP_MAX_PACKET_SIZE (512) -> fragmented.
  std::vector<uint8_t> payload(1200, 0xAB);
  events.push_back({ WHERE, 0, stream_handle->stream_send(payload) });
  // Expect each fragment that generate_packets would produce.
  auto packets = generate_all_packets(Code::STREAM_DATA, payload, 0, 1);
  for (auto& packet : packets) {
    events.push_back({ WHERE, 0, std::make_unique<ExpectPacketTimeout>(Ipv4(127, 0, 0, 1), 12345,
      std::move(packet)) });
  }
  close_connection(events);
  execute_test(events);
}

// =====================================================================
// Suite 4 — packet loss / retransmission
// =====================================================================

TEST_F(ScorpioUdpTester, reliable_stream_retransmits_on_heartbeat_gap) {
  std::vector<EventQueueItem> events;
  auto connection_handle = create_connection(events);
  auto stream_handle = create_outgoing_reliable_stream(events, connection_handle, 1, 16);
  events.push_back({ WHERE, 0, std::make_unique<DrainSendQueueEvent>() });

  // Send three messages on the stream — they go out as STREAM_DATA seq 0/1/2.
  std::vector<std::vector<uint8_t>> payloads = {
    { 0x01, 0x01 }, { 0x02, 0x02 }, { 0x03, 0x03 },
  };
  for (auto& p : payloads) {
    events.push_back({ WHERE, 0, stream_handle->stream_send(p) });
  }
  for (SeqNumber seq = 0; seq < 3; ++seq) {
    events.push_back({ WHERE, 0, std::make_unique<ExpectPacketTimeout>(Ipv4(127, 0, 0, 1), 12345,
      stream_data_packet(1, seq, payloads[seq])) });
  }

  // Peer heartbeat: "stream 1, delivered up to seq 1, holding [2, 3)" — peer
  // received seq 0 and seq 2 but missed seq 1. Sender must resend seq 1.
  auto hb_body = generate_heartbeat_body(1, /*initial_end=*/ 1, /*held_ranges=*/ { { 2u, 3u } });
  events.push_back({ WHERE, 0, std::make_unique<SendPacket>(Ipv4(127, 0, 0, 1), 12345,
    generate_single_packet(Code::HEARTBEAT, hb_body)) });
  events.push_back({ WHERE, 0, std::make_unique<ExpectPacketTimeout>(Ipv4(127, 0, 0, 1), 12345,
    stream_data_packet(1, 1, payloads[1])) });
  close_connection(events);
  execute_test(events);
}

TEST_F(ScorpioUdpTester, reliable_stream_heartbeat_no_gap_no_retransmission) {
  std::vector<EventQueueItem> events;
  auto connection_handle = create_connection(events);
  auto stream_handle = create_outgoing_reliable_stream(events, connection_handle, 1, 16);
  events.push_back({ WHERE, 0, std::make_unique<DrainSendQueueEvent>() });

  std::vector<std::vector<uint8_t>> payloads = {
    { 0x01 }, { 0x02 }, { 0x03 },
  };
  for (auto& p : payloads) {
    events.push_back({ WHERE, 0, stream_handle->stream_send(p) });
  }
  for (SeqNumber seq = 0; seq < 3; ++seq) {
    events.push_back({ WHERE, 0, std::make_unique<ExpectPacketTimeout>(Ipv4(127, 0, 0, 1), 12345,
      stream_data_packet(1, seq, payloads[seq])) });
  }

  // Heartbeat: peer says it has delivered everything up to seq 3, no holes.
  auto hb_body = generate_heartbeat_body(1, /*initial_end=*/ 3, /*held_ranges=*/ { });
  events.push_back({ WHERE, 0, std::make_unique<SendPacket>(Ipv4(127, 0, 0, 1), 12345,
    generate_single_packet(Code::HEARTBEAT, hb_body)) });
  // No STREAM_DATA should reappear (heartbeats are ignored by the filter).
  events.push_back({ WHERE, 0, std::make_unique<ExpectNoPacket>(TICK_TIME, 10, /*only_stream_data=*/ true) });
  close_connection(events);
  execute_test(events);
}

// =====================================================================
// Suite 5 — unreliable stream data
// =====================================================================

static std::shared_ptr<StreamHandle> establish_incoming_unreliable_stream(
  std::vector<EventQueueItem>& events,
  std::shared_ptr<ConnectionHandle> connection_handle,
  StreamNumber stream_id) {
  events.push_back({ WHERE, 0, connection_handle->connection_auto_accept_streams(true) });
  const ScorpioUdpStream::StreamQoS qos{ 0, ScorpioUdpStream::StreamQoS::Reliability::UNRELIABLE };
  events.push_back({ WHERE, 0, std::make_unique<SendPacket>(Ipv4(127, 0, 0, 1), 12345,
    generate_single_packet(Code::CREATE_STREAM,
      create_stream_payload(stream_id, qos, Code::CreateStreamSubCommands::CREATE))) });
  events.push_back({ WHERE, 0, std::make_unique<ExpectPacketTimeout>(Ipv4(127, 0, 0, 1), 12345,
    generate_single_packet(Code::CREATE_STREAM,
      create_stream_payload(stream_id, qos, Code::CreateStreamSubCommands::ACCEPT))) });
  auto stream_handle = StreamHandle::create(connection_handle);
  events.push_back({ WHERE, 0, stream_handle->get_stream(true) });
  events.push_back({ WHERE, 0, stream_handle->stream_is_active(true) });
  return stream_handle;
}

TEST_F(ScorpioUdpTester, unreliable_stream_single_packet_delivered_immediately) {
  std::vector<EventQueueItem> events;
  auto connection_handle = create_connection(events);
  auto stream_handle = establish_incoming_unreliable_stream(events, connection_handle, 2);
  std::vector<uint8_t> payload{ 0xDE, 0xAD, 0xBE, 0xEF };
  events.push_back({ WHERE, 0, std::make_unique<SendPacket>(Ipv4(127, 0, 0, 1), 12345,
    stream_data_packet(2, 0, payload)) });
  events.push_back({ WHERE, 0, stream_handle->stream_receive(payload) });
  close_connection(events);
  execute_test(events);
}

TEST_F(ScorpioUdpTester, unreliable_stream_fragments_reassembled_out_of_order) {
  std::vector<EventQueueItem> events;
  auto connection_handle = create_connection(events);
  auto stream_handle = establish_incoming_unreliable_stream(events, connection_handle, 2);
  // A payload that fragments into 3 packets (> 2 * 512 bytes after header overhead).
  std::vector<uint8_t> payload;
  payload.reserve(1400);
  for (size_t i = 0; i < 1400; ++i) {
    payload.push_back(static_cast<uint8_t>(i & 0xff));
  }
  auto packets = generate_all_packets(Code::STREAM_DATA, payload, 0, 2);
  ASSERT_EQ(packets.size(), 3u) << "Expected 1400-byte payload to fragment into 3 packets";
  // Inject in order 2, 0, 1 to verify reassembly is order-insensitive.
  for (size_t idx : { 2u, 0u, 1u }) {
    events.push_back({ WHERE, 0, std::make_unique<SendPacket>(Ipv4(127, 0, 0, 1), 12345,
      packets[idx]) });
  }
  events.push_back({ WHERE, 0, stream_handle->stream_receive(payload) });
  close_connection(events);
  execute_test(events);
}

TEST_F(ScorpioUdpTester, unreliable_stream_missing_middle_fragment_not_delivered) {
  std::vector<EventQueueItem> events;
  auto connection_handle = create_connection(events);
  auto stream_handle = establish_incoming_unreliable_stream(events, connection_handle, 2);
  std::vector<uint8_t> payload(1400, 0x77);
  auto packets = generate_all_packets(Code::STREAM_DATA, payload, 0, 2);
  ASSERT_EQ(packets.size(), 3u);
  // Inject only fragments 0 and 2 — middle (1) is "lost".
  events.push_back({ WHERE, 0, std::make_unique<SendPacket>(Ipv4(127, 0, 0, 1), 12345,
    packets[0]) });
  events.push_back({ WHERE, 0, std::make_unique<SendPacket>(Ipv4(127, 0, 0, 1), 12345,
    packets[2]) });
  // Stream must not deliver anything.
  events.push_back({ WHERE, 0, stream_handle->stream_expect_no_receive() });
  close_connection(events);
  execute_test(events);
}

// =====================================================================
// Suite 6 — multi-stream / mixed QoS
// =====================================================================

TEST_F(ScorpioUdpTester, two_reliable_streams_independent_data) {
  std::vector<EventQueueItem> events;
  auto connection_handle = create_connection(events);
  events.push_back({ WHERE, 0, connection_handle->connection_auto_accept_streams(true) });

  const ScorpioUdpStream::StreamQoS qos{
    16, ScorpioUdpStream::StreamQoS::Reliability::RELIABLE_ORDERED };
  // Open stream 1
  events.push_back({ WHERE, 0, std::make_unique<SendPacket>(Ipv4(127, 0, 0, 1), 12345,
    generate_single_packet(Code::CREATE_STREAM,
      create_stream_payload(1, qos, Code::CreateStreamSubCommands::CREATE))) });
  events.push_back({ WHERE, 0, std::make_unique<ExpectPacketTimeout>(Ipv4(127, 0, 0, 1), 12345,
    generate_single_packet(Code::CREATE_STREAM,
      create_stream_payload(1, qos, Code::CreateStreamSubCommands::ACCEPT))) });
  auto stream_a = StreamHandle::create(connection_handle);
  events.push_back({ WHERE, 0, stream_a->get_stream(true) });

  // Open stream 2
  events.push_back({ WHERE, 0, std::make_unique<SendPacket>(Ipv4(127, 0, 0, 1), 12345,
    generate_single_packet(Code::CREATE_STREAM,
      create_stream_payload(2, qos, Code::CreateStreamSubCommands::CREATE))) });
  events.push_back({ WHERE, 0, std::make_unique<ExpectPacketTimeout>(Ipv4(127, 0, 0, 1), 12345,
    generate_single_packet(Code::CREATE_STREAM,
      create_stream_payload(2, qos, Code::CreateStreamSubCommands::ACCEPT))) });
  auto stream_b = StreamHandle::create(connection_handle);
  events.push_back({ WHERE, 0, stream_b->get_stream(true) });

  std::vector<uint8_t> payload_a{ 0xAA, 0xAA, 0xAA };
  std::vector<uint8_t> payload_b{ 0xBB, 0xBB, 0xBB };
  events.push_back({ WHERE, 0, std::make_unique<SendPacket>(Ipv4(127, 0, 0, 1), 12345,
    stream_data_packet(1, 0, payload_a)) });
  events.push_back({ WHERE, 0, std::make_unique<SendPacket>(Ipv4(127, 0, 0, 1), 12345,
    stream_data_packet(2, 0, payload_b)) });

  // Each stream gets its own payload.
  events.push_back({ WHERE, 0, stream_a->stream_receive(payload_a) });
  events.push_back({ WHERE, 0, stream_b->stream_receive(payload_b) });
  // And no cross-talk.
  events.push_back({ WHERE, 0, stream_a->stream_expect_no_receive() });
  events.push_back({ WHERE, 0, stream_b->stream_expect_no_receive() });

  close_connection(events);
  execute_test(events);
}

TEST_F(ScorpioUdpTester, reliable_and_unreliable_concurrent) {
  std::vector<EventQueueItem> events;
  auto connection_handle = create_connection(events);
  events.push_back({ WHERE, 0, connection_handle->connection_auto_accept_streams(true) });

  const ScorpioUdpStream::StreamQoS reliable_qos{
    16, ScorpioUdpStream::StreamQoS::Reliability::RELIABLE_ORDERED };
  events.push_back({ WHERE, 0, std::make_unique<SendPacket>(Ipv4(127, 0, 0, 1), 12345,
    generate_single_packet(Code::CREATE_STREAM,
      create_stream_payload(1, reliable_qos, Code::CreateStreamSubCommands::CREATE))) });
  events.push_back({ WHERE, 0, std::make_unique<ExpectPacketTimeout>(Ipv4(127, 0, 0, 1), 12345,
    generate_single_packet(Code::CREATE_STREAM,
      create_stream_payload(1, reliable_qos, Code::CreateStreamSubCommands::ACCEPT))) });
  auto rel_stream = StreamHandle::create(connection_handle);
  events.push_back({ WHERE, 0, rel_stream->get_stream(true) });

  const ScorpioUdpStream::StreamQoS unreliable_qos{
    0, ScorpioUdpStream::StreamQoS::Reliability::UNRELIABLE };
  events.push_back({ WHERE, 0, std::make_unique<SendPacket>(Ipv4(127, 0, 0, 1), 12345,
    generate_single_packet(Code::CREATE_STREAM,
      create_stream_payload(2, unreliable_qos, Code::CreateStreamSubCommands::CREATE))) });
  events.push_back({ WHERE, 0, std::make_unique<ExpectPacketTimeout>(Ipv4(127, 0, 0, 1), 12345,
    generate_single_packet(Code::CREATE_STREAM,
      create_stream_payload(2, unreliable_qos, Code::CreateStreamSubCommands::ACCEPT))) });
  auto unrel_stream = StreamHandle::create(connection_handle);
  events.push_back({ WHERE, 0, unrel_stream->get_stream(true) });

  std::vector<uint8_t> rel_payload{ 0x10, 0x20, 0x30 };
  std::vector<uint8_t> unrel_payload{ 0xC0, 0xFF, 0xEE };
  events.push_back({ WHERE, 0, std::make_unique<SendPacket>(Ipv4(127, 0, 0, 1), 12345,
    stream_data_packet(1, 0, rel_payload)) });
  events.push_back({ WHERE, 0, std::make_unique<SendPacket>(Ipv4(127, 0, 0, 1), 12345,
    stream_data_packet(2, 0, unrel_payload)) });
  events.push_back({ WHERE, 0, rel_stream->stream_receive(rel_payload) });
  events.push_back({ WHERE, 0, unrel_stream->stream_receive(unrel_payload) });

  close_connection(events);
  execute_test(events);
}

// =====================================================================
// Suite 7 — heartbeat edge cases
// =====================================================================

TEST_F(ScorpioUdpTester, heartbeat_nonexistent_stream_sends_already_closed) {
  std::vector<EventQueueItem> events;
  auto connection_handle = create_connection(events);
  events.push_back({ WHERE, 0, std::make_unique<DrainSendQueueEvent>() });
  std::vector<uint8_t> hb_body;
  write_be16(hb_body, static_cast<uint16_t>(99));
  hb_body.push_back(0);  // ranges
  write_be32(hb_body, 0);  // initial_end
  events.push_back({ WHERE, 0, std::make_unique<SendPacket>(Ipv4(127, 0, 0, 1), 12345,
    generate_single_packet(Code::HEARTBEAT, hb_body)) });
  events.push_back({ WHERE, 0, std::make_unique<ExpectPacketAnySeqTimeout>(Ipv4(127, 0, 0, 1), 12345,
    generate_single_packet(Code::CLOSE_STREAM,
      { AS_BYTE(Code::CloseStreamSubCommands::ALREADY_CLOSED) }, 0, 99)) });
  close_connection(events);
  execute_test(events);
}

TEST_F(ScorpioUdpTester, heartbeat_nonexistent_stream_between_retransmit_streams) {
  std::vector<EventQueueItem> events;
  auto connection_handle = create_connection(events);

  // Streams 2 and 5 are active outgoing reliable streams; stream 99 is intentionally absent.
  // Drain between creates to discard any CREATE_STREAM:CREATE retransmits for stream 2 that
  // may be queued while stream_is_active polls (advancing time triggers heartbeat/update).
  auto stream_a = create_outgoing_reliable_stream(events, connection_handle, 2, 16);
  events.push_back({ WHERE, 0, std::make_unique<DrainSendQueueEvent>() });
  auto stream_b = create_outgoing_reliable_stream(events, connection_handle, 5, 16);
  events.push_back({ WHERE, 0, std::make_unique<DrainSendQueueEvent>() });

  std::vector<std::vector<uint8_t>> payloads_a;
  std::vector<std::vector<uint8_t>> payloads_b;
  for (SeqNumber i = 0; i < 3; ++i) {
    payloads_a.push_back({ static_cast<uint8_t>(i * 2u), static_cast<uint8_t>(i * 2u + 1u) });
    payloads_b.push_back({ static_cast<uint8_t>(0x80u | (i * 2u)), static_cast<uint8_t>(0x80u | (i * 2u + 1u)) });
  }
  for (const auto& p : payloads_a) {
    events.push_back({ WHERE, 0, stream_a->stream_send(p) });
  }
  for (const auto& p : payloads_b) {
    events.push_back({ WHERE, 0, stream_b->stream_send(p) });
  }
  for (SeqNumber seq = 0; seq < 3; ++seq) {
    events.push_back({ WHERE, 0, std::make_unique<ExpectPacketTimeout>(Ipv4(127, 0, 0, 1), 12345,
      stream_data_packet(2, seq, payloads_a[seq])) });
  }
  for (SeqNumber seq = 0; seq < 3; ++seq) {
    events.push_back({ WHERE, 0, std::make_unique<ExpectPacketTimeout>(Ipv4(127, 0, 0, 1), 12345,
      stream_data_packet(5, seq, payloads_b[seq])) });
  }

  // One heartbeat carries three stream entries:
  // stream 2  : initial_end=1, held=[2,3) -> peer received seq 0 and seq 2 but missed seq 1
  // stream 99 : nonexistent (0 ranges)    -> must elicit CLOSE_STREAM ALREADY_CLOSED
  // stream 5  : initial_end=1, held=[2,3) -> same gap as stream 2, retransmit seq 1
  // heartbeat_packet_handler processes entries in order, so responses arrive:
  // retransmit stream 2 seq 1, CLOSE_STREAM for 99, retransmit stream 5 seq 1.
  std::vector<uint8_t> hb_body;
  const auto hb_a = generate_heartbeat_body(2, /*initial_end=*/ 1, /*held_ranges=*/ { { 2u, 3u } });
  hb_body.insert(hb_body.end(), hb_a.begin(), hb_a.end());
  write_be16(hb_body, static_cast<uint16_t>(99));
  hb_body.push_back(0);
  write_be32(hb_body, 0);
  const auto hb_b = generate_heartbeat_body(5, /*initial_end=*/ 1, /*held_ranges=*/ { { 2u, 3u } });
  hb_body.insert(hb_body.end(), hb_b.begin(), hb_b.end());
  events.push_back({ WHERE, 0, std::make_unique<SendPacket>(Ipv4(127, 0, 0, 1), 12345,
    generate_single_packet(Code::HEARTBEAT, hb_body)) });

  events.push_back({ WHERE, 0, std::make_unique<ExpectPacketTimeout>(Ipv4(127, 0, 0, 1), 12345,
    stream_data_packet(2, 1, payloads_a[1])) });
  events.push_back({ WHERE, 0, std::make_unique<ExpectPacketAnySeqTimeout>(Ipv4(127, 0, 0, 1), 12345,
    generate_single_packet(Code::CLOSE_STREAM,
      { AS_BYTE(Code::CloseStreamSubCommands::ALREADY_CLOSED) }, 0, 99)) });
  events.push_back({ WHERE, 0, std::make_unique<ExpectPacketTimeout>(Ipv4(127, 0, 0, 1), 12345,
    stream_data_packet(5, 1, payloads_b[1])) });

  close_connection(events);
  execute_test(events);
}

TEST_F(ScorpioUdpTester, already_closed_response_closes_active_stream) {
  std::vector<EventQueueItem> events;
  auto connection_handle = create_connection(events);
  auto stream_handle = establish_incoming_reliable_stream(events, connection_handle, 1, 16);
  events.push_back({ WHERE, 0, std::make_unique<DrainSendQueueEvent>() });
  // Peer tells us "this stream is already closed on my side" while our stream is active.
  // Expected: stream becomes inactive regardless of its current state.
  events.push_back({ WHERE, 0, std::make_unique<SendPacket>(Ipv4(127, 0, 0, 1), 12345,
    generate_single_packet(Code::CLOSE_STREAM,
      close_stream_payload(1, Code::CloseStreamSubCommands::ALREADY_CLOSED), 0, 1)) });
  events.push_back({ WHERE, 0, stream_handle->stream_is_active(false) });
  close_connection(events);
  execute_test(events);
}
