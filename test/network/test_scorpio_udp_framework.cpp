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
using scorpio_utils::network::ConnectionId;
using scorpio_utils::network::Ipv4;
using scorpio_utils::network::Port;
using scorpio_utils::network::SeqNumber;
using scorpio_utils::network::StreamNumber;
using scorpio_utils::network::StreamEpoch;
using scorpio_utils::network::ScorpioUdp;
using scorpio_utils::network::ScorpioUdpConnection;
using scorpio_utils::network::ScorpioUdpStream;
using scorpio_utils::network::UdpData;
using scorpio_utils::network::UdpMessageInfo;
using scorpio_utils::network::UdpSocket;
using scorpio_utils::Success;
using scorpio_utils::Unexpected;
using scorpio_utils::testing::MockTimeProvider;

#define AS_BYTE(x) (SCU_AS(uint8_t, x))
#define TICK_TIME (SCU_UDP_HEARTBEAT_PERIOD)
#define WHERE (__FILE__ ":" + std::to_string(__LINE__))

auto generate_single_packet(
  Code code, const std::vector<uint8_t>& data,
  SeqNumber sequence_number = 0,
  std::optional<StreamNumber> stream_number = std::nullopt,
  std::optional<StreamEpoch> stream_epoch = std::nullopt) {
  std::atomic<size_t> sequence_number_atomic{ sequence_number };
  auto result = generate_packets(stream_number, sequence_number_atomic, code, data, stream_epoch);
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
  std::optional<StreamNumber> stream_number = std::nullopt,
  std::optional<StreamEpoch> stream_epoch = std::nullopt) {
  std::atomic<size_t> sequence_number_atomic{ sequence_number };
  auto result = generate_packets(stream_number, sequence_number_atomic, code, data, stream_epoch);
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

SCU_ALWAYS_INLINE void write_be64(std::vector<uint8_t>& dst, uint64_t v) {
  for (int shift = 56; shift >= 0; shift -= 8) {
    dst.push_back(static_cast<uint8_t>((v >> shift) & 0xffu));
  }
}

// A CONNECT/DISCONNECT payload: 1-byte subcommand followed by the 8-byte
// big-endian connection id, matching the wire format the implementation expects.
std::vector<uint8_t> id_payload(uint8_t subcommand, ConnectionId connection_id) {
  std::vector<uint8_t> payload;
  payload.reserve(1 + sizeof(ConnectionId));
  payload.push_back(subcommand);
  write_be64(payload, connection_id);
  return payload;
}

// Connection id used by tests that originate a CONNECT/DISCONNECT themselves
// (i.e. where the test, not ScorpioUdp, picks the id). For connections that
// ScorpioUdp initiates the id is random and must be read from the connection.
constexpr ConnectionId TEST_PEER_CONNECTION_ID = 0x1122334455667788ULL;

// Stream epoch used by tests where the peer drives stream creation (the test
// injects the CREATE, so it owns the epoch). The connection stores this value
// and echoes it back on every ACCEPT/REJECT/... and tags reliable STREAM_DATA
// with it, so injected/expected packets for such streams all carry it. For
// streams the local side creates the epoch is random and must instead be
// captured from the outgoing CREATE (see StreamHandle::expect_create).
constexpr StreamEpoch TEST_STREAM_EPOCH = 0x5A;

// One per-stream heartbeat block: stream id, the stream epoch (verified by the
// receiver), the range count, end0, then the held (begin,end) pairs. The epoch
// defaults to TEST_STREAM_EPOCH (peer-created streams); locally-created streams
// carry a random epoch and must pass their captured value.
std::vector<uint8_t> generate_heartbeat_body(
  StreamNumber stream_id, SeqNumber initial_end,
  const std::vector<std::pair<SeqNumber, SeqNumber>>& held_ranges = { },
  StreamEpoch stream_epoch = TEST_STREAM_EPOCH) {
  std::vector<uint8_t> body;
  body.reserve(2 + 1 + 1 + 4 + held_ranges.size() * 8);
  write_be16(body, stream_id);
  body.push_back(static_cast<uint8_t>(stream_epoch));
  body.push_back(static_cast<uint8_t>(held_ranges.size()));
  write_be32(body, initial_end);
  for (const auto& [begin, end] : held_ranges) {
    write_be32(body, begin);
    write_be32(body, end);
  }
  return body;
}

// Full heartbeat payload: the 8-byte connection id followed by the concatenated
// per-stream blocks. The receiver drops heartbeats whose connection id does not
// match, so every injected/expected heartbeat leads with it. Defaults to
// TEST_PEER_CONNECTION_ID (the id create_connection() establishes).
std::vector<uint8_t> heartbeat_payload(
  const std::vector<uint8_t>& blocks = { },
  ConnectionId connection_id = TEST_PEER_CONNECTION_ID) {
  std::vector<uint8_t> payload;
  payload.reserve(sizeof(ConnectionId) + blocks.size());
  write_be64(payload, connection_id);
  payload.insert(payload.end(), blocks.begin(), blocks.end());
  return payload;
}

// Reliable STREAM_DATA carries the stream epoch right after the header; pass the
// epoch for reliable streams and std::nullopt for unreliable ones (which never
// carry an epoch, matching ScorpioUdpStream::send).
auto stream_data_packet(
  StreamNumber stream_id, SeqNumber seq, const std::vector<uint8_t>& data,
  std::optional<StreamEpoch> stream_epoch = std::nullopt) {
  return generate_single_packet(Code::STREAM_DATA, data, seq, stream_id, stream_epoch);
}

// CREATE_STREAM payload: subcommand, the 8-byte connection id, stream id, the
// stream epoch, then QoS. The connection id sits right after the subcommand
// (matching CONNECT/DISCONNECT); the epoch sits between stream id and QoS. All
// of CREATE and every response (ACCEPT/REJECT/REJECT_SIMILAR_EXISTED/
// ALREADY_EXISTS) carry the same layout. connection_id defaults to
// TEST_PEER_CONNECTION_ID because the shared create_connection() helper
// establishes its connection with that id.
std::vector<uint8_t> create_stream_payload(
  StreamNumber stream_id,
  ScorpioUdpStream::StreamQoS qos,
  Code::CreateStreamSubCommands subcommand = Code::CreateStreamSubCommands::CREATE,
  StreamEpoch stream_epoch = TEST_STREAM_EPOCH,
  ConnectionId connection_id = TEST_PEER_CONNECTION_ID) {
  std::vector<uint8_t> payload;
  payload.push_back(static_cast<uint8_t>(subcommand));
  write_be64(payload, connection_id);
  write_be16(payload, stream_id);
  payload.push_back(static_cast<uint8_t>(stream_epoch));
  payload.push_back(static_cast<uint8_t>(qos.reliability));
  if (qos.is_reliable()) {
    write_be16(payload, qos.depth);
  }
  return payload;
}

// CLOSE_STREAM payload: subcommand + 8-byte connection id + stream id + stream
// epoch, for CLOSE and every reply (CLOSED/ALREADY_CLOSED). The receiver drops
// frames whose connection id mismatches and verifies the epoch, so all callers
// carry both. connection_id/stream_epoch default to the values the shared
// create_connection() helper and peer-created streams use.
std::vector<uint8_t> close_stream_payload(
  StreamNumber stream_id, Code::CloseStreamSubCommands subcommand,
  StreamEpoch stream_epoch = TEST_STREAM_EPOCH,
  ConnectionId connection_id = TEST_PEER_CONNECTION_ID) {
  std::vector<uint8_t> payload;
  payload.push_back(static_cast<uint8_t>(subcommand));
  write_be64(payload, connection_id);
  write_be16(payload, stream_id);
  payload.push_back(static_cast<uint8_t>(stream_epoch));
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
    packet[1] == static_cast<uint8_t>(Code::ConnectSubCommands::CONNECT) &&
    !(expected_command == Code::CONNECT && expected.size() >= 2 &&
    expected[1] == static_cast<uint8_t>(Code::ConnectSubCommands::CONNECT))) {
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

// Drains the send queue, skipping background packets, until a packet matching
// `expected` (exact ip/port/bytes) is seen or `max_attempts` time ticks elapse.
// Shared by ExpectPacketTimeout and the connection-id-aware lazy events below.
static Expected<Success, std::string> drain_until_match(
  UdpSocket& socket, Ipv4 remote_ip, Port remote_port,
  const std::vector<uint8_t>& expected, int64_t period, size_t max_attempts) {
  const auto time_provider = ScorpioUdp::get_time_provider();
  for (size_t attempt = 0; attempt < max_attempts; ++attempt) {
    while (auto result = socket.get_from_send_queue<false>()) {
      auto [ip, port, data] = *std::move(result);
      if (is_background_packet(data, expected)) {
        continue;
      }
      if (SCU_UNLIKELY(ip != remote_ip)) {
        return Unexpected("Expected remote IP "s + remote_ip.str() + " but got " +
          std::to_string(ip.ip()));
      }
      if (SCU_UNLIKELY(port != remote_port)) {
        return Unexpected("Expected remote port " + std::to_string(remote_port) +
          " but got " + std::to_string(port));
      }
      if (SCU_UNLIKELY(data != expected)) {
        return Unexpected("Expected data " + packet_to_string(expected) + " but got " + packet_to_string(data));
      }
      return Success();
    }
    time_provider->advance_time(period);
    std::this_thread::sleep_for(std::chrono::nanoseconds(period));
  }
  return Unexpected("No packet received after "s + std::to_string(max_attempts) +
    " attempts (period=" + std::to_string(period) + "ns)");
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
    return drain_until_match(socket, _remote_ip, _remote_port, _data, _period, _max_attempts);
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
    const auto time_provider = ScorpioUdp::get_time_provider();
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

// Like ExpectPacketAnySeqTimeout but ALSO ignores the trailing stream-epoch byte.
// Used for the heartbeat-driven CLOSE_STREAM:ALREADY_CLOSED emitted for a stream
// we never created: heartbeat_packet_handler tags it with the connection's stored
// _streams_epoch[stream] slot, a random per-connection value the test can't predict.
class ExpectClosePacketAnyEpochTimeout final : public EventInTime {
  const Ipv4 _remote_ip;
  const Port _remote_port;
  const std::vector<uint8_t> _data;
  const int64_t _period;
  const size_t _max_attempts;

  static bool matches(const std::vector<uint8_t>& got, const std::vector<uint8_t>& want) {
    // Layout: code(1) stream_number(2) seq(4) subcommand(1) connid(8) streamid(2) epoch(1).
    // Compare code + header stream_number [0..3) and payload up to the epoch
    // [7..end-1); skip seq [3..7) and the trailing epoch byte.
    if (got.size() != want.size() || want.size() < 8) {
      return got == want;
    }
    return std::equal(got.begin(), got.begin() + 3, want.begin()) &&
           std::equal(got.begin() + 7, got.end() - 1, want.begin() + 7);
  }

public:
  ExpectClosePacketAnyEpochTimeout(
    Ipv4 remote_ip, Port remote_port, std::vector<uint8_t> data,
    int64_t period = TICK_TIME, size_t max_attempts = 20)
  : _remote_ip(remote_ip), _remote_port(remote_port), _data(std::move(data)),
    _period(period), _max_attempts(max_attempts) { }

  Expected<Success, std::string> execute(
    int64_t,
    UdpSocket& socket,
    std::shared_ptr<ScorpioUdp>
  ) override {
    const auto time_provider = ScorpioUdp::get_time_provider();
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
        if (SCU_UNLIKELY(!matches(data, _data))) {
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
    return "ExpectClosePacketAnyEpochTimeout(" + _remote_ip.str() + ":" + std::to_string(_remote_port) + ", " +
           std::to_string(_data.size()) + " bytes)";
  }
  ~ExpectClosePacketAnyEpochTimeout() override = default;
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
    const auto time_provider = ScorpioUdp::get_time_provider();
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

// Drains the send queue for `_max_attempts` ticks. The filter argument selects
// which command, if any, the caller cares about. ANY fails on any packet.
// STREAM_DATA_ONLY ignores non-STREAM_DATA traffic (heartbeats etc.) and fails
// only on STREAM_DATA. CREATE_STREAM_CREATE_ONLY likewise filters down to
// CREATE_STREAM:CREATE subcommand packets, used to assert no retransmits of an
// outgoing CREATE while a stream is in CREATING state.
class ExpectNoPacket final : public EventInTime {
public:
  enum class Filter : uint8_t {
    ANY,
    STREAM_DATA_ONLY,
    CREATE_STREAM_CREATE_ONLY,
    CLOSE_STREAM_ONLY,
    // Any CREATE_STREAM subcommand except CREATE itself, i.e. the responses
    // (ACCEPT/REJECT/REJECT_SIMILAR_EXISTED/ALREADY_EXISTS). Used to assert the
    // implementation stays silent on a CREATE it decided to drop.
    CREATE_STREAM_RESPONSE_ONLY,
  };

private:
  const int64_t _period;
  const size_t _max_attempts;
  const Filter _filter;

  static bool packet_matches_filter(const std::vector<uint8_t>& data, Filter filter) noexcept {
    switch (filter) {
      case Filter::ANY:
        return true;
      case Filter::STREAM_DATA_ONLY: {
          const auto command = static_cast<uint8_t>(data[0] & 0x0f);
          return command == Code::STREAM_DATA;
        }
      case Filter::CREATE_STREAM_CREATE_ONLY: {
          if (data.size() < 2) {
            return false;
          }
          const auto command = static_cast<uint8_t>(data[0] & 0x0f);
          return command == Code::CREATE_STREAM &&
                 data[1] == static_cast<uint8_t>(Code::CreateStreamSubCommands::CREATE);
        }
      case Filter::CLOSE_STREAM_ONLY: {
          const auto command = static_cast<uint8_t>(data[0] & 0x0f);
          return command == Code::CLOSE_STREAM;
        }
      case Filter::CREATE_STREAM_RESPONSE_ONLY: {
          if (data.size() < 2) {
            return false;
          }
          const auto command = static_cast<uint8_t>(data[0] & 0x0f);
          return command == Code::CREATE_STREAM &&
                 data[1] != static_cast<uint8_t>(Code::CreateStreamSubCommands::CREATE);
        }
    }
    return false;
  }

public:
  explicit ExpectNoPacket(
    int64_t period = TICK_TIME, size_t max_attempts = 10,
    Filter filter = Filter::ANY)
  : _period(period), _max_attempts(max_attempts), _filter(filter) { }

  Expected<Success, std::string> execute(
    int64_t,
    UdpSocket& socket,
    std::shared_ptr<ScorpioUdp>
  ) override {
    const auto time_provider = ScorpioUdp::get_time_provider();
    for (size_t attempt = 0; attempt < _max_attempts; ++attempt) {
      while (auto result = socket.get_from_send_queue<false>()) {
        auto [ip, port, data] = *std::move(result);
        if (SCU_UNLIKELY(data.empty())) {
          return Unexpected("Empty packet observed in send queue"s);
        }
        if (!packet_matches_filter(data, _filter)) {
          continue;
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
           std::to_string(_max_attempts) + ", filter=" + std::to_string(static_cast<int>(_filter)) + ")";
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
      const auto time_provider = ScorpioUdp::get_time_provider();
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
      const auto time_provider = ScorpioUdp::get_time_provider();
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

  // Injects a CONNECT/DISCONNECT packet carrying THIS connection's id. The id is
  // only known once connect()/accept has run, so it is read from the connection
  // at execute time rather than baked into the packet at construction.
  class SendIdPacket final : public EventInTime {
    friend class ConnectionHandle;
    const std::shared_ptr<ConnectionHandle> _handle;
    const Code::Values _command;
    const uint8_t _subcommand;

    SendIdPacket(std::shared_ptr<ConnectionHandle> handle, Code::Values command, uint8_t subcommand)
    : _handle(std::move(handle)), _command(command), _subcommand(subcommand) { }

public:
    Expected<Success, std::string> execute(
      int64_t,
      UdpSocket& socket,
      std::shared_ptr<ScorpioUdp>
    ) override {
      SCU_ASSERT(_handle->_connection.has_value(), "Handle does not contain a connection");
      const auto& conn = *(_handle->_connection);
      auto packet = generate_single_packet(_command, id_payload(_subcommand, conn->connection_id()));
      const Expected<UdpMessageInfo, std::string> info{ { packet.size(), conn->remote_ip(), conn->remote_port() } };
      socket.add_to_receive_queue<true>(info, std::move(packet));
      return Success();
    }
    std::string name() override {
      return "SendIdPacket(cmd=" + std::to_string(static_cast<int>(_command)) +
             ", sub=" + std::to_string(static_cast<int>(_subcommand)) + ")";
    }
    ~SendIdPacket() override = default;
  };

  std::unique_ptr<SendIdPacket> send_connect(Code::ConnectSubCommands sub) {
    return std::unique_ptr<SendIdPacket>(
      new SendIdPacket(shared_from_this(), Code::CONNECT, static_cast<uint8_t>(sub)));
  }
  std::unique_ptr<SendIdPacket> send_disconnect(Code::DisconnectSubCommands sub) {
    return std::unique_ptr<SendIdPacket>(
      new SendIdPacket(shared_from_this(), Code::DISCONNECT, static_cast<uint8_t>(sub)));
  }

  // Expects a CONNECT/DISCONNECT packet carrying THIS connection's id, draining
  // background retransmits while waiting (same logic as ExpectPacketTimeout).
  class ExpectIdPacket final : public EventInTime {
    friend class ConnectionHandle;
    const std::shared_ptr<ConnectionHandle> _handle;
    const Code::Values _command;
    const uint8_t _subcommand;
    const int64_t _period;
    const size_t _max_attempts;

    ExpectIdPacket(
      std::shared_ptr<ConnectionHandle> handle, Code::Values command, uint8_t subcommand,
      int64_t period, size_t max_attempts)
    : _handle(std::move(handle)), _command(command), _subcommand(subcommand),
      _period(period), _max_attempts(max_attempts) { }

public:
    Expected<Success, std::string> execute(
      int64_t,
      UdpSocket& socket,
      std::shared_ptr<ScorpioUdp>
    ) override {
      SCU_ASSERT(_handle->_connection.has_value(), "Handle does not contain a connection");
      const auto& conn = *(_handle->_connection);
      const auto expected = generate_single_packet(_command, id_payload(_subcommand, conn->connection_id()));
      return drain_until_match(socket, conn->remote_ip(), conn->remote_port(),
        expected, _period, _max_attempts);
    }
    std::string name() override {
      return "ExpectIdPacket(cmd=" + std::to_string(static_cast<int>(_command)) +
             ", sub=" + std::to_string(static_cast<int>(_subcommand)) + ")";
    }
    ~ExpectIdPacket() override = default;
  };

  std::unique_ptr<ExpectIdPacket> expect_connect(
    Code::ConnectSubCommands sub, int64_t period = TICK_TIME, size_t max_attempts = 20) {
    return std::unique_ptr<ExpectIdPacket>(
      new ExpectIdPacket(shared_from_this(), Code::CONNECT, static_cast<uint8_t>(sub), period, max_attempts));
  }
  std::unique_ptr<ExpectIdPacket> expect_disconnect(
    Code::DisconnectSubCommands sub, int64_t period = TICK_TIME, size_t max_attempts = 20) {
    return std::unique_ptr<ExpectIdPacket>(
      new ExpectIdPacket(shared_from_this(), Code::DISCONNECT, static_cast<uint8_t>(sub), period, max_attempts));
  }

  // Expects a bare HEARTBEAT (connection id, no stream blocks) carrying THIS
  // connection's id, draining background retransmits while waiting. Needed for
  // locally-initiated connections whose id is random and known only at run time.
  class ExpectHeartbeat final : public EventInTime {
    friend class ConnectionHandle;
    const std::shared_ptr<ConnectionHandle> _handle;
    const int64_t _period;
    const size_t _max_attempts;

    ExpectHeartbeat(std::shared_ptr<ConnectionHandle> handle, int64_t period, size_t max_attempts)
    : _handle(std::move(handle)), _period(period), _max_attempts(max_attempts) { }

public:
    Expected<Success, std::string> execute(
      int64_t,
      UdpSocket& socket,
      std::shared_ptr<ScorpioUdp>
    ) override {
      SCU_ASSERT(_handle->_connection.has_value(), "Handle does not contain a connection");
      const auto& conn = *(_handle->_connection);
      const auto expected = generate_single_packet(Code::HEARTBEAT, heartbeat_payload({ }, conn->connection_id()));
      return drain_until_match(socket, conn->remote_ip(), conn->remote_port(),
        expected, _period, _max_attempts);
    }
    std::string name() override {
      return "ExpectHeartbeat"s;
    }
    ~ExpectHeartbeat() override = default;
  };

  std::unique_ptr<ExpectHeartbeat> expect_heartbeat(int64_t period = TICK_TIME, size_t max_attempts = 20) {
    return std::unique_ptr<ExpectHeartbeat>(
      new ExpectHeartbeat(shared_from_this(), period, max_attempts));
  }
};

class StreamHandle final : public std::enable_shared_from_this<StreamHandle> {
  const std::shared_ptr<ConnectionHandle> _connection_handle;
  std::optional<std::shared_ptr<ScorpioUdpStream>> _stream;
  // Epoch of a locally-created stream, learned from the outgoing CREATE packet
  // (its value is random per stream instance). Set by ExpectCreateCaptureEpoch,
  // then reused to build the ACCEPT/REJECT/... we inject and the reliable
  // STREAM_DATA / CLOSE we expect back. Unused for peer-created streams, which
  // carry the known TEST_STREAM_EPOCH instead.
  std::optional<StreamEpoch> _captured_epoch;

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
      const auto time_provider = ScorpioUdp::get_time_provider();
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
      const auto time_provider = ScorpioUdp::get_time_provider();
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
      const auto time_provider = ScorpioUdp::get_time_provider();
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

  class StreamIsAlive final : public EventInTime {
    friend class StreamHandle;
    const std::shared_ptr<StreamHandle> _handle;
    const bool _expect_alive;
    const int64_t _period;
    const size_t _max_attempts;

    StreamIsAlive(
      std::shared_ptr<StreamHandle> handle, bool expect_alive,
      int64_t period, size_t max_attempts)
    : _handle(std::move(handle)), _expect_alive(expect_alive),
      _period(period), _max_attempts(max_attempts) { }

public:
    Expected<Success, std::string> execute(
      int64_t,
      UdpSocket&,
      std::shared_ptr<ScorpioUdp>
    ) override {
      SCU_ASSERT(_handle->_stream.has_value(), "Handle does not contain a stream");
      const auto& stream = *(_handle->_stream);
      const auto time_provider = ScorpioUdp::get_time_provider();
      for (size_t attempt = 0; attempt < _max_attempts; ++attempt) {
        if (stream->is_alive() == _expect_alive) {
          return Success();
        }
        time_provider->advance_time(_period);
        std::this_thread::sleep_for(std::chrono::nanoseconds(_period));
      }
      return Unexpected("Stream is_alive expectation not met (expected: "s +
        std::to_string(_expect_alive) + ", got: " + std::to_string(stream->is_alive()) +
        ", state=" + std::to_string(static_cast<int>(stream->state())) + ")");
    }
    std::string name() override {
      return "StreamIsAlive(expect_alive=" + std::to_string(_expect_alive) + ")";
    }
    ~StreamIsAlive() override = default;
  };

  std::unique_ptr<StreamIsAlive> stream_is_alive(
    bool expect_alive = true, int64_t period = TICK_TIME, size_t max_attempts = 20) {
    return std::unique_ptr<StreamIsAlive>(
      new StreamIsAlive(shared_from_this(), expect_alive, period, max_attempts));
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
      const auto time_provider = ScorpioUdp::get_time_provider();
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

  // ---- Epoch-aware events for locally-created streams -----------------------
  // A stream we create locally is assigned a random epoch, so the test cannot
  // know it up front. These events learn it from the outgoing CREATE and reuse
  // it for the response we inject and the reliable traffic we expect back.

  // Drains the send queue until this stream's outgoing CREATE_STREAM:CREATE is
  // seen, validates it (everything but the epoch is known), and records the
  // observed epoch on the handle.
  class ExpectCreateCaptureEpoch final : public EventInTime {
    friend class StreamHandle;
    const std::shared_ptr<StreamHandle> _handle;
    const int64_t _period;
    const size_t _max_attempts;

    ExpectCreateCaptureEpoch(std::shared_ptr<StreamHandle> handle, int64_t period, size_t max_attempts)
    : _handle(std::move(handle)), _period(period), _max_attempts(max_attempts) { }

public:
    Expected<Success, std::string> execute(
      int64_t,
      UdpSocket& socket,
      std::shared_ptr<ScorpioUdp>
    ) override {
      SCU_ASSERT(_handle->_stream.has_value(), "Handle does not contain a stream");
      const auto& stream = *(_handle->_stream);
      const auto connection = _handle->get_connection();
      const auto stream_id = stream->stream_id();
      const auto qos = stream->qos();
      // CREATE_STREAM wire layout: code(1) + subcommand(1) + connection_id(8) +
      // stream_number(2) + epoch(1) + qos. CREATE_STREAM is not a stream command,
      // so no seq is written into the header (the epoch is at a fixed offset).
      constexpr size_t epoch_offset =
        1 + sizeof(Code::CreateStreamSubCommands) + sizeof(ConnectionId) + sizeof(StreamNumber);
      // is_background_packet must not treat the CREATE we are hunting for as
      // background noise; hand it a CREATE-shaped probe so it still drains
      // heartbeats/connect retransmits but stops on our CREATE.
      const std::vector<uint8_t> create_probe{
        AS_BYTE(Code::CREATE_STREAM), AS_BYTE(Code::CreateStreamSubCommands::CREATE) };
      const auto time_provider = ScorpioUdp::get_time_provider();
      for (size_t attempt = 0; attempt < _max_attempts; ++attempt) {
        while (auto result = socket.get_from_send_queue<false>()) {
          auto [ip, port, data] = *std::move(result);
          if (is_background_packet(data, create_probe)) {
            continue;
          }
          if (SCU_UNLIKELY(ip != connection->remote_ip())) {
            return Unexpected("Expected remote IP "s + connection->remote_ip().str() +
              " but got " + std::to_string(ip.ip()));
          }
          if (SCU_UNLIKELY(port != connection->remote_port())) {
            return Unexpected("Expected remote port " + std::to_string(connection->remote_port()) +
              " but got " + std::to_string(port));
          }
          if (SCU_UNLIKELY(data.size() <= epoch_offset)) {
            return Unexpected("CREATE_STREAM packet too short to hold an epoch: " + packet_to_string(data));
          }
          const auto epoch = static_cast<StreamEpoch>(data[epoch_offset]);
          const auto expected = generate_single_packet(
            Code::CREATE_STREAM,
            create_stream_payload(stream_id, qos, Code::CreateStreamSubCommands::CREATE, epoch,
              connection->connection_id()));
          if (SCU_UNLIKELY(data != expected)) {
            return Unexpected("Expected CREATE_STREAM "s + packet_to_string(expected) +
              " but got " + packet_to_string(data));
          }
          _handle->_captured_epoch = epoch;
          return Success();
        }
        time_provider->advance_time(_period);
        std::this_thread::sleep_for(std::chrono::nanoseconds(_period));
      }
      return Unexpected("No CREATE_STREAM packet received after "s + std::to_string(_max_attempts) +
        " attempts (period=" + std::to_string(_period) + "ns)");
    }
    std::string name() override {
      return "ExpectCreateCaptureEpoch"s;
    }
    ~ExpectCreateCaptureEpoch() override = default;
  };

  std::unique_ptr<ExpectCreateCaptureEpoch> expect_create(
    int64_t period = TICK_TIME, size_t max_attempts = 20) {
    return std::unique_ptr<ExpectCreateCaptureEpoch>(
      new ExpectCreateCaptureEpoch(shared_from_this(), period, max_attempts));
  }

  // Injects a CREATE_STREAM response (ACCEPT / REJECT / REJECT_SIMILAR_EXISTED /
  // ALREADY_EXISTS) carrying the captured epoch and the stream's QoS, matching
  // what a real peer echoes back for a locally-initiated CREATE.
  class SendCreateResponse final : public EventInTime {
    friend class StreamHandle;
    const std::shared_ptr<StreamHandle> _handle;
    const Code::CreateStreamSubCommands _subcommand;
    const StreamEpoch _epoch_delta;

    SendCreateResponse(
      std::shared_ptr<StreamHandle> handle, Code::CreateStreamSubCommands subcommand, StreamEpoch epoch_delta)
    : _handle(std::move(handle)), _subcommand(subcommand), _epoch_delta(epoch_delta) { }

public:
    Expected<Success, std::string> execute(
      int64_t,
      UdpSocket& socket,
      std::shared_ptr<ScorpioUdp>
    ) override {
      SCU_ASSERT(_handle->_stream.has_value(), "Handle does not contain a stream");
      SCU_ASSERT(_handle->_captured_epoch.has_value(),
        "Stream epoch was not captured (call expect_create first)");
      const auto& stream = *(_handle->_stream);
      const auto connection = _handle->get_connection();
      auto packet = generate_single_packet(
        Code::CREATE_STREAM,
        create_stream_payload(stream->stream_id(), stream->qos(), _subcommand,
          static_cast<StreamEpoch>(*_handle->_captured_epoch + _epoch_delta),
          connection->connection_id()));
      const Expected<UdpMessageInfo, std::string> info{
        { packet.size(), connection->remote_ip(), connection->remote_port() } };
      socket.add_to_receive_queue<true>(info, std::move(packet));
      return Success();
    }
    std::string name() override {
      return "SendCreateResponse(sub=" + std::to_string(static_cast<int>(_subcommand)) +
             ", epoch_delta=" + std::to_string(static_cast<int>(_epoch_delta)) + ")";
    }
    ~SendCreateResponse() override = default;
  };

  // epoch_delta shifts the epoch relative to the captured one; a non-zero delta
  // simulates a CREATE/response for a DIFFERENT incarnation of the stream.
  std::unique_ptr<SendCreateResponse> send_create_response(
    Code::CreateStreamSubCommands subcommand, StreamEpoch epoch_delta = 0) {
    return std::unique_ptr<SendCreateResponse>(
      new SendCreateResponse(shared_from_this(), subcommand, epoch_delta));
  }

  // Injects a peer heartbeat (connection id + one per-stream block) for this
  // locally-created stream, using the captured random epoch and the live
  // connection id — the values a real peer echoes. Drives NACK-based
  // retransmission from the stream's _sent_history.
  class InjectHeartbeat final : public EventInTime {
    friend class StreamHandle;
    const std::shared_ptr<StreamHandle> _handle;
    const SeqNumber _initial_end;
    const std::vector<std::pair<SeqNumber, SeqNumber>> _held_ranges;

    InjectHeartbeat(
      std::shared_ptr<StreamHandle> handle, SeqNumber initial_end,
      std::vector<std::pair<SeqNumber, SeqNumber>> held_ranges)
    : _handle(std::move(handle)), _initial_end(initial_end), _held_ranges(std::move(held_ranges)) { }

public:
    Expected<Success, std::string> execute(
      int64_t,
      UdpSocket& socket,
      std::shared_ptr<ScorpioUdp>
    ) override {
      SCU_ASSERT(_handle->_stream.has_value(), "Handle does not contain a stream");
      SCU_ASSERT(_handle->_captured_epoch.has_value(),
        "Stream epoch was not captured (call expect_create first)");
      const auto& stream = *(_handle->_stream);
      const auto connection = _handle->get_connection();
      auto packet = generate_single_packet(
        Code::HEARTBEAT,
        heartbeat_payload(
          generate_heartbeat_body(stream->stream_id(), _initial_end, _held_ranges, *_handle->_captured_epoch),
          connection->connection_id()));
      const Expected<UdpMessageInfo, std::string> info{
        { packet.size(), connection->remote_ip(), connection->remote_port() } };
      socket.add_to_receive_queue<true>(info, std::move(packet));
      return Success();
    }
    std::string name() override {
      return "InjectHeartbeat(initial_end=" + std::to_string(_initial_end) + ", " +
             std::to_string(_held_ranges.size()) + " ranges)";
    }
    ~InjectHeartbeat() override = default;
  };

  std::unique_ptr<InjectHeartbeat> inject_heartbeat(
    SeqNumber initial_end, std::vector<std::pair<SeqNumber, SeqNumber>> held_ranges = { }) {
    return std::unique_ptr<InjectHeartbeat>(
      new InjectHeartbeat(shared_from_this(), initial_end, std::move(held_ranges)));
  }

  // One ordered entry in a multi-stream heartbeat (see inject_multi_heartbeat).
  // A local entry (handle set) resolves the stream's random epoch at execute time;
  // a raw entry (handle null) injects a verbatim block built by generate_heartbeat_body
  // (nonexistent stream, or a peer stream carrying a known epoch).
  struct HeartbeatEntry {
    std::shared_ptr<StreamHandle> handle;
    SeqNumber initial_end = 0;
    std::vector<std::pair<SeqNumber, SeqNumber>> held_ranges = { };
    std::vector<uint8_t> raw_block = { };
  };

  static HeartbeatEntry hb_entry(
    std::shared_ptr<StreamHandle> handle, SeqNumber initial_end,
    std::vector<std::pair<SeqNumber, SeqNumber>> held_ranges = { }) {
    return HeartbeatEntry{ std::move(handle), initial_end, std::move(held_ranges), { } };
  }
  static HeartbeatEntry hb_raw(std::vector<uint8_t> raw_block) {
    return HeartbeatEntry{ nullptr, 0, { }, std::move(raw_block) };
  }

  // Injects a single heartbeat packet spanning several stream entries in order,
  // resolving every locally-created stream's random epoch at execute time and
  // prefixing the live connection id. Requires at least one local entry (used to
  // locate the connection). Exercises the multi-entry heartbeat parsing path.
  class InjectMultiHeartbeat final : public EventInTime {
    friend class StreamHandle;
    const std::vector<HeartbeatEntry> _entries;

    explicit InjectMultiHeartbeat(std::vector<HeartbeatEntry> entries)
    : _entries(std::move(entries)) { }

public:
    Expected<Success, std::string> execute(
      int64_t,
      UdpSocket& socket,
      std::shared_ptr<ScorpioUdp>
    ) override {
      std::shared_ptr<ScorpioUdpConnection> connection;
      std::vector<uint8_t> blocks;
      for (const auto& entry : _entries) {
        if (entry.handle) {
          SCU_ASSERT(entry.handle->_stream.has_value(), "Handle does not contain a stream");
          SCU_ASSERT(entry.handle->_captured_epoch.has_value(),
            "Stream epoch was not captured (call expect_create first)");
          connection = entry.handle->get_connection();
          const auto block = generate_heartbeat_body((*(entry.handle->_stream))->stream_id(),
            entry.initial_end, entry.held_ranges, *(entry.handle->_captured_epoch));
          blocks.insert(blocks.end(), block.begin(), block.end());
        } else {
          blocks.insert(blocks.end(), entry.raw_block.begin(), entry.raw_block.end());
        }
      }
      SCU_ASSERT(connection != nullptr, "InjectMultiHeartbeat needs at least one local stream entry");
      auto packet = generate_single_packet(Code::HEARTBEAT,
        heartbeat_payload(blocks, connection->connection_id()));
      const Expected<UdpMessageInfo, std::string> info{
        { packet.size(), connection->remote_ip(), connection->remote_port() } };
      socket.add_to_receive_queue<true>(info, std::move(packet));
      return Success();
    }
    std::string name() override {
      return "InjectMultiHeartbeat(" + std::to_string(_entries.size()) + " entries)";
    }
    ~InjectMultiHeartbeat() override = default;
  };

  static std::unique_ptr<InjectMultiHeartbeat> inject_multi_heartbeat(std::vector<HeartbeatEntry> entries) {
    return std::unique_ptr<InjectMultiHeartbeat>(new InjectMultiHeartbeat(std::move(entries)));
  }

  // Expects a reliable STREAM_DATA packet (seq + payload) tagged with the
  // captured epoch, draining background traffic while waiting.
  class ExpectStreamData final : public EventInTime {
    friend class StreamHandle;
    const std::shared_ptr<StreamHandle> _handle;
    const SeqNumber _seq;
    const std::vector<uint8_t> _payload;
    const int64_t _period;
    const size_t _max_attempts;

    ExpectStreamData(
      std::shared_ptr<StreamHandle> handle, SeqNumber seq, std::vector<uint8_t> payload,
      int64_t period, size_t max_attempts)
    : _handle(std::move(handle)), _seq(seq), _payload(std::move(payload)),
      _period(period), _max_attempts(max_attempts) { }

public:
    Expected<Success, std::string> execute(
      int64_t,
      UdpSocket& socket,
      std::shared_ptr<ScorpioUdp>
    ) override {
      SCU_ASSERT(_handle->_stream.has_value(), "Handle does not contain a stream");
      SCU_ASSERT(_handle->_captured_epoch.has_value(),
        "Stream epoch was not captured (call expect_create first)");
      const auto& stream = *(_handle->_stream);
      const auto connection = _handle->get_connection();
      const auto expected = stream_data_packet(stream->stream_id(), _seq, _payload, *_handle->_captured_epoch);
      return drain_until_match(socket, connection->remote_ip(), connection->remote_port(),
        expected, _period, _max_attempts);
    }
    std::string name() override {
      return "ExpectStreamData(seq=" + std::to_string(_seq) + ", " +
             std::to_string(_payload.size()) + " bytes)";
    }
    ~ExpectStreamData() override = default;
  };

  std::unique_ptr<ExpectStreamData> expect_stream_data(
    SeqNumber seq, std::vector<uint8_t> payload, int64_t period = TICK_TIME, size_t max_attempts = 20) {
    return std::unique_ptr<ExpectStreamData>(
      new ExpectStreamData(shared_from_this(), seq, std::move(payload), period, max_attempts));
  }

  // Expects every fragment of a reliable STREAM_DATA message (built the same way
  // generate_all_packets would) tagged with the captured epoch, in order.
  class ExpectStreamDataAll final : public EventInTime {
    friend class StreamHandle;
    const std::shared_ptr<StreamHandle> _handle;
    const std::vector<uint8_t> _payload;
    const SeqNumber _start_seq;
    const int64_t _period;
    const size_t _max_attempts;

    ExpectStreamDataAll(
      std::shared_ptr<StreamHandle> handle, std::vector<uint8_t> payload, SeqNumber start_seq,
      int64_t period, size_t max_attempts)
    : _handle(std::move(handle)), _payload(std::move(payload)), _start_seq(start_seq),
      _period(period), _max_attempts(max_attempts) { }

public:
    Expected<Success, std::string> execute(
      int64_t,
      UdpSocket& socket,
      std::shared_ptr<ScorpioUdp>
    ) override {
      SCU_ASSERT(_handle->_stream.has_value(), "Handle does not contain a stream");
      SCU_ASSERT(_handle->_captured_epoch.has_value(),
        "Stream epoch was not captured (call expect_create first)");
      const auto& stream = *(_handle->_stream);
      const auto connection = _handle->get_connection();
      auto packets = generate_all_packets(
        Code::STREAM_DATA, _payload, _start_seq, stream->stream_id(), *_handle->_captured_epoch);
      for (auto& packet : packets) {
        auto result = drain_until_match(socket, connection->remote_ip(), connection->remote_port(),
          packet, _period, _max_attempts);
        if (SCU_UNLIKELY(result.is_err())) {
          return result;
        }
      }
      return Success();
    }
    std::string name() override {
      return "ExpectStreamDataAll(" + std::to_string(_payload.size()) + " bytes)";
    }
    ~ExpectStreamDataAll() override = default;
  };

  std::unique_ptr<ExpectStreamDataAll> expect_stream_data_all(
    std::vector<uint8_t> payload, SeqNumber start_seq = 0,
    int64_t period = TICK_TIME, size_t max_attempts = 20) {
    return std::unique_ptr<ExpectStreamDataAll>(
      new ExpectStreamDataAll(shared_from_this(), std::move(payload), start_seq, period, max_attempts));
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
    _time_provider = ScorpioUdp::get_time_provider();
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

// Establishes a connection with a KNOWN id: the peer initiates CONNECT carrying
// TEST_PEER_CONNECTION_ID and the local (auto-accepting) side adopts it. Streams
// on this connection therefore carry TEST_PEER_CONNECTION_ID, which is the
// default connection id baked into create_stream_payload / close_stream_payload.
// Tests that specifically need the local side to be the connection initiator
// drive the connect flow directly instead (see
// reconnect_same_address_before_disconnect_accept).
auto create_connection(std::vector<EventQueueItem>& events) {
  std::shared_ptr<ConnectionHandle> connection_handle = ConnectionHandle::create();
  events.push_back({ WHERE, 0, std::make_unique<StartScorpioUdp>() });
  events.push_back({ WHERE, 0, std::make_unique<StartListening>(Ipv4(127, 0, 0, 1), 10001) });
  events.push_back({ WHERE, 0, std::make_unique<SetAutoAccept>(true) });
  events.push_back({ WHERE, TICK_TIME, std::make_unique<SendPacket>(Ipv4(127, 0, 0, 1), 12345,
    generate_single_packet(Code::CONNECT,
      id_payload(AS_BYTE(Code::ConnectSubCommands::CONNECT), TEST_PEER_CONNECTION_ID))) });
  events.push_back({ WHERE, 0, std::make_unique<ExpectPacketTimeout>(Ipv4(127, 0, 0, 1), 12345,
    generate_single_packet(Code::CONNECT,
      id_payload(AS_BYTE(Code::ConnectSubCommands::ACCEPTED), TEST_PEER_CONNECTION_ID))) });
  events.push_back({ WHERE, 0, connection_handle->get_connection(true) });
  events.push_back({ WHERE, 0, connection_handle->connection_is_alive(true) });
  return connection_handle;
}

void close_connection(std::vector<EventQueueItem>& events, const std::shared_ptr<ConnectionHandle>& connection_handle) {
  events.push_back({ WHERE, 0, connection_handle->send_disconnect(Code::DisconnectSubCommands::DISCONNECT) });
  events.push_back({ WHERE, 0, connection_handle->expect_disconnect(Code::DisconnectSubCommands::ACCEPTED) });
}

TEST_F(ScorpioUdpTester, connect_and_get_closed) {
  std::vector<EventQueueItem> events;
  auto connection_handle = create_connection(events);
  close_connection(events, connection_handle);
  execute_test(events);
}

TEST_F(ScorpioUdpTester, reconnect_same_address_before_disconnect_accept) {
  std::vector<EventQueueItem> events;
  const Ipv4 peer(127, 0, 0, 1);
  const Port port = 12345;

  // conn1: outgoing connect + handshake. This test needs the local side to be the
  // connection initiator, so it drives the connect flow directly rather than using
  // create_connection() (which now uses the accept flow with a fixed peer id).
  auto conn1 = ConnectionHandle::create();
  events.push_back({ WHERE, 0, std::make_unique<StartScorpioUdp>() });
  events.push_back({ WHERE, 0, conn1->create_connection(peer, port) });
  events.push_back({ WHERE, 0, conn1->expect_connect(Code::ConnectSubCommands::CONNECT) });
  events.push_back({ WHERE, 0, conn1->send_connect(Code::ConnectSubCommands::ACCEPTED) });
  events.push_back({ WHERE, 0, conn1->connection_is_alive(true) });

  // create_connection only asserts is_alive(), which is already satisfied in the
  // CONNECTING state, so conn1's injected CONNECT/ACCEPTED may still be sitting
  // unprocessed in the receive queue. Let the socket finish the handshake before
  // the local close below; otherwise the ACCEPTED is handled *after* close() and
  // the implementation emits a stray ERROR packet (ACCEPTED for a non-existing
  // connection) that lands ahead of conn2's CONNECT and breaks expect_connect.
  events.push_back({ WHERE, TICK_TIME, std::make_unique<NoOpEvent>() });

  // Quick local disconnect. close() emits DISCONNECT/DISCONNECT; drain it so the
  // later CONNECT expectation does not trip over it.
  events.push_back({ WHERE, 0, conn1->close_connection(true) });
  events.push_back({ WHERE, 0, conn1->expect_disconnect(Code::DisconnectSubCommands::DISCONNECT) });

  // Reconnect to the SAME ip:port before the peer's DISCONNECT ACCEPT is delivered.
  // conn2 gets its own (different) random connection id.
  auto conn2 = ConnectionHandle::create();
  events.push_back({ WHERE, 0, conn2->create_connection(peer, port) });
  events.push_back({ WHERE, 0, conn2->expect_connect(Code::ConnectSubCommands::CONNECT) });
  events.push_back({ WHERE, 0, conn2->send_connect(Code::ConnectSubCommands::ACCEPTED) });
  events.push_back({ WHERE, 0, conn2->connection_is_alive(true) });
  events.push_back({ WHERE, 0, conn1->connection_is_alive(false) });

  // The late DISCONNECT ACCEPT for the old connection (conn1's id) finally arrives.
  // It must be ignored and must not disturb the new connection.
  events.push_back({ WHERE, 0, conn1->send_disconnect(Code::DisconnectSubCommands::ACCEPTED) });
  events.push_back({ WHERE, 0, conn2->connection_is_alive(true) });

  execute_test(events);
}

TEST_F(ScorpioUdpTester, accept_connection_and_close) {
  std::shared_ptr<ConnectionHandle> connection_handle = ConnectionHandle::create();
  std::vector<EventQueueItem> events;
  events.push_back({ WHERE, 0, std::make_unique<StartScorpioUdp>() });
  events.push_back({ WHERE, 0, std::make_unique<StartListening>(Ipv4(127, 0, 0, 1), 10001) });
  events.push_back({ WHERE, 0, std::make_unique<SetAutoAccept>(true) });
  events.push_back({ WHERE, TICK_TIME, std::make_unique<SendPacket>(Ipv4(127, 0, 0, 1), 12345,
  generate_single_packet(Code::CONNECT,
      id_payload(AS_BYTE(Code::ConnectSubCommands::CONNECT), TEST_PEER_CONNECTION_ID))) });
  events.push_back({ WHERE, 0, std::make_unique<ExpectPacketTimeout>(Ipv4(127, 0, 0, 1), 12345,
  generate_single_packet(Code::CONNECT,
      id_payload(AS_BYTE(Code::ConnectSubCommands::ACCEPTED), TEST_PEER_CONNECTION_ID))) });
  events.push_back({ WHERE, 0, connection_handle->get_connection(true) });
  events.push_back({ WHERE, 0, connection_handle->connection_is_alive(true) });
  events.push_back({ WHERE, 0, connection_handle->close_connection(true) });
  // Incoming connection adopts the peer's id, so the local-close DISCONNECT carries it.
  events.push_back({ WHERE, 0, std::make_unique<ExpectPacketTimeout>(Ipv4(127, 0, 0, 1), 12345,
  generate_single_packet(Code::DISCONNECT,
      id_payload(AS_BYTE(Code::DisconnectSubCommands::DISCONNECT), TEST_PEER_CONNECTION_ID))) });
  execute_test(events);
}

TEST_F(ScorpioUdpTester, reject_connection) {
  std::shared_ptr<ConnectionHandle> connection_handle = ConnectionHandle::create();
  std::vector<EventQueueItem> events;
  events.push_back({ WHERE, 0, std::make_unique<StartScorpioUdp>() });
  events.push_back({ WHERE, 0, std::make_unique<StartListening>(Ipv4(127, 0, 0, 1), 10001) });
  events.push_back({ WHERE, 0, std::make_unique<SetAutoAccept>(false) });
  events.push_back({ WHERE, 0, std::make_unique<SendPacket>(Ipv4(127, 0, 0, 1), 12345,
  generate_single_packet(Code::CONNECT,
      id_payload(AS_BYTE(Code::ConnectSubCommands::CONNECT), TEST_PEER_CONNECTION_ID))) });
  events.push_back({ WHERE, 0, std::make_unique<ExpectPacketTimeout>(Ipv4(127, 0, 0, 1), 12345,
  generate_single_packet(Code::CONNECT,
      id_payload(AS_BYTE(Code::ConnectSubCommands::REJECTED), TEST_PEER_CONNECTION_ID))) });
  execute_test(events);
}

TEST_F(ScorpioUdpTester, accept_connection_and_get_stream) {
  std::vector<EventQueueItem> events;
  auto connection_handle = create_connection(events);
  std::shared_ptr<StreamHandle> stream_handle = StreamHandle::create(connection_handle);
  events.push_back({ WHERE, 0, connection_handle->connection_auto_accept_streams(true) });
  const ScorpioUdpStream::StreamQoS create_qos{ 0, ScorpioUdpStream::StreamQoS::Reliability::UNRELIABLE };
  events.push_back({ WHERE, 0, std::make_unique<SendPacket>(Ipv4(127, 0, 0, 1), 12345,
  generate_single_packet(Code::CREATE_STREAM,
      // subcommand, connection id, stream_number(0x0100 = 256), epoch, reliability(UNRELIABLE)
      create_stream_payload(256, create_qos, Code::CreateStreamSubCommands::CREATE))) });
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
  events.push_back({ WHERE, 0, connection_handle->expect_connect(Code::ConnectSubCommands::CONNECT) });
  events.push_back({ WHERE, 0, connection_handle->send_connect(Code::ConnectSubCommands::REJECTED) });
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
  // A bare heartbeat with no streams: code byte (HEARTBEAT|FIRST=0x45) + connection id.
  events.push_back({ WHERE, 0, std::make_unique<ExpectPacketTimeout>(Ipv4(127, 0, 0, 1), 12345,
    generate_single_packet(Code::HEARTBEAT, heartbeat_payload())) });
  close_connection(events, connection_handle);
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
  // The local stream's epoch is random; capture it from the outgoing CREATE,
  // then echo it back in the ACCEPT a peer would send.
  events.push_back({ WHERE, 0, stream_handle->expect_create() });
  events.push_back({ WHERE, 0, stream_handle->send_create_response(Code::CreateStreamSubCommands::ACCEPT) });
  events.push_back({ WHERE, 0, stream_handle->stream_is_active(true) });
  return stream_handle;
}

TEST_F(ScorpioUdpTester, outgoing_stream_creation_accepted) {
  std::vector<EventQueueItem> events;
  auto connection_handle = create_connection(events);
  auto stream_handle = create_outgoing_reliable_stream(events, connection_handle, 1, 16);
  close_connection(events, connection_handle);
  execute_test(events);
}

TEST_F(ScorpioUdpTester, outgoing_stream_creation_rejected) {
  std::vector<EventQueueItem> events;
  auto connection_handle = create_connection(events);
  auto stream_handle = StreamHandle::create(connection_handle);
  const ScorpioUdpStream::StreamQoS qos{
    16, ScorpioUdpStream::StreamQoS::Reliability::RELIABLE_ORDERED };
  events.push_back({ WHERE, 0, stream_handle->create_stream(1, qos) });
  events.push_back({ WHERE, 0, stream_handle->expect_create() });
  events.push_back({ WHERE, 0, stream_handle->send_create_response(Code::CreateStreamSubCommands::REJECT) });
  events.push_back({ WHERE, 0, stream_handle->stream_is_active(false) });
  close_connection(events, connection_handle);
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
  close_connection(events, connection_handle);
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
  close_connection(events, connection_handle);
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
      stream_data_packet(1, seq, payload, TEST_STREAM_EPOCH)) });
  }
  for (SeqNumber seq = 0; seq < 3; ++seq) {
    std::vector<uint8_t> payload{ static_cast<uint8_t>(0x10 + seq), 0x20, 0x30 };
    events.push_back({ WHERE, 0, stream_handle->stream_receive(payload) });
  }
  close_connection(events, connection_handle);
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
      stream_data_packet(1, seq, payloads[seq], TEST_STREAM_EPOCH)) });
  }
  // Receive in seq order (0, 1, 2) — the orderer should reassemble.
  for (SeqNumber seq = 0; seq < 3; ++seq) {
    events.push_back({ WHERE, 0, stream_handle->stream_receive(payloads[seq]) });
  }
  close_connection(events, connection_handle);
  execute_test(events);
}

TEST_F(ScorpioUdpTester, reliable_stream_duplicate_ignored) {
  std::vector<EventQueueItem> events;
  auto connection_handle = create_connection(events);
  auto stream_handle = establish_incoming_reliable_stream(events, connection_handle, 1, 16);
  std::vector<uint8_t> payload0{ 0xA0, 0xA1 };
  std::vector<uint8_t> payload1{ 0xB0, 0xB1 };
  events.push_back({ WHERE, 0, std::make_unique<SendPacket>(Ipv4(127, 0, 0, 1), 12345,
    stream_data_packet(1, 0, payload0, TEST_STREAM_EPOCH)) });
  events.push_back({ WHERE, 0, std::make_unique<SendPacket>(Ipv4(127, 0, 0, 1), 12345,
    stream_data_packet(1, 0, payload0, TEST_STREAM_EPOCH)) });  // duplicate of seq 0
  events.push_back({ WHERE, 0, std::make_unique<SendPacket>(Ipv4(127, 0, 0, 1), 12345,
    stream_data_packet(1, 1, payload1, TEST_STREAM_EPOCH)) });
  events.push_back({ WHERE, 0, stream_handle->stream_receive(payload0) });
  events.push_back({ WHERE, 0, stream_handle->stream_receive(payload1) });
  // No further data — the duplicate of seq 0 must have been dropped.
  events.push_back({ WHERE, 0, stream_handle->stream_expect_no_receive() });
  close_connection(events, connection_handle);
  execute_test(events);
}

TEST_F(ScorpioUdpTester, reliable_stream_send_emits_stream_data) {
  std::vector<EventQueueItem> events;
  auto connection_handle = create_connection(events);
  auto stream_handle = create_outgoing_reliable_stream(events, connection_handle, 1, 16);
  events.push_back({ WHERE, 0, std::make_unique<DrainSendQueueEvent>() });
  std::vector<uint8_t> payload{ 0x01, 0x02, 0x03 };
  events.push_back({ WHERE, 0, stream_handle->stream_send(payload) });
  events.push_back({ WHERE, 0, stream_handle->expect_stream_data(0, payload) });
  close_connection(events, connection_handle);
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
  // Expect each fragment that generate_packets would produce (tagged with the
  // stream's captured epoch).
  events.push_back({ WHERE, 0, stream_handle->expect_stream_data_all(payload) });
  close_connection(events, connection_handle);
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
    events.push_back({ WHERE, 0, stream_handle->expect_stream_data(seq, payloads[seq]) });
  }

  // Peer heartbeat: "stream 1, delivered up to seq 1, holding [2, 3)" — peer
  // received seq 0 and seq 2 but missed seq 1. Sender must resend seq 1. Time is
  // advanced past SCU_UDP_RESEND_INTERVAL first: packets younger than the resend
  // interval are considered in flight and are not retransmitted.
  events.push_back({ WHERE, SCU_UDP_RESEND_INTERVAL,
      stream_handle->inject_heartbeat(/*initial_end=*/ 1, /*held_ranges=*/ { { 2u, 3u } }) });
  events.push_back({ WHERE, 0, stream_handle->expect_stream_data(1, payloads[1]) });
  close_connection(events, connection_handle);
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
    events.push_back({ WHERE, 0, stream_handle->expect_stream_data(seq, payloads[seq]) });
  }

  // Heartbeat: peer says it has delivered everything up to seq 3, no holes.
  events.push_back({ WHERE, 0, stream_handle->inject_heartbeat(/*initial_end=*/ 3, /*held_ranges=*/ { }) });
  // No STREAM_DATA should reappear (heartbeats are ignored by the filter).
  events.push_back({ WHERE, 0, std::make_unique<ExpectNoPacket>(TICK_TIME, 10,
    ExpectNoPacket::Filter::STREAM_DATA_ONLY) });
  close_connection(events, connection_handle);
  execute_test(events);
}

// Regression: the resend window must span the whole _sent_history ring buffer
// (depth + SCU_UDP_QOS_DEPTH_SAFETY_BUFFER), not just `depth`. A packet older than
// `depth` but still physically in the buffer must be retransmitted on NACK. Before
// the fix this logged "already out of resend history" and dropped the packet, which
// permanently wedged RELIABLE_ORDERED streams (e.g. the bridge signalling stream)
// when loss struck during a burst larger than `depth`.
TEST_F(ScorpioUdpTester, reliable_stream_retransmits_beyond_depth_within_buffer) {
  std::vector<EventQueueItem> events;
  auto connection_handle = create_connection(events);
  // Small depth so a modest burst easily exceeds it while staying well inside the
  // depth + SCU_UDP_QOS_DEPTH_SAFETY_BUFFER ring buffer.
  constexpr uint16_t kDepth = 8;
  auto stream_handle = create_outgoing_reliable_stream(events, connection_handle, 1, kDepth);
  events.push_back({ WHERE, 0, std::make_unique<DrainSendQueueEvent>() });

  // Send more packets than `depth` (seq 0..11). The send path is bounded by the full
  // buffer size, not depth, so this stays below the QoS-depth panic threshold.
  constexpr SeqNumber kSent = 12;
  static_assert(kSent > kDepth, "must send more than depth to exercise the beyond-depth path");
  std::vector<std::vector<uint8_t>> payloads;
  for (SeqNumber seq = 0; seq < kSent; ++seq) {
    payloads.push_back({ static_cast<uint8_t>(0xA0 + seq), static_cast<uint8_t>(seq) });
  }
  for (auto& p : payloads) {
    events.push_back({ WHERE, 0, stream_handle->stream_send(p) });
  }
  for (SeqNumber seq = 0; seq < kSent; ++seq) {
    events.push_back({ WHERE, 0, stream_handle->expect_stream_data(seq, payloads[seq]) });
  }

  // Peer heartbeat: delivered up to seq 1, holding [2, kSent) -> only seq 1 is missing.
  // seq 1 is far older than `depth` (current seq is kSent) yet well within the buffer,
  // so the sender must still resend it instead of declaring it out of history.
  // (Time advances past SCU_UDP_RESEND_INTERVAL so the packet is no longer "in flight".)
  events.push_back({ WHERE, SCU_UDP_RESEND_INTERVAL,
      stream_handle->inject_heartbeat(/*initial_end=*/ 1, /*held_ranges=*/ { { 2u, kSent } }) });
  events.push_back({ WHERE, 0, stream_handle->expect_stream_data(1, payloads[1]) });
  close_connection(events, connection_handle);
  execute_test(events);
}

// Regression: a peer that misses the very FIRST packet (seq 0) must get it resent while it
// is still in history. The old `i <= sat_sub(seq, size)` check reported seq 0 as "out of
// resend history" from the start (sat_sub saturates to 0 before the buffer fills), so a lost
// first packet on a young stream could never be recovered.
TEST_F(ScorpioUdpTester, reliable_stream_retransmits_seq_zero_on_young_stream) {
  std::vector<EventQueueItem> events;
  auto connection_handle = create_connection(events);
  auto stream_handle = create_outgoing_reliable_stream(events, connection_handle, 1, 16);
  events.push_back({ WHERE, 0, std::make_unique<DrainSendQueueEvent>() });

  // Only a few packets sent -> the buffer is nowhere near full, so seq 0 is still retained.
  std::vector<std::vector<uint8_t>> payloads = { { 0xA0 }, { 0xA1 }, { 0xA2 } };
  for (auto& p : payloads) {
    events.push_back({ WHERE, 0, stream_handle->stream_send(p) });
  }
  for (SeqNumber seq = 0; seq < 3; ++seq) {
    events.push_back({ WHERE, 0, stream_handle->expect_stream_data(seq, payloads[seq]) });
  }

  // Peer: next expected seq 0, holding [1, 3) -> it missed seq 0. It must be resent.
  // (Time advances past SCU_UDP_RESEND_INTERVAL so the packet is no longer "in flight".)
  events.push_back({ WHERE, SCU_UDP_RESEND_INTERVAL,
      stream_handle->inject_heartbeat(/*initial_end=*/ 0, /*held_ranges=*/ { { 1u, 3u } }) });
  events.push_back({ WHERE, 0, stream_handle->expect_stream_data(0, payloads[0]) });
  close_connection(events, connection_handle);
  execute_test(events);
}

// Self-heal: if the peer stays stuck requesting a packet that has fallen out of resend
// history (the "already out of resend history" warning), it can loop forever on a
// reliable-ordered stream. After SCU_UDP_TIMEOUT of the SAME unrecoverable seq, the stream
// must panic so the bridge can rebuild it -- but a single/transient occurrence must not.
// (Relies on the shrunk SCU_UDP_QOS_DEPTH_SAFETY_BUFFER=8 for this target so the
// out-of-history boundary is reachable in a handful of packets.)
TEST_F(ScorpioUdpTester, reliable_stream_panics_when_peer_stuck_out_of_history) {
  std::vector<EventQueueItem> events;
  auto connection_handle = create_connection(events);
  constexpr uint16_t kDepth = 1;  // buffer = depth + SAFETY_BUFFER(8) = 9
  auto stream_handle = create_outgoing_reliable_stream(events, connection_handle, 1, kDepth);
  events.push_back({ WHERE, 0, std::make_unique<DrainSendQueueEvent>() });

  // Push the sender past the buffer while ACKing so the send path never overflows:
  // send seq 0..4, ACK up to 5, send seq 5..10 -> sequence_number = 11 (seq 1 + buffer 9 < 11).
  auto send_range = [&](SeqNumber from, SeqNumber to) {
      for (SeqNumber seq = from; seq < to; ++seq) {
        events.push_back({ WHERE, 0, stream_handle->stream_send({ static_cast<uint8_t>(seq) }) });
      }
      events.push_back({ WHERE, 0, std::make_unique<DrainSendQueueEvent>() });
    };
  send_range(0, 5);
  events.push_back({ WHERE, 0, stream_handle->inject_heartbeat(/*initial_end=*/ 5) });
  // Let the connection thread process the ACK (advance least-non-delivered) before sending
  // the next batch, otherwise the send guard would see an un-acked backlog and overflow.
  events.push_back({ WHERE, 0, std::make_unique<AdvanceTimeEvent>(TICK_TIME * 2) });
  send_range(5, 11);

  // Peer (falsely) reports missing seq 1 while holding [2, 11). seq 1 is out of history
  // (current seq 11, buffer 9), so it can never be resent -> the unrecoverable stuck case.
  // A fresh event each time (the heartbeat is rebuilt with the captured epoch at execute time).
  const auto stuck_hb = [&]() {
      return stream_handle->inject_heartbeat(/*initial_end=*/ 1, /*held_ranges=*/ { { 2u, 11u } });
    };

  // First occurrence: warns and starts the timer, but must NOT panic yet.
  events.push_back({ WHERE, 0, stuck_hb() });
  events.push_back({ WHERE, 0, stream_handle->stream_is_panic(false) });

  // Re-send the same NACK every SCU_UDP_TIMEOUT/2 so the connection's own 5s silence
  // timeout never trips; after the total exceeds SCU_UDP_TIMEOUT the stream must panic.
  events.push_back({ WHERE, 0, std::make_unique<AdvanceTimeEvent>(SCU_UDP_TIMEOUT / 2) });
  events.push_back({ WHERE, 0, stuck_hb() });
  events.push_back({ WHERE, 0, stream_handle->stream_is_panic(false) });  // ~2.5s: still tolerated
  for (int i = 0; i < 2; ++i) {
    events.push_back({ WHERE, 0, std::make_unique<AdvanceTimeEvent>(SCU_UDP_TIMEOUT / 2) });
    events.push_back({ WHERE, 0, stuck_hb() });
  }
  events.push_back({ WHERE, 0, stream_handle->stream_is_panic(true) });
  // Tolerant teardown (a panicked stream still emits stray heartbeats); assert close succeeds
  // rather than the exact DISCONNECT handshake packets.
  events.push_back({ WHERE, 0, connection_handle->close_connection(true) });
  execute_test(events);
}

// Regression: production logs showed the peer cycling between several DIFFERENT
// out-of-history seqs across heartbeats (0, 5, 6, 12, 20, 22, ...) and the stream never
// panicking. Root cause: a heartbeat can report MULTIPLE simultaneous out-of-history holes
// (a genuinely stuck connection loses more than one packet at a time), but the old tracker
// only ever remembered the FIRST (lowest) one it scanned that pass and silently dropped the
// rest. When that front hole eventually resolved and a later hole became the new "first
// encountered", the old code treated it as a brand-new occurrence and reset the timer to
// `now` -- even though that later hole had already been sitting unresolved, unreported,
// since the very first pass. Construct exactly that: two simultaneous holes (seq 1 and
// seq 3) in the first heartbeat; seq 1 then "resolves" (peer's ack cursor advances past it)
// while seq 3 remains stuck forever. The panic must fire SCU_UDP_TIMEOUT after seq 3 was
// FIRST observed (pass 1), not SCU_UDP_TIMEOUT after it became the reported front (pass 2).
TEST_F(ScorpioUdpTester, reliable_stream_panics_using_first_observed_time_of_stuck_seq) {
  std::vector<EventQueueItem> events;
  auto connection_handle = create_connection(events);
  constexpr uint16_t kDepth = 1;  // buffer = depth + SAFETY_BUFFER(8) = 9
  auto stream_handle = create_outgoing_reliable_stream(events, connection_handle, 1, kDepth);
  events.push_back({ WHERE, 0, std::make_unique<DrainSendQueueEvent>() });

  auto send_range = [&](SeqNumber from, SeqNumber to) {
      for (SeqNumber seq = from; seq < to; ++seq) {
        events.push_back({ WHERE, 0, stream_handle->stream_send({ static_cast<uint8_t>(seq) }) });
      }
      events.push_back({ WHERE, 0, std::make_unique<DrainSendQueueEvent>() });
    };
  send_range(0, 5);
  events.push_back({ WHERE, 0, stream_handle->inject_heartbeat(/*initial_end=*/ 5) });
  // Let the connection thread process the ACK (advance least-non-delivered) before sending
  // the next batch, otherwise the send guard would see an un-acked backlog and overflow.
  events.push_back({ WHERE, 0, std::make_unique<AdvanceTimeEvent>(TICK_TIME * 2) });
  send_range(5, 13);
  // sequence_number is now 13, history size is 9 -> seqs 0..3 are out of history
  // (i + 9 < 13). Seqs 1 and 3 are both unrecoverable holes below.

  // Pass 1: peer holds [2,3) and [4,13) -> missing seq 1 AND seq 3, both out of history.
  // Both get tracked with first-seen time "now" (t=0).
  events.push_back({ WHERE, 0,
      stream_handle->inject_heartbeat(/*initial_end=*/ 1, /*held_ranges=*/ { { 2u, 3u }, { 4u, 13u } }) });
  events.push_back({ WHERE, 0, stream_handle->stream_is_panic(false) });

  // Pass 2, at t=T/2: seq 1 has since "arrived" (peer's ack cursor advances to 3), pruning
  // its entry. Seq 3 is still missing -> it is now the sole/"first encountered" hole, but its
  // tracked first-seen time must still be t=0 from pass 1, not reset to t=T/2.
  events.push_back({ WHERE, 0, std::make_unique<AdvanceTimeEvent>(SCU_UDP_TIMEOUT / 2) });
  events.push_back({ WHERE, 0, stream_handle->inject_heartbeat(/*initial_end=*/ 3, /*held_ranges=*/ { { 4u, 13u } }) });
  events.push_back({ WHERE, 0, stream_handle->stream_is_panic(false) });  // ~T/2 since seq 3 first seen: tolerated

  // Pass 3, at t=T: seq 3 is still missing. Elapsed since its TRUE first-seen time (pass 1,
  // t=0) is now T -> must panic. (A single-slot tracker that reset at pass 2 would only be
  // at T/2 since its own reset and would wrongly stay quiet.)
  events.push_back({ WHERE, 0, std::make_unique<AdvanceTimeEvent>(SCU_UDP_TIMEOUT / 2) });
  events.push_back({ WHERE, 0, stream_handle->inject_heartbeat(/*initial_end=*/ 3, /*held_ranges=*/ { { 4u, 13u } }) });
  events.push_back({ WHERE, 0, stream_handle->stream_is_panic(true) });
  // Tolerant teardown (a panicked stream still emits stray heartbeats); assert close succeeds
  // rather than the exact DISCONNECT handshake packets.
  events.push_back({ WHERE, 0, connection_handle->close_connection(true) });
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
  close_connection(events, connection_handle);
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
  close_connection(events, connection_handle);
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
  close_connection(events, connection_handle);
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
    stream_data_packet(1, 0, payload_a, TEST_STREAM_EPOCH)) });
  events.push_back({ WHERE, 0, std::make_unique<SendPacket>(Ipv4(127, 0, 0, 1), 12345,
    stream_data_packet(2, 0, payload_b, TEST_STREAM_EPOCH)) });

  // Each stream gets its own payload.
  events.push_back({ WHERE, 0, stream_a->stream_receive(payload_a) });
  events.push_back({ WHERE, 0, stream_b->stream_receive(payload_b) });
  // And no cross-talk.
  events.push_back({ WHERE, 0, stream_a->stream_expect_no_receive() });
  events.push_back({ WHERE, 0, stream_b->stream_expect_no_receive() });

  close_connection(events, connection_handle);
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
    stream_data_packet(1, 0, rel_payload, TEST_STREAM_EPOCH)) });
  events.push_back({ WHERE, 0, std::make_unique<SendPacket>(Ipv4(127, 0, 0, 1), 12345,
    stream_data_packet(2, 0, unrel_payload)) });
  events.push_back({ WHERE, 0, rel_stream->stream_receive(rel_payload) });
  events.push_back({ WHERE, 0, unrel_stream->stream_receive(unrel_payload) });

  close_connection(events, connection_handle);
  execute_test(events);
}

// =====================================================================
// Suite 7 — heartbeat edge cases
// =====================================================================

TEST_F(ScorpioUdpTester, heartbeat_nonexistent_stream_sends_already_closed) {
  std::vector<EventQueueItem> events;
  auto connection_handle = create_connection(events);
  events.push_back({ WHERE, 0, std::make_unique<DrainSendQueueEvent>() });
  const auto hb_body = generate_heartbeat_body(99, /*initial_end=*/ 0);
  events.push_back({ WHERE, 0, std::make_unique<SendPacket>(Ipv4(127, 0, 0, 1), 12345,
    generate_single_packet(Code::HEARTBEAT, heartbeat_payload(hb_body))) });
  // The ALREADY_CLOSED must echo the epoch from the heartbeat block (here the
  // generate_heartbeat_body default, TEST_STREAM_EPOCH): the reply is only useful
  // if it matches the peer's live stream incarnation. Exact match on purpose -
  // it used to carry the local random _streams_epoch[99] slot, which the peer
  // (correctly) ignored, leaving the stale stream to a 5 s timeout.
  events.push_back({ WHERE, 0, std::make_unique<ExpectPacketAnySeqTimeout>(Ipv4(127, 0, 0, 1), 12345,
    generate_single_packet(Code::CLOSE_STREAM,
      close_stream_payload(99, Code::CloseStreamSubCommands::ALREADY_CLOSED, TEST_STREAM_EPOCH), 0, 99)) });
  close_connection(events, connection_handle);
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
    events.push_back({ WHERE, 0, stream_a->expect_stream_data(seq, payloads_a[seq]) });
  }
  for (SeqNumber seq = 0; seq < 3; ++seq) {
    events.push_back({ WHERE, 0, stream_b->expect_stream_data(seq, payloads_b[seq]) });
  }

  // One heartbeat carries three stream entries:
  // stream 99 : nonexistent (0 ranges)    -> must elicit CLOSE_STREAM ALREADY_CLOSED
  // stream 2  : initial_end=1, held=[2,3) -> peer received seq 0 and seq 2 but missed seq 1
  // stream 5  : initial_end=1, held=[2,3) -> same gap as stream 2, retransmit seq 1
  // heartbeat_packet_handler processes entries in order. The unknown-stream block
  // sits FIRST so the emitted packet order stays deterministic: the ALREADY_CLOSED
  // travels through the control-priority send queue and would overtake data
  // retransmits enqueued before it.
  // Streams 2 and 5 are local (random captured epochs, resolved at execute time);
  // stream 99 is a verbatim nonexistent-stream block.
  // (Time advances past SCU_UDP_RESEND_INTERVAL so the packets are no longer "in flight".)
  events.push_back({ WHERE, SCU_UDP_RESEND_INTERVAL, StreamHandle::inject_multi_heartbeat({
      StreamHandle::hb_raw(generate_heartbeat_body(99, /*initial_end=*/ 0)),
      StreamHandle::hb_entry(stream_a, /*initial_end=*/ 1, /*held_ranges=*/ { { 2u, 3u } }),
      StreamHandle::hb_entry(stream_b, /*initial_end=*/ 1, /*held_ranges=*/ { { 2u, 3u } }),
    }) });

  // The ALREADY_CLOSED for the unknown stream 99 must echo the epoch from the
  // heartbeat block (generate_heartbeat_body default = TEST_STREAM_EPOCH).
  events.push_back({ WHERE, 0, std::make_unique<ExpectPacketAnySeqTimeout>(Ipv4(127, 0, 0, 1), 12345,
    generate_single_packet(Code::CLOSE_STREAM,
      close_stream_payload(99, Code::CloseStreamSubCommands::ALREADY_CLOSED, TEST_STREAM_EPOCH), 0, 99)) });
  events.push_back({ WHERE, 0, stream_a->expect_stream_data(1, payloads_a[1]) });
  events.push_back({ WHERE, 0, stream_b->expect_stream_data(1, payloads_b[1]) });

  close_connection(events, connection_handle);
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
  close_connection(events, connection_handle);
  execute_test(events);
}

// =====================================================================
// Suite 8 — regression tests and additional coverage
//
// Tests in this suite were originally written as bug reports for
// src/network/scorpio_udp.cpp. All bugs have since been fixed; these
// tests now serve as regression coverage.
// =====================================================================

// Regression for bug 1: handle_connect_packet's ALREADY_CONNECTED branch
// (scorpio_udp.cpp:450-457) was not transitioning to CONNECTED, leaving
// the connection in its CONNECTING-retransmit loop. Fix: treat peer's
// ALREADY_CONNECTED as confirmation, transition to CONNECTED, and start
// the heartbeat loop. Test asserts a HEARTBEAT is observed.
TEST_F(ScorpioUdpTester, connect_handles_already_connected_response) {
  auto connection_handle = ConnectionHandle::create();
  std::vector<EventQueueItem> events;
  events.push_back({ WHERE, 0, std::make_unique<StartScorpioUdp>() });
  events.push_back({ WHERE, 0, connection_handle->create_connection(Ipv4(127, 0, 0, 1), 12345) });
  events.push_back({ WHERE, 0, connection_handle->expect_connect(Code::ConnectSubCommands::CONNECT) });
  events.push_back({ WHERE, 0, connection_handle->send_connect(Code::ConnectSubCommands::ALREADY_CONNECTED) });
  // expect_heartbeat silently drains the CONNECT retransmits via the
  // is_background_packet filter, so this only succeeds if a HEARTBEAT is
  // actually emitted (i.e., we reached State::CONNECTED). The connection is
  // locally-initiated, so its id is random and resolved at execute time.
  events.push_back({ WHERE, 0, connection_handle->expect_heartbeat(TICK_TIME, 30) });
  events.push_back({ WHERE, 0, connection_handle->close_connection(true) });
  execute_test(events);
}

// Regression for bug 2: handle_connect_packet's CREATE_STREAM:REJECT handler
// (scorpio_udp.cpp:832-866) was not transitioning the local stream out of
// CREATING, causing repeated CREATE_STREAM:CREATE retransmits until the
// retry timeout. Fix: transition to REJECTED immediately on REJECT response.
// Test asserts no CREATE_STREAM:CREATE follows the REJECT.
TEST_F(ScorpioUdpTester, outgoing_stream_rejected_transitions_out_of_creating) {
  std::vector<EventQueueItem> events;
  auto connection_handle = create_connection(events);
  auto stream_handle = StreamHandle::create(connection_handle);
  const ScorpioUdpStream::StreamQoS qos{
    16, ScorpioUdpStream::StreamQoS::Reliability::RELIABLE_ORDERED };
  events.push_back({ WHERE, 0, stream_handle->create_stream(1, qos) });
  events.push_back({ WHERE, 0, stream_handle->expect_create() });
  events.push_back({ WHERE, 0, stream_handle->send_create_response(Code::CreateStreamSubCommands::REJECT) });
  // Wait for the stream to leave CREATING. is_alive() flips to false only when the
  // REJECT handler has stored State::REJECTED; advancing time gives the connection's
  // processing_thread ticks to drain _incoming_packets. stream_is_active(false) is
  // unusable here because CREATING is already not "active".
  events.push_back({ WHERE, 0, stream_handle->stream_is_alive(false) });
  // Drop any CREATE retransmits that landed before the REJECT was processed (the
  // tick-driven heartbeat can race with packet propagation through receiver_thread →
  // _receiver_channel → _incoming_packets).
  events.push_back({ WHERE, 0, std::make_unique<DrainSendQueueEvent>() });
  // After the state transition, no further CREATE_STREAM:CREATE retransmits may occur.
  events.push_back({ WHERE, 0, std::make_unique<ExpectNoPacket>(TICK_TIME, 10,
  ExpectNoPacket::Filter::CREATE_STREAM_CREATE_ONLY) });
  close_connection(events, connection_handle);
  execute_test(events);
}

// Coverage: symmetric to reliable_stream_send_fragmented_emits_multiple_
// packets. Peer sends a multi-fragment reliable STREAM_DATA in order; the
// orderer + partial_data path should reassemble (scorpio_udp.cpp:1395-1423).
TEST_F(ScorpioUdpTester, reliable_stream_fragmented_receive_in_order) {
  std::vector<EventQueueItem> events;
  auto connection_handle = create_connection(events);
  auto stream_handle = establish_incoming_reliable_stream(events, connection_handle, 1, 64);
  std::vector<uint8_t> payload;
  payload.reserve(1400);
  for (size_t i = 0; i < 1400; ++i) {
    payload.push_back(static_cast<uint8_t>(i & 0xff));
  }
  auto packets = generate_all_packets(Code::STREAM_DATA, payload, 0, 1, TEST_STREAM_EPOCH);
  ASSERT_GE(packets.size(), 3u) << "1400-byte reliable payload should fragment into >=3 packets";
  for (auto& p : packets) {
    events.push_back({ WHERE, 0, std::make_unique<SendPacket>(Ipv4(127, 0, 0, 1), 12345, p) });
  }
  events.push_back({ WHERE, 0, stream_handle->stream_receive(payload) });
  close_connection(events, connection_handle);
  execute_test(events);
}

// Coverage: same as above but fragments injected out of order. The orderer
// at scorpio_udp.cpp:1399 should buffer until the missing seq arrives, then
// drain in order via _orderer.next().
TEST_F(ScorpioUdpTester, reliable_stream_fragmented_receive_out_of_order) {
  std::vector<EventQueueItem> events;
  auto connection_handle = create_connection(events);
  auto stream_handle = establish_incoming_reliable_stream(events, connection_handle, 1, 64);
  std::vector<uint8_t> payload;
  payload.reserve(1400);
  for (size_t i = 0; i < 1400; ++i) {
    payload.push_back(static_cast<uint8_t>((i * 31 + 7) & 0xff));
  }
  auto packets = generate_all_packets(Code::STREAM_DATA, payload, 0, 1, TEST_STREAM_EPOCH);
  ASSERT_GE(packets.size(), 3u);
  // Inject in order N-1, N-2, ..., 0 — reverse, fully out-of-order.
  for (size_t i = packets.size(); i-- > 0; ) {
    events.push_back({ WHERE, 0, std::make_unique<SendPacket>(Ipv4(127, 0, 0, 1), 12345, packets[i]) });
  }
  events.push_back({ WHERE, 0, stream_handle->stream_receive(payload) });
  close_connection(events, connection_handle);
  execute_test(events);
}

// Coverage: two fragmented messages back-to-back. partial_data must reset
// between them; the panic guard at scorpio_udp.cpp:1410-1413 should not fire.
TEST_F(ScorpioUdpTester, reliable_stream_two_consecutive_fragmented_messages) {
  std::vector<EventQueueItem> events;
  auto connection_handle = create_connection(events);
  auto stream_handle = establish_incoming_reliable_stream(events, connection_handle, 1, 64);
  std::vector<uint8_t> payload_a(1400, 0xAA);
  std::vector<uint8_t> payload_b(1200, 0xBB);
  auto packets_a = generate_all_packets(Code::STREAM_DATA, payload_a, 0, 1, TEST_STREAM_EPOCH);
  ASSERT_GT(packets_a.size(), 1u);
  auto packets_b = generate_all_packets(
    Code::STREAM_DATA, payload_b, static_cast<SeqNumber>(packets_a.size()), 1, TEST_STREAM_EPOCH);
  ASSERT_GT(packets_b.size(), 1u);
  for (auto& p : packets_a) {
    events.push_back({ WHERE, 0, std::make_unique<SendPacket>(Ipv4(127, 0, 0, 1), 12345, p) });
  }
  for (auto& p : packets_b) {
    events.push_back({ WHERE, 0, std::make_unique<SendPacket>(Ipv4(127, 0, 0, 1), 12345, p) });
  }
  events.push_back({ WHERE, 0, stream_handle->stream_receive(payload_a) });
  events.push_back({ WHERE, 0, stream_handle->stream_receive(payload_b) });
  events.push_back({ WHERE, 0, stream_handle->stream_is_panic(false) });
  close_connection(events, connection_handle);
  execute_test(events);
}

// Coverage: orderer window enforcement. With depth=16 the orderer size is
// 16 + SCU_UDP_QOS_DEPTH_SAFETY_BUFFER = 2064. A seq beyond that window
// should trigger OrdererAddResult::TOO_NEW and panic the stream
// (scorpio_udp.cpp:1399-1402).
TEST_F(ScorpioUdpTester, reliable_stream_too_new_seq_panics_stream) {
  std::vector<EventQueueItem> events;
  auto connection_handle = create_connection(events);
  auto stream_handle = establish_incoming_reliable_stream(events, connection_handle, 1, 16);
  events.push_back({ WHERE, 0, std::make_unique<SendPacket>(Ipv4(127, 0, 0, 1), 12345,
    stream_data_packet(1, 3000, { 0x01, 0x02 }, TEST_STREAM_EPOCH)) });
  events.push_back({ WHERE, 0, stream_handle->stream_is_panic(true) });
  close_connection(events, connection_handle);
  execute_test(events);
}

// Coverage: unreliable multi-fragment assembly with internally-inconsistent
// frames_left. The check at scorpio_udp.cpp:1481-1487 panics when a
// follow-on fragment's frames_left does not decrement by 1. We craft a
// 3-fragment unreliable STREAM_DATA and mutate fragment[1]'s frames_left
// from 1 to 2 (matching fragment[0]'s value) to trigger the panic.
TEST_F(ScorpioUdpTester, unreliable_stream_inconsistent_frames_left_panics) {
  std::vector<EventQueueItem> events;
  auto connection_handle = create_connection(events);
  auto stream_handle = establish_incoming_unreliable_stream(events, connection_handle, 2);
  std::vector<uint8_t> payload(1400, 0x77);
  auto packets = generate_all_packets(Code::STREAM_DATA, payload, 0, 2);
  ASSERT_EQ(packets.size(), 3u);
  // STREAM_DATA fragment wire layout: code(1) + stream_num(2) + seq(4) +
  // frames_left(2) when NOT_LAST is set. frames_left lives at bytes [7,8].
  ASSERT_GE(packets[1].size(), 9u);
  packets[1][7] = 0x00;
  packets[1][8] = 0x02;  // claim 2 frames left; orderer expects 1
  events.push_back({ WHERE, 0, std::make_unique<SendPacket>(Ipv4(127, 0, 0, 1), 12345, packets[0]) });
  events.push_back({ WHERE, 0, std::make_unique<SendPacket>(Ipv4(127, 0, 0, 1), 12345, packets[1]) });
  events.push_back({ WHERE, 0, stream_handle->stream_is_panic(true) });
  close_connection(events, connection_handle);
  execute_test(events);
}

// Coverage: ScorpioUdpStream::send guard at scorpio_udp.cpp:1252 rejects
// any non-CLOSE_STREAM send once the stream has been locally closed.
// We deliberately skip close_connection: while in CLOSING state, update()
// (scorpio_udp.cpp:1372-1374) retransmits CLOSE_STREAM on every heartbeat,
// and a clean DISCONNECT handshake would race those retransmits. TearDown
// flushes everything via a big time advance + socket close.
TEST_F(ScorpioUdpTester, stream_send_after_close_returns_false) {
  std::vector<EventQueueItem> events;
  auto connection_handle = create_connection(events);
  auto stream_handle = establish_incoming_reliable_stream(events, connection_handle, 1, 16);
  events.push_back({ WHERE, 0, std::make_unique<DrainSendQueueEvent>() });
  events.push_back({ WHERE, 0, stream_handle->close_stream(true) });
  // Locally-initiated CLOSE carries the stream epoch (send_close_packet); this
  // peer-created stream's epoch is the known TEST_STREAM_EPOCH.
  events.push_back({ WHERE, 0, std::make_unique<ExpectPacketAnySeqTimeout>(Ipv4(127, 0, 0, 1), 12345,
    generate_single_packet(Code::CLOSE_STREAM,
      close_stream_payload(1, Code::CloseStreamSubCommands::CLOSE, TEST_STREAM_EPOCH), 0, 1)) });
  events.push_back({ WHERE, 0, stream_handle->stream_send({ 0x01, 0x02, 0x03 }, /*expect_success=*/ false) });
  execute_test(events);
}

// Regression for bug 4: heartbeat_packet_handler read data[pos++] without
// bounds-checking when a stream in the heartbeat was not known locally
// (scorpio_udp.cpp:937-950). Fix: explicit pos < data.size() check.
// Test feeds a truncated heartbeat body and asserts no crash/panic.
TEST_F(ScorpioUdpTester, heartbeat_unknown_stream_truncated_does_not_crash) {
  std::vector<EventQueueItem> events;
  auto connection_handle = create_connection(events);
  events.push_back({ WHERE, 0, std::make_unique<DrainSendQueueEvent>() });
  // Body is a stream_number + epoch with no ranges/initial_end. Stream 99 is
  // not known locally, so the handler enters the "unknown stream" branch and
  // must bounds-check before reading the (missing) ranges byte.
  std::vector<uint8_t> body;
  write_be16(body, static_cast<uint16_t>(99));
  body.push_back(static_cast<uint8_t>(TEST_STREAM_EPOCH));
  events.push_back({ WHERE, 0, std::make_unique<SendPacket>(Ipv4(127, 0, 0, 1), 12345,
    generate_single_packet(Code::HEARTBEAT, heartbeat_payload(body))) });
  events.push_back({ WHERE, 0, connection_handle->connection_is_alive(true) });
  events.push_back({ WHERE, 0, connection_handle->connection_is_panic(false) });
  close_connection(events, connection_handle);
  execute_test(events);
}

// Coverage: STREAM_DATA for a stream the connection has not created or
// accepted is dropped with a warning (handle_new_packet:STREAM_DATA at
// scorpio_udp.cpp:958-965); the connection itself must survive intact.
TEST_F(ScorpioUdpTester, incoming_stream_data_before_create_is_dropped) {
  std::vector<EventQueueItem> events;
  auto connection_handle = create_connection(events);
  events.push_back({ WHERE, 0, std::make_unique<DrainSendQueueEvent>() });
  events.push_back({ WHERE, 0, std::make_unique<SendPacket>(Ipv4(127, 0, 0, 1), 12345,
    stream_data_packet(7, 0, { 0x01, 0x02, 0x03 })) });
  events.push_back({ WHERE, 0, connection_handle->connection_is_alive(true) });
  events.push_back({ WHERE, 0, connection_handle->connection_is_panic(false) });
  // We should not emit any STREAM_DATA in response.
  events.push_back({ WHERE, 0, std::make_unique<ExpectNoPacket>(TICK_TIME, 5,
    ExpectNoPacket::Filter::STREAM_DATA_ONLY) });
  close_connection(events, connection_handle);
  execute_test(events);
}

// Coverage: second peer CLOSE for the same stream id, after the stream has
// already been closed by an earlier round-trip, must produce an
// ALREADY_CLOSED response (close_stream_packet_handler CLOSE branch at
// scorpio_udp.cpp:889-915).
TEST_F(ScorpioUdpTester, peer_close_stream_idempotent_already_closed) {
  std::vector<EventQueueItem> events;
  auto connection_handle = create_connection(events);
  events.push_back({ WHERE, 0, connection_handle->connection_auto_accept_streams(true) });
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
  // First close round-trip.
  events.push_back({ WHERE, 0, std::make_unique<SendPacket>(Ipv4(127, 0, 0, 1), 12345,
    generate_single_packet(Code::CLOSE_STREAM,
      close_stream_payload(1, Code::CloseStreamSubCommands::CLOSE), 0, 1)) });
  events.push_back({ WHERE, 0, std::make_unique<ExpectPacketAnySeqTimeout>(Ipv4(127, 0, 0, 1), 12345,
    generate_single_packet(Code::CLOSE_STREAM,
      close_stream_payload(1, Code::CloseStreamSubCommands::CLOSED), 0, 1)) });
  events.push_back({ WHERE, 0, stream_handle->stream_is_active(false) });
  // Second CLOSE for the same stream id — must be answered with ALREADY_CLOSED.
  events.push_back({ WHERE, 0, std::make_unique<SendPacket>(Ipv4(127, 0, 0, 1), 12345,
    generate_single_packet(Code::CLOSE_STREAM,
      close_stream_payload(1, Code::CloseStreamSubCommands::CLOSE), 0, 1)) });
  events.push_back({ WHERE, 0, std::make_unique<ExpectPacketAnySeqTimeout>(Ipv4(127, 0, 0, 1), 12345,
    generate_single_packet(Code::CLOSE_STREAM,
      close_stream_payload(1, Code::CloseStreamSubCommands::ALREADY_CLOSED), 0, 1)) });
  close_connection(events, connection_handle);
  execute_test(events);
}

// Coverage: a bare HEARTBEAT (with no per-stream entries) updates
// _last_received_packet_time (scorpio_udp.cpp:1006) and therefore staves
// off the SCU_UDP_TIMEOUT panic the way an active peer is meant to.
// Mirrors connection_panics_on_peer_silence which asserts the opposite
// when no traffic arrives.
TEST_F(ScorpioUdpTester, bare_heartbeat_keeps_connection_alive) {
  std::vector<EventQueueItem> events;
  auto connection_handle = create_connection(events);
  events.push_back({ WHERE, 0, std::make_unique<DrainSendQueueEvent>() });
  // Move close to the timeout boundary, but stay under it.
  events.push_back({ WHERE, 0, std::make_unique<AdvanceTimeEvent>(SCU_UDP_TIMEOUT * 4 / 5) });
  events.push_back({ WHERE, 0, std::make_unique<SendPacket>(Ipv4(127, 0, 0, 1), 12345,
    generate_single_packet(Code::HEARTBEAT, heartbeat_payload())) });
  // Give the processing thread a tick to consume the heartbeat.
  events.push_back({ WHERE, TICK_TIME, std::make_unique<NoOpEvent>() });
  // Advance past where panic would have fired without the heartbeat reset.
  events.push_back({ WHERE, 0, std::make_unique<AdvanceTimeEvent>(SCU_UDP_TIMEOUT * 4 / 5) });
  events.push_back({ WHERE, 0, connection_handle->connection_is_panic(false) });
  events.push_back({ WHERE, 0, connection_handle->connection_is_alive(true) });
  close_connection(events, connection_handle);
  execute_test(events);
}

// =====================================================================
// Suite 9 — regression tests and additional coverage.
// Bug numbers continue from Suite 8; all fixed.
// =====================================================================

// Regression for bug 5: heartbeat_packet_handler delegated to
// handle_heartbeat_data but did not advance `pos` for unreliable streams
// (scorpio_udp.cpp:1589-1591), causing the outer loop to misread the next
// stream_num and emit a spurious CLOSE_STREAM. Fix: always advance pos
// after each stream entry. Test asserts no CLOSE_STREAM is emitted.
TEST_F(ScorpioUdpTester, heartbeat_for_unreliable_stream_does_not_emit_spurious_close) {
  std::vector<EventQueueItem> events;
  auto connection_handle = create_connection(events);
  auto stream_handle = establish_incoming_unreliable_stream(events, connection_handle, 5);
  events.push_back({ WHERE, 0, std::make_unique<DrainSendQueueEvent>() });
  auto hb_body = generate_heartbeat_body(5, /*initial_end=*/ 0, /*held_ranges=*/ { });
  events.push_back({ WHERE, 0, std::make_unique<SendPacket>(Ipv4(127, 0, 0, 1), 12345,
    generate_single_packet(Code::HEARTBEAT, heartbeat_payload(hb_body))) });
  events.push_back({ WHERE, 0, std::make_unique<ExpectNoPacket>(TICK_TIME, 10,
    ExpectNoPacket::Filter::CLOSE_STREAM_ONLY) });
  close_connection(events, connection_handle);
  execute_test(events);
}

// Regression for bug 5 in a multi-entry heartbeat: reliable then unreliable
// stream. The misaligned pos after the unreliable entry caused a spurious
// CLOSE_STREAM. Test asserts no CLOSE_STREAM follows the correct retransmit.
TEST_F(ScorpioUdpTester, heartbeat_mixed_reliable_unreliable_does_not_corrupt_parsing) {
  std::vector<EventQueueItem> events;
  auto connection_handle = create_connection(events);
  events.push_back({ WHERE, 0, connection_handle->connection_auto_accept_streams(true) });
  // Reliable outgoing stream id=1 so we own the _sent_history and respond
  // to gap NACKs with STREAM_DATA retransmits.
  auto reliable_stream = create_outgoing_reliable_stream(events, connection_handle, 1, 16);
  events.push_back({ WHERE, 0, std::make_unique<DrainSendQueueEvent>() });
  // Unreliable incoming stream id=5.
  const ScorpioUdpStream::StreamQoS unreliable_qos{
    0, ScorpioUdpStream::StreamQoS::Reliability::UNRELIABLE };
  events.push_back({ WHERE, 0, std::make_unique<SendPacket>(Ipv4(127, 0, 0, 1), 12345,
    generate_single_packet(Code::CREATE_STREAM,
      create_stream_payload(5, unreliable_qos, Code::CreateStreamSubCommands::CREATE))) });
  events.push_back({ WHERE, 0, std::make_unique<ExpectPacketTimeout>(Ipv4(127, 0, 0, 1), 12345,
    generate_single_packet(Code::CREATE_STREAM,
      create_stream_payload(5, unreliable_qos, Code::CreateStreamSubCommands::ACCEPT))) });
  auto unreliable_stream = StreamHandle::create(connection_handle);
  events.push_back({ WHERE, 0, unreliable_stream->get_stream(true) });
  events.push_back({ WHERE, 0, std::make_unique<DrainSendQueueEvent>() });
  // Send three messages on the reliable stream so the sender has seq 0,1,2 in history.
  std::vector<std::vector<uint8_t>> payloads = {
    { 0x01, 0x01 }, { 0x02, 0x02 }, { 0x03, 0x03 },
  };
  for (auto& p : payloads) {
    events.push_back({ WHERE, 0, reliable_stream->stream_send(p) });
  }
  for (SeqNumber seq = 0; seq < 3; ++seq) {
    events.push_back({ WHERE, 0, reliable_stream->expect_stream_data(seq, payloads[seq]) });
  }
  // Peer heartbeat: stream 1 reliable (gap -> resend seq 1, local random epoch resolved
  // at execute time), then stream 5 unreliable (peer stream, known TEST_STREAM_EPOCH).
  // (Time advances past SCU_UDP_RESEND_INTERVAL so the packets are no longer "in flight".)
  events.push_back({ WHERE, SCU_UDP_RESEND_INTERVAL, StreamHandle::inject_multi_heartbeat({
      StreamHandle::hb_entry(reliable_stream, /*initial_end=*/ 1, /*held_ranges=*/ { { 2u, 3u } }),
      StreamHandle::hb_raw(generate_heartbeat_body(5, /*initial_end=*/ 0, /*held_ranges=*/ { })),
    }) });
  // Sender must retransmit seq 1 on stream 1.
  events.push_back({ WHERE, 0, reliable_stream->expect_stream_data(1, payloads[1]) });
  // No CLOSE_STREAM may follow.
  events.push_back({ WHERE, 0, std::make_unique<ExpectNoPacket>(TICK_TIME, 10,
    ExpectNoPacket::Filter::CLOSE_STREAM_ONLY) });
  close_connection(events, connection_handle);
  execute_test(events);
}

// Regression for bug 12: incoming CREATE_STREAM with unsupported QoS
// (UNRELIABLE_LATEST_ONLY or RELIABLE_UNORDERED) was silently accepted
// (scorpio_udp.cpp:766-769). Fix: respond with REJECT for unsupported modes.
// Test asserts a REJECT is sent.
TEST_F(ScorpioUdpTester, peer_unsupported_qos_create_stream_should_be_rejected) {
  std::vector<EventQueueItem> events;
  auto connection_handle = create_connection(events);
  events.push_back({ WHERE, 0, connection_handle->connection_auto_accept_streams(true) });
  const ScorpioUdpStream::StreamQoS unsupported_qos{
    0, ScorpioUdpStream::StreamQoS::Reliability::UNRELIABLE_LATEST_ONLY };
  events.push_back({ WHERE, 0, std::make_unique<SendPacket>(Ipv4(127, 0, 0, 1), 12345,
    generate_single_packet(Code::CREATE_STREAM,
      create_stream_payload(3, unsupported_qos, Code::CreateStreamSubCommands::CREATE))) });
  events.push_back({ WHERE, 0, std::make_unique<ExpectPacketTimeout>(Ipv4(127, 0, 0, 1), 12345,
    generate_single_packet(Code::CREATE_STREAM,
      create_stream_payload(3, unsupported_qos, Code::CreateStreamSubCommands::REJECT))) });
  close_connection(events, connection_handle);
  execute_test(events);
}

// Coverage: DISCONNECT from an endpoint we have no connection for must
// trigger an ALREADY_DISCONNECTED reply (scorpio_udp.cpp:498-502).
TEST_F(ScorpioUdpTester, disconnect_to_unknown_peer_responds_already_disconnected) {
  std::vector<EventQueueItem> events;
  events.push_back({ WHERE, 0, std::make_unique<StartScorpioUdp>() });
  events.push_back({ WHERE, 0, std::make_unique<SendPacket>(Ipv4(127, 0, 0, 1), 12345,
    generate_single_packet(Code::DISCONNECT,
      id_payload(AS_BYTE(Code::DisconnectSubCommands::DISCONNECT), TEST_PEER_CONNECTION_ID))) });
  events.push_back({ WHERE, 0, std::make_unique<ExpectPacketTimeout>(Ipv4(127, 0, 0, 1), 12345,
    generate_single_packet(Code::DISCONNECT,
      id_payload(AS_BYTE(Code::DisconnectSubCommands::ALREADY_DISCONNECTED), TEST_PEER_CONNECTION_ID))) });
  execute_test(events);
}

// Coverage: PING { PONG } from peer triggers a PING reply with payload
// { 0x01 } per handle_ping_packet at scorpio_udp.cpp:380-382. Locks in
// current intent since the spec does not formally describe PING flow.
TEST_F(ScorpioUdpTester, peer_ping_pong_subcommand_triggers_ping_response) {
  std::vector<EventQueueItem> events;
  events.push_back({ WHERE, 0, std::make_unique<StartScorpioUdp>() });
  events.push_back({ WHERE, 0, std::make_unique<SendPacket>(Ipv4(127, 0, 0, 1), 12345,
    generate_single_packet(Code::PING, { AS_BYTE(Code::PingSubCommands::PONG) })) });
  events.push_back({ WHERE, 0, std::make_unique<ExpectPacketTimeout>(Ipv4(127, 0, 0, 1), 12345,
    generate_single_packet(Code::PING, { AS_BYTE(Code::PingSubCommands::PONG) })) });
  execute_test(events);
}

// Coverage: when peer answers our CREATE_STREAM with ALREADY_EXISTS the
// stream must remain in CREATING (per protocol "stays CREATING
// (idempotent)"). update() keeps retransmitting until SCU_UDP_CREATE_
// RETRY_PERIOD elapses; we only verify the post-ALREADY_EXISTS state.
TEST_F(ScorpioUdpTester, outgoing_stream_already_exists_response_transitions_to_created) {
  std::vector<EventQueueItem> events;
  auto connection_handle = create_connection(events);
  auto stream_handle = StreamHandle::create(connection_handle);
  const ScorpioUdpStream::StreamQoS qos{
    16, ScorpioUdpStream::StreamQoS::Reliability::RELIABLE_ORDERED };
  events.push_back({ WHERE, 0, stream_handle->create_stream(1, qos) });
  events.push_back({ WHERE, 0, stream_handle->expect_create() });
  // ALREADY_EXISTS now echoes stream_number + epoch + QoS (the auto-accept path
  // copies the whole CREATE payload back). With matching epoch and QoS the
  // handler calls connected(), so the stream transitions CREATING -> CREATED.
  events.push_back({ WHERE, 0, stream_handle->send_create_response(Code::CreateStreamSubCommands::ALREADY_EXISTS) });
  events.push_back({ WHERE, 0, stream_handle->stream_is_alive(true) });
  events.push_back({ WHERE, 0, stream_handle->stream_is_active(true) });
  events.push_back({ WHERE, 0, stream_handle->stream_is_panic(false) });
  close_connection(events, connection_handle);
  execute_test(events);
}

// Coverage: HEARTBEAT with two disjoint held ranges -> sender resends
// both gaps. Exercises the range_count > 1 path of handle_heartbeat_
// data at scorpio_udp.cpp:1614-1645.
TEST_F(ScorpioUdpTester, reliable_stream_retransmit_multiple_gaps) {
  std::vector<EventQueueItem> events;
  auto connection_handle = create_connection(events);
  auto stream_handle = create_outgoing_reliable_stream(events, connection_handle, 1, 16);
  events.push_back({ WHERE, 0, std::make_unique<DrainSendQueueEvent>() });
  // Send five messages: seqs 0..4.
  std::vector<std::vector<uint8_t>> payloads = {
    { 0x10 }, { 0x11 }, { 0x12 }, { 0x13 }, { 0x14 },
  };
  for (auto& p : payloads) {
    events.push_back({ WHERE, 0, stream_handle->stream_send(p) });
  }
  for (SeqNumber seq = 0; seq < 5; ++seq) {
    events.push_back({ WHERE, 0, stream_handle->expect_stream_data(seq, payloads[seq]) });
  }
  // Peer received seq 0, 2, 4 (missing 1 and 3). Heartbeat carries
  // initial_end=1, held=[(2,3),(4,5)].
  // (Time advances past SCU_UDP_RESEND_INTERVAL so the packets are no longer "in flight".)
  events.push_back({ WHERE, SCU_UDP_RESEND_INTERVAL, stream_handle->inject_heartbeat(/*initial_end=*/ 1,
      /*held_ranges=*/ { { 2u, 3u }, { 4u, 5u } }) });
  events.push_back({ WHERE, 0, stream_handle->expect_stream_data(1, payloads[1]) });
  events.push_back({ WHERE, 0, stream_handle->expect_stream_data(3, payloads[3]) });
  close_connection(events, connection_handle);
  execute_test(events);
}

// Coverage: two concurrent unreliable streams must not cross-talk. Mirror
// of two_reliable_streams_independent_data for UNRELIABLE QoS.
TEST_F(ScorpioUdpTester, two_unreliable_streams_independent_data) {
  std::vector<EventQueueItem> events;
  auto connection_handle = create_connection(events);
  auto stream_a = establish_incoming_unreliable_stream(events, connection_handle, 1);
  auto stream_b = establish_incoming_unreliable_stream(events, connection_handle, 2);
  std::vector<uint8_t> payload_a{ 0xAA, 0xAA, 0xAA };
  std::vector<uint8_t> payload_b{ 0xBB, 0xBB, 0xBB };
  events.push_back({ WHERE, 0, std::make_unique<SendPacket>(Ipv4(127, 0, 0, 1), 12345,
    stream_data_packet(1, 0, payload_a)) });
  events.push_back({ WHERE, 0, std::make_unique<SendPacket>(Ipv4(127, 0, 0, 1), 12345,
    stream_data_packet(2, 0, payload_b)) });
  events.push_back({ WHERE, 0, stream_a->stream_receive(payload_a) });
  events.push_back({ WHERE, 0, stream_b->stream_receive(payload_b) });
  events.push_back({ WHERE, 0, stream_a->stream_expect_no_receive() });
  events.push_back({ WHERE, 0, stream_b->stream_expect_no_receive() });
  close_connection(events, connection_handle);
  execute_test(events);
}

// Coverage: after peer-initiated CLOSE the stream goes to CLOSED;
// subsequent local send() must return false (is_active guard at
// scorpio_udp.cpp:1257). Complements stream_send_after_close_returns_
// false which tests the locally-initiated close path.
TEST_F(ScorpioUdpTester, reliable_stream_send_after_peer_close_returns_false) {
  std::vector<EventQueueItem> events;
  auto connection_handle = create_connection(events);
  auto stream_handle = establish_incoming_reliable_stream(events, connection_handle, 1, 16);
  events.push_back({ WHERE, 0, std::make_unique<DrainSendQueueEvent>() });
  events.push_back({ WHERE, 0, std::make_unique<SendPacket>(Ipv4(127, 0, 0, 1), 12345,
    generate_single_packet(Code::CLOSE_STREAM,
      close_stream_payload(1, Code::CloseStreamSubCommands::CLOSE), 0, 1)) });
  events.push_back({ WHERE, 0, std::make_unique<ExpectPacketAnySeqTimeout>(Ipv4(127, 0, 0, 1), 12345,
    generate_single_packet(Code::CLOSE_STREAM,
      close_stream_payload(1, Code::CloseStreamSubCommands::CLOSED), 0, 1)) });
  events.push_back({ WHERE, 0, stream_handle->stream_is_active(false) });
  events.push_back({ WHERE, 0, stream_handle->stream_send({ 0x01, 0x02, 0x03 },
      /*expect_success=*/ false) });
  close_connection(events, connection_handle);
  execute_test(events);
}

// Coverage: a packet with an unknown command byte (low nibble = 9) is
// processed by handle_new_packet's default arm at scorpio_udp.cpp:988-990
// which logs and returns. Connection must remain alive and unpaniced and
// nothing should be emitted in response.
TEST_F(ScorpioUdpTester, unknown_command_byte_is_logged_and_ignored) {
  std::vector<EventQueueItem> events;
  auto connection_handle = create_connection(events);
  events.push_back({ WHERE, 0, std::make_unique<DrainSendQueueEvent>() });
  // 0x49 = FIRST | command 9 (no such command).
  events.push_back({ WHERE, 0, std::make_unique<SendPacket>(Ipv4(127, 0, 0, 1), 12345,
    std::vector<uint8_t>{ 0x49 }) });
  events.push_back({ WHERE, 0, connection_handle->connection_is_alive(true) });
  events.push_back({ WHERE, 0, connection_handle->connection_is_panic(false) });
  events.push_back({ WHERE, 0, std::make_unique<ExpectNoPacket>(TICK_TIME, 5,
    ExpectNoPacket::Filter::STREAM_DATA_ONLY) });
  events.push_back({ WHERE, 0, std::make_unique<ExpectNoPacket>(TICK_TIME, 5,
    ExpectNoPacket::Filter::CLOSE_STREAM_ONLY) });
  close_connection(events, connection_handle);
  execute_test(events);
}

// =====================================================================
// Suite 10 — per-stream heartbeat timeout (stream_epoch feature)
//
// A RELIABLE stream that stops receiving heartbeats for SCU_UDP_TIMEOUT moves to
// State::ERROR (ScorpioUdpStream::update, scorpio_udp.cpp:1789); incoming
// heartbeats reset the per-stream clock (handle_heartbeat_data, cpp:2116);
// connected() seeds it on activation (cpp:1740). The timeout is reliable-only.
//
// Pattern: refresh the CONNECTION every SCU_UDP_TIMEOUT/2 so its own 5s silence
// timeout never trips (see reliable_stream_panics_when_peer_stuck_out_of_history),
// while controlling whether the STREAM itself is fed heartbeats.
// =====================================================================

// A reliable stream starved of heartbeats for > SCU_UDP_TIMEOUT must transition
// to ERROR (not alive), while the connection stays healthy. Bare heartbeats keep
// the connection alive but never carry stream 1, so only the per-stream timeout
// can fire here.
TEST_F(ScorpioUdpTester, reliable_stream_errors_on_heartbeat_timeout) {
  std::vector<EventQueueItem> events;
  auto connection_handle = create_connection(events);
  auto stream_handle = establish_incoming_reliable_stream(events, connection_handle, 1, 16);
  events.push_back({ WHERE, 0, std::make_unique<DrainSendQueueEvent>() });
  // 4 * (SCU_UDP_TIMEOUT/2) = 2 * SCU_UDP_TIMEOUT of stream silence; each bare
  // heartbeat resets the connection clock (gap SCU_UDP_TIMEOUT/2 < timeout).
  for (int i = 0; i < 4; ++i) {
    events.push_back({ WHERE, 0, std::make_unique<AdvanceTimeEvent>(SCU_UDP_TIMEOUT / 2) });
    events.push_back({ WHERE, 0, std::make_unique<SendPacket>(Ipv4(127, 0, 0, 1), 12345,
      generate_single_packet(Code::HEARTBEAT, heartbeat_payload())) });
  }
  // Stream timed out -> ERROR (is_alive() == false); connection unaffected.
  events.push_back({ WHERE, 0, stream_handle->stream_is_alive(false) });
  events.push_back({ WHERE, 0, connection_handle->connection_is_panic(false) });
  events.push_back({ WHERE, 0, connection_handle->connection_is_alive(true) });
  events.push_back({ WHERE, 0, connection_handle->close_connection(true) });
  execute_test(events);
}

// A reliable stream that keeps receiving heartbeats stays active well past
// SCU_UDP_TIMEOUT. Regression guard for the _last_heartbeat_time reset in
// handle_heartbeat_data: without it the stream would ERROR at 5s.
TEST_F(ScorpioUdpTester, reliable_stream_heartbeat_refresh_prevents_timeout) {
  std::vector<EventQueueItem> events;
  auto connection_handle = create_connection(events);
  auto stream_handle = establish_incoming_reliable_stream(events, connection_handle, 1, 16);
  events.push_back({ WHERE, 0, std::make_unique<DrainSendQueueEvent>() });
  // Heartbeat that names stream 1 (no holes) -> refreshes its per-stream clock.
  const auto hb = generate_single_packet(Code::HEARTBEAT,
    heartbeat_payload(generate_heartbeat_body(1, /*initial_end=*/ 0)));
  for (int i = 0; i < 6; ++i) {  // 6 * (SCU_UDP_TIMEOUT/2) = 3 * SCU_UDP_TIMEOUT total
    events.push_back({ WHERE, 0, std::make_unique<AdvanceTimeEvent>(SCU_UDP_TIMEOUT / 2) });
    events.push_back({ WHERE, 0, std::make_unique<SendPacket>(Ipv4(127, 0, 0, 1), 12345, hb) });
  }
  events.push_back({ WHERE, 0, stream_handle->stream_is_active(true) });
  events.push_back({ WHERE, 0, connection_handle->connection_is_alive(true) });
  events.push_back({ WHERE, 0, connection_handle->close_connection(true) });
  execute_test(events);
}

// The heartbeat timeout is reliable-only: an unreliable stream starved of
// heartbeats stays active (its update() only expires partial data).
TEST_F(ScorpioUdpTester, unreliable_stream_survives_heartbeat_silence) {
  std::vector<EventQueueItem> events;
  auto connection_handle = create_connection(events);
  auto stream_handle = establish_incoming_unreliable_stream(events, connection_handle, 2);
  events.push_back({ WHERE, 0, std::make_unique<DrainSendQueueEvent>() });
  for (int i = 0; i < 4; ++i) {  // 2 * SCU_UDP_TIMEOUT of stream silence
    events.push_back({ WHERE, 0, std::make_unique<AdvanceTimeEvent>(SCU_UDP_TIMEOUT / 2) });
    events.push_back({ WHERE, 0, std::make_unique<SendPacket>(Ipv4(127, 0, 0, 1), 12345,
      generate_single_packet(Code::HEARTBEAT, heartbeat_payload())) });
  }
  events.push_back({ WHERE, 0, stream_handle->stream_is_active(true) });
  events.push_back({ WHERE, 0, connection_handle->connection_is_alive(true) });
  events.push_back({ WHERE, 0, connection_handle->close_connection(true) });
  execute_test(events);
}

// Guards the moved connection-liveness clock: STREAM_DATA still refreshes
// _last_received_packet_time. Uses an unreliable stream so the per-stream
// heartbeat timeout cannot interfere. (The HEARTBEAT branch is covered by
// bare_heartbeat_keeps_connection_alive; CREATE_STREAM by every establish_*.)
TEST_F(ScorpioUdpTester, connection_kept_alive_by_stream_data_only) {
  std::vector<EventQueueItem> events;
  auto connection_handle = create_connection(events);
  auto stream_handle = establish_incoming_unreliable_stream(events, connection_handle, 2);
  events.push_back({ WHERE, 0, std::make_unique<DrainSendQueueEvent>() });
  for (SeqNumber seq = 0; seq < 4; ++seq) {  // 2 * SCU_UDP_TIMEOUT, refreshed by data only
    std::vector<uint8_t> payload{ static_cast<uint8_t>(0xC0 + seq) };
    events.push_back({ WHERE, 0, std::make_unique<AdvanceTimeEvent>(SCU_UDP_TIMEOUT / 2) });
    events.push_back({ WHERE, 0, std::make_unique<SendPacket>(Ipv4(127, 0, 0, 1), 12345,
      stream_data_packet(2, seq, payload)) });
    events.push_back({ WHERE, 0, stream_handle->stream_receive(payload) });
  }
  events.push_back({ WHERE, 0, connection_handle->connection_is_panic(false) });
  events.push_back({ WHERE, 0, connection_handle->connection_is_alive(true) });
  events.push_back({ WHERE, 0, connection_handle->close_connection(true) });
  execute_test(events);
}

// =====================================================================
// Suite 9 — self-healing under churn, loss and stale state
//
// Regression tests for the bullet-proofing pass: connection-thread
// freezes, zombie connections, panic cascades, tail-loss recovery,
// stale stream incarnations and fast unknown-connection healing.
// =====================================================================

// A CREATE for a stream slot that is occupied by a dead-but-still-referenced
// stream must be dropped, NOT waited for. The old do/while(true) spun on the
// connection processing thread forever (the test handle keeps the closed stream
// alive), freezing the whole connection while it still reported CONNECTED.
TEST_F(ScorpioUdpTester, create_for_busy_dead_slot_does_not_freeze_connection) {
  std::vector<EventQueueItem> events;
  auto connection_handle = create_connection(events);
  auto stream_handle = establish_incoming_reliable_stream(events, connection_handle, 1, 16);
  events.push_back({ WHERE, 0, std::make_unique<DrainSendQueueEvent>() });
  // Peer closes the stream; the local copy goes CLOSED but stays referenced by
  // the test handle, so the slot remains claimed.
  events.push_back({ WHERE, 0, std::make_unique<SendPacket>(Ipv4(127, 0, 0, 1), 12345,
    generate_single_packet(Code::CLOSE_STREAM,
      close_stream_payload(1, Code::CloseStreamSubCommands::CLOSE), 0, 1)) });
  events.push_back({ WHERE, 0, std::make_unique<ExpectPacketAnySeqTimeout>(Ipv4(127, 0, 0, 1), 12345,
    generate_single_packet(Code::CLOSE_STREAM,
      close_stream_payload(1, Code::CloseStreamSubCommands::CLOSED), 0, 1)) });
  events.push_back({ WHERE, 0, stream_handle->stream_is_alive(false) });
  events.push_back({ WHERE, 0, std::make_unique<DrainSendQueueEvent>() });
  // Peer starts a new incarnation of stream 1 while the slot is still busy.
  const ScorpioUdpStream::StreamQoS qos{ 16, ScorpioUdpStream::StreamQoS::Reliability::RELIABLE_ORDERED };
  events.push_back({ WHERE, 0, std::make_unique<SendPacket>(Ipv4(127, 0, 0, 1), 12345,
    generate_single_packet(Code::CREATE_STREAM,
      create_stream_payload(1, qos, Code::CreateStreamSubCommands::CREATE,
        static_cast<StreamEpoch>(TEST_STREAM_EPOCH + 1)))) });
  events.push_back({ WHERE, 0, std::make_unique<SleepEvent>(TICK_TIME) });
  // The CREATE is dropped silently and the connection thread keeps running:
  // no CREATE_STREAM response of any kind, but heartbeats keep flowing.
  events.push_back({ WHERE, 0, std::make_unique<ExpectNoPacket>(TICK_TIME, 5,
    ExpectNoPacket::Filter::CREATE_STREAM_RESPONSE_ONLY) });
  events.push_back({ WHERE, 0, std::make_unique<ExpectPacketTimeout>(Ipv4(127, 0, 0, 1), 12345,
    generate_single_packet(Code::HEARTBEAT, heartbeat_payload())) });
  events.push_back({ WHERE, 0, connection_handle->connection_is_alive(true) });
  execute_test(events);
}

// Heartbeats carrying a MISMATCHED connection id must not refresh the
// connection's liveness clock. They used to (the timestamp was stored before id
// validation), so two mismatched connection incarnations could keep each other
// alive forever while every actual packet was dropped - a permanent zombie.
TEST_F(ScorpioUdpTester, mismatched_id_heartbeats_do_not_keep_connection_alive) {
  std::vector<EventQueueItem> events;
  auto connection_handle = create_connection(events);
  constexpr ConnectionId kWrongId = TEST_PEER_CONNECTION_ID ^ 0xF0F0F0F0F0F0F0F0ULL;
  for (int i = 0; i < 3; ++i) {
    events.push_back({ WHERE, 0, std::make_unique<AdvanceTimeEvent>(SCU_UDP_TIMEOUT / 2) });
    events.push_back({ WHERE, 0, std::make_unique<SendPacket>(Ipv4(127, 0, 0, 1), 12345,
      generate_single_packet(Code::HEARTBEAT, heartbeat_payload({ }, kWrongId))) });
  }
  // Only wrong-id heartbeats arrived for over SCU_UDP_TIMEOUT: the connection
  // must have panicked instead of treating them as proof of life.
  events.push_back({ WHERE, 0, connection_handle->connection_is_panic(true) });
  events.push_back({ WHERE, 0, connection_handle->connection_is_alive(false) });
  execute_test(events);
}

// When a connection dies, every stream on it must die with it. Streams used to
// stay CREATED forever after a connection panic (nothing drove their update()
// anymore), so the application kept sending into the void without ever learning.
TEST_F(ScorpioUdpTester, connection_panic_cascades_to_streams) {
  std::vector<EventQueueItem> events;
  auto connection_handle = create_connection(events);
  auto incoming_stream = establish_incoming_reliable_stream(events, connection_handle, 1, 16);
  auto outgoing_stream = create_outgoing_reliable_stream(events, connection_handle, 2, 16);
  events.push_back({ WHERE, 0, std::make_unique<DrainSendQueueEvent>() });
  // An ERROR packet from the peer panics the connection.
  events.push_back({ WHERE, 0, std::make_unique<SendPacket>(Ipv4(127, 0, 0, 1), 12345,
    generate_single_packet(Code::ERROR, { })) });
  events.push_back({ WHERE, 0, connection_handle->connection_is_panic(true) });
  events.push_back({ WHERE, 0, incoming_stream->stream_is_panic(true) });
  events.push_back({ WHERE, 0, outgoing_stream->stream_is_panic(true) });
  execute_test(events);
}

// Tail-loss recovery: packets above everything the peer's heartbeat ranges
// mention cannot be NACKed (the peer does not know they exist) and were never
// retransmitted - the lost last packets of a burst were gone forever. The sender
// must now resend the unacked tail once it is older than SCU_UDP_RESEND_INTERVAL,
// and the throttle must prevent a resend storm on every heartbeat.
TEST_F(ScorpioUdpTester, reliable_stream_tail_loss_retransmitted_and_throttled) {
  std::vector<EventQueueItem> events;
  auto connection_handle = create_connection(events);
  auto stream_handle = create_outgoing_reliable_stream(events, connection_handle, 1, 16);
  events.push_back({ WHERE, 0, std::make_unique<DrainSendQueueEvent>() });
  std::vector<std::vector<uint8_t>> payloads = { { 0x01 }, { 0x02 }, { 0x03 } };
  for (auto& p : payloads) {
    events.push_back({ WHERE, 0, stream_handle->stream_send(p) });
  }
  for (SeqNumber seq = 0; seq < 3; ++seq) {
    events.push_back({ WHERE, 0, stream_handle->expect_stream_data(seq, payloads[seq]) });
  }
  // Peer: "delivered up to seq 1, holding nothing above". Seq 1 and 2 are pure
  // tail loss - no gap range mentions them. After the resend interval they must
  // be retransmitted anyway.
  events.push_back({ WHERE, SCU_UDP_RESEND_INTERVAL,
      stream_handle->inject_heartbeat(/*initial_end=*/ 1, /*held_ranges=*/ { }) });
  events.push_back({ WHERE, 0, stream_handle->expect_stream_data(1, payloads[1]) });
  events.push_back({ WHERE, 0, stream_handle->expect_stream_data(2, payloads[2]) });
  // The same heartbeat right away must NOT trigger another resend (throttle).
  events.push_back({ WHERE, 0,
      stream_handle->inject_heartbeat(/*initial_end=*/ 1, /*held_ranges=*/ { }) });
  events.push_back({ WHERE, 0, std::make_unique<SleepEvent>(TICK_TIME) });
  events.push_back({ WHERE, 0, std::make_unique<ExpectNoPacket>(TICK_TIME, 2,
    ExpectNoPacket::Filter::STREAM_DATA_ONLY) });
  // After another resend interval the still-unacked tail goes out again.
  events.push_back({ WHERE, SCU_UDP_RESEND_INTERVAL,
      stream_handle->inject_heartbeat(/*initial_end=*/ 1, /*held_ranges=*/ { }) });
  events.push_back({ WHERE, 0, stream_handle->expect_stream_data(1, payloads[1]) });
  events.push_back({ WHERE, 0, stream_handle->expect_stream_data(2, payloads[2]) });
  execute_test(events);
}

// A CREATE for a live peer-created stream with the same QoS but a DIFFERENT
// epoch announces a new incarnation: the local copy is stale and must be
// panicked, with no response. It used to answer ALREADY_EXISTS echoing the new
// epoch, convincing the initiator its new incarnation was established while this
// side still ran the old one - both sides "connected", all data epoch-dropped.
TEST_F(ScorpioUdpTester, create_with_new_epoch_replaces_stale_peer_created_stream) {
  std::vector<EventQueueItem> events;
  auto connection_handle = create_connection(events);
  auto stream_handle = establish_incoming_reliable_stream(events, connection_handle, 1, 16);
  events.push_back({ WHERE, 0, std::make_unique<DrainSendQueueEvent>() });
  const ScorpioUdpStream::StreamQoS qos{ 16, ScorpioUdpStream::StreamQoS::Reliability::RELIABLE_ORDERED };
  events.push_back({ WHERE, 0, std::make_unique<SendPacket>(Ipv4(127, 0, 0, 1), 12345,
    generate_single_packet(Code::CREATE_STREAM,
      create_stream_payload(1, qos, Code::CreateStreamSubCommands::CREATE,
        static_cast<StreamEpoch>(TEST_STREAM_EPOCH + 1)))) });
  events.push_back({ WHERE, 0, stream_handle->stream_is_panic(true) });
  events.push_back({ WHERE, 0, std::make_unique<ExpectNoPacket>(TICK_TIME, 5,
    ExpectNoPacket::Filter::CREATE_STREAM_RESPONSE_ONLY) });
  events.push_back({ WHERE, 0, connection_handle->connection_is_alive(true) });
  execute_test(events);
}

// Cross-create collision, acceptor side: both peers created the same stream
// number independently (different epochs). The tie-break is deterministic - the
// dialer's stream wins - so this side (the connection acceptor) must yield by
// panicking its own stream and staying silent.
TEST_F(ScorpioUdpTester, cross_create_collision_acceptor_yields) {
  std::vector<EventQueueItem> events;
  auto connection_handle = create_connection(events);
  auto stream_handle = create_outgoing_reliable_stream(events, connection_handle, 1, 16);
  events.push_back({ WHERE, 0, std::make_unique<DrainSendQueueEvent>() });
  events.push_back({ WHERE, 0,
      stream_handle->send_create_response(Code::CreateStreamSubCommands::CREATE, /*epoch_delta=*/ 1) });
  events.push_back({ WHERE, 0, stream_handle->stream_is_panic(true) });
  events.push_back({ WHERE, 0, std::make_unique<ExpectNoPacket>(TICK_TIME, 5,
    ExpectNoPacket::Filter::CREATE_STREAM_RESPONSE_ONLY) });
  events.push_back({ WHERE, 0, connection_handle->connection_is_alive(true) });
  execute_test(events);
}

// Cross-create collision, dialer side: the dialer's stream wins the tie-break,
// so the colliding CREATE from the peer is ignored and the local stream stays up.
TEST_F(ScorpioUdpTester, cross_create_collision_dialer_wins) {
  std::vector<EventQueueItem> events;
  const Ipv4 peer(127, 0, 0, 1);
  const Port port = 12345;
  auto connection_handle = ConnectionHandle::create();
  events.push_back({ WHERE, 0, std::make_unique<StartScorpioUdp>() });
  events.push_back({ WHERE, 0, connection_handle->create_connection(peer, port) });
  events.push_back({ WHERE, 0, connection_handle->expect_connect(Code::ConnectSubCommands::CONNECT) });
  events.push_back({ WHERE, 0, connection_handle->send_connect(Code::ConnectSubCommands::ACCEPTED) });
  events.push_back({ WHERE, 0, connection_handle->connection_is_alive(true) });
  events.push_back({ WHERE, TICK_TIME, std::make_unique<NoOpEvent>() });
  auto stream_handle = create_outgoing_reliable_stream(events, connection_handle, 1, 16);
  events.push_back({ WHERE, 0, std::make_unique<DrainSendQueueEvent>() });
  events.push_back({ WHERE, 0,
      stream_handle->send_create_response(Code::CreateStreamSubCommands::CREATE, /*epoch_delta=*/ 1) });
  events.push_back({ WHERE, 0, std::make_unique<SleepEvent>(TICK_TIME) });
  events.push_back({ WHERE, 0, stream_handle->stream_is_active(true) });
  events.push_back({ WHERE, 0, std::make_unique<ExpectNoPacket>(TICK_TIME, 5,
    ExpectNoPacket::Filter::CREATE_STREAM_RESPONSE_ONLY) });
  execute_test(events);
}

// Malformed or stale heartbeat ranges must be ignored, not acted upon. A block
// acknowledging more data than was ever sent used to poison the ack level, and
// a garbage held-range used to walk the resend loop into unsent history slots
// and panic the stream ("Peer expects unsend message").
TEST_F(ScorpioUdpTester, malformed_heartbeat_ranges_are_clamped) {
  std::vector<EventQueueItem> events;
  auto connection_handle = create_connection(events);
  auto stream_handle = create_outgoing_reliable_stream(events, connection_handle, 1, 16);
  events.push_back({ WHERE, 0, std::make_unique<DrainSendQueueEvent>() });
  std::vector<std::vector<uint8_t>> payloads = { { 0x01 }, { 0x02 } };
  for (auto& p : payloads) {
    events.push_back({ WHERE, 0, stream_handle->stream_send(p) });
  }
  for (SeqNumber seq = 0; seq < 2; ++seq) {
    events.push_back({ WHERE, 0, stream_handle->expect_stream_data(seq, payloads[seq]) });
  }
  // "Delivered up to seq 100" - impossible, only 2 packets were sent.
  events.push_back({ WHERE, SCU_UDP_RESEND_INTERVAL,
      stream_handle->inject_heartbeat(/*initial_end=*/ 100, /*held_ranges=*/ { }) });
  events.push_back({ WHERE, 0, std::make_unique<SleepEvent>(TICK_TIME) });
  events.push_back({ WHERE, 0, std::make_unique<ExpectNoPacket>(TICK_TIME, 3,
    ExpectNoPacket::Filter::STREAM_DATA_ONLY) });
  events.push_back({ WHERE, 0, stream_handle->stream_is_active(true) });
  // Held range far beyond anything sent - the resend walk must not run into
  // unsent slots and panic.
  events.push_back({ WHERE, 0,
      stream_handle->inject_heartbeat(/*initial_end=*/ 1, /*held_ranges=*/ { { 50u, 60u } }) });
  events.push_back({ WHERE, 0, std::make_unique<SleepEvent>(TICK_TIME) });
  events.push_back({ WHERE, 0, stream_handle->stream_is_active(true) });
  events.push_back({ WHERE, 0, connection_handle->connection_is_alive(true) });
  execute_test(events);
}

// A stream stuck in CLOSING (the peer never answers the close) must fail after
// SCU_UDP_TIMEOUT instead of retrying forever. A live CLOSING stream pins its
// stream number, which would make that number unusable for any future stream.
TEST_F(ScorpioUdpTester, closing_stream_times_out_to_error) {
  std::vector<EventQueueItem> events;
  auto connection_handle = create_connection(events);
  auto stream_handle = create_outgoing_reliable_stream(events, connection_handle, 1, 16);
  events.push_back({ WHERE, 0, std::make_unique<DrainSendQueueEvent>() });
  events.push_back({ WHERE, 0, stream_handle->close_stream(true) });
  // Keep the CONNECTION alive with bare heartbeats while the stream's close
  // handshake goes unanswered for more than SCU_UDP_TIMEOUT.
  for (int i = 0; i < 3; ++i) {
    events.push_back({ WHERE, 0, std::make_unique<AdvanceTimeEvent>(SCU_UDP_TIMEOUT / 2) });
    events.push_back({ WHERE, 0, std::make_unique<SendPacket>(Ipv4(127, 0, 0, 1), 12345,
      generate_single_packet(Code::HEARTBEAT, heartbeat_payload())) });
  }
  events.push_back({ WHERE, 0, stream_handle->stream_is_panic(true) });
  events.push_back({ WHERE, 0, connection_handle->connection_is_alive(true) });
  execute_test(events);
}

// A heartbeat for a connection this socket knows nothing about draws a
// DISCONNECT/ALREADY_DISCONNECTED echoing the received id (rate limited), so a
// peer that survived our restart learns immediately instead of after 5 s.
TEST_F(ScorpioUdpTester, heartbeat_for_unknown_connection_draws_already_disconnected) {
  std::vector<EventQueueItem> events;
  constexpr ConnectionId kStaleId = 0xABCDEF0123456789ULL;
  events.push_back({ WHERE, 0, std::make_unique<StartScorpioUdp>() });
  events.push_back({ WHERE, 0, std::make_unique<StartListening>(Ipv4(127, 0, 0, 1), 10001) });
  // Two back-to-back heartbeats: exactly one reply (rate limiter).
  events.push_back({ WHERE, TICK_TIME, std::make_unique<SendPacket>(Ipv4(127, 0, 0, 1), 12345,
    generate_single_packet(Code::HEARTBEAT, heartbeat_payload({ }, kStaleId))) });
  events.push_back({ WHERE, 0, std::make_unique<SendPacket>(Ipv4(127, 0, 0, 1), 12345,
    generate_single_packet(Code::HEARTBEAT, heartbeat_payload({ }, kStaleId))) });
  events.push_back({ WHERE, 0, std::make_unique<ExpectPacketTimeout>(Ipv4(127, 0, 0, 1), 12345,
    generate_single_packet(Code::DISCONNECT,
      id_payload(AS_BYTE(Code::DisconnectSubCommands::ALREADY_DISCONNECTED), kStaleId))) });
  events.push_back({ WHERE, 0, std::make_unique<ExpectNoPacket>(TICK_TIME, 5,
    ExpectNoPacket::Filter::ANY) });
  // After the rate-limit window another heartbeat draws another reply.
  events.push_back({ WHERE, 2 * TICK_TIME, std::make_unique<SendPacket>(Ipv4(127, 0, 0, 1), 12345,
    generate_single_packet(Code::HEARTBEAT, heartbeat_payload({ }, kStaleId))) });
  events.push_back({ WHERE, 0, std::make_unique<ExpectPacketTimeout>(Ipv4(127, 0, 0, 1), 12345,
    generate_single_packet(Code::DISCONNECT,
      id_payload(AS_BYTE(Code::DisconnectSubCommands::ALREADY_DISCONNECTED), kStaleId))) });
  execute_test(events);
}

// Receiving DISCONNECT/ALREADY_DISCONNECTED with the connection's own id means
// the peer has no state for it (peer restarted): the local connection fails
// immediately. A mismatched id must be ignored - it belongs to a previous
// incarnation and must not kill the current connection.
TEST_F(ScorpioUdpTester, already_disconnected_reply_fails_matching_connection) {
  std::vector<EventQueueItem> events;
  auto connection_handle = create_connection(events);
  constexpr ConnectionId kStaleId = TEST_PEER_CONNECTION_ID ^ 0x1ULL;
  events.push_back({ WHERE, 0, std::make_unique<SendPacket>(Ipv4(127, 0, 0, 1), 12345,
    generate_single_packet(Code::DISCONNECT,
      id_payload(AS_BYTE(Code::DisconnectSubCommands::ALREADY_DISCONNECTED), kStaleId))) });
  events.push_back({ WHERE, TICK_TIME, std::make_unique<NoOpEvent>() });
  events.push_back({ WHERE, 0, connection_handle->connection_is_alive(true) });
  events.push_back({ WHERE, 0, std::make_unique<SendPacket>(Ipv4(127, 0, 0, 1), 12345,
    generate_single_packet(Code::DISCONNECT,
      id_payload(AS_BYTE(Code::DisconnectSubCommands::ALREADY_DISCONNECTED), TEST_PEER_CONNECTION_ID))) });
  events.push_back({ WHERE, 0, connection_handle->connection_is_panic(true) });
  events.push_back({ WHERE, 0, connection_handle->connection_is_alive(false) });
  execute_test(events);
}
