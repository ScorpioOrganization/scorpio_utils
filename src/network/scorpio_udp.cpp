#include "scorpio_utils/network/scorpio_udp.hpp"

#include <chrono>
#include <cmath>
#include <exception>
#include <iomanip>
#include <sstream>

#include "scorpio_utils/assert.hpp"
#include "scorpio_utils/decorators.hpp"
#include "scorpio_utils/magic_enum_include.hpp"
#include "scorpio_utils/misc.hpp"
#include "scorpio_utils/network/types.hpp"
#include "scorpio_utils/sat_math.hpp"

/**
 * Possible optimizations:
 *  - Append header at the end of the packets not at the beginning to avoid memcopy
 *  - Use shared_ptr of const vector<uint8_t> for packets to avoid memcopy on send
 *  - While giving streams some CPU time look only for active streams (not iterate while 65536 streams)
 *
 * Worth to do:
 *  - Create some nested classes so it is clear which variable is used on which thread
 */

using std::literals::string_literals::operator""s;
using scorpio_utils::network::Code;
using scorpio_utils::network::CodeType;
using scorpio_utils::network::FramesLeft;
using scorpio_utils::network::MessageHeader;
using scorpio_utils::network::Port;
using scorpio_utils::network::ScorpioUdp;
using scorpio_utils::network::ScorpioUdpConnection;
using scorpio_utils::network::ScorpioUdpStream;
using scorpio_utils::network::SeqNumber;
using scorpio_utils::network::StreamNumber;
using scorpio_utils::network::UdpData;
using scorpio_utils::network::TimeProvider;
using scorpio_utils::network::StreamEpoch;
using scorpio_utils::network::SendOutcome;

static_assert(sizeof(Code::ConnectSubCommands) == 1, "ConnectSubCommands size is assumed to be 1 byte");
static_assert(sizeof(Code::DisconnectSubCommands) == 1, "DisconnectSubCommands size is assumed to be 1 byte");

#define AS_BYTE(x) (SCU_AS(uint8_t, x))

struct PanicException : public std::exception { };

SCU_HOT SCU_CONST_FUNC constexpr size_t packets_count(size_t data_size, size_t header_without_frames_left) {
  size_t packets = SCU_AS(size_t, data_size != 0);
  if (data_size > (SCU_UDP_MAX_PACKET_SIZE - header_without_frames_left)) {
    data_size -= (SCU_UDP_MAX_PACKET_SIZE - header_without_frames_left);
    auto packet_size = (SCU_UDP_MAX_PACKET_SIZE - header_without_frames_left - sizeof(FramesLeft));
    packets += data_size / packet_size;
    packets += SCU_AS(size_t, (data_size % packet_size) != 0);
  }
  return packets;
}

SCU_CONST_FUNC constexpr size_t calculate_header_without_frames_left_size(Code code) {
  return sizeof(CodeType) +
         (code.is_command_for_stream() ? (sizeof(SeqNumber) + sizeof(StreamNumber)) : 0);
}

SCU_HOT SCU_PURE static scorpio_utils::Expected<MessageHeader, std::string> parse_header(
  const std::vector<uint8_t>& data) {
  using scorpio_utils::network::network_to_host;
  using scorpio_utils::Unexpected;
  MessageHeader header;
  header.data_offset = 0;
  CodeType code_byte = 0;
  if (!network_to_host(data, &code_byte, header.data_offset)) {
    // scorpio_utils::logger::Logger* logger;
    // SCU_LOG_FATAL(logger, "Failed to parse header: not enough data");
    return Unexpected("Failed to parse header: not enough data"s);
  }
  Code code(code_byte);
  header.command = code.get_command();
  header.is_first = code.is_first();
  if (!code.is_command_for_stream()) {
    header.stream_number = std::nullopt;
    header.seq_number = std::nullopt;
  } else {
    header.stream_number = 0;
    if (!network_to_host(data, &*header.stream_number, header.data_offset)) {
      return Unexpected("Failed to parse header: not enough data for stream_number"s);
    }
    header.seq_number = 0;
    if (!network_to_host(data, &*header.seq_number, header.data_offset)) {
      return Unexpected("Failed to parse header: not enough data for seq_number"s);
    }
  }
  if (code.is_not_last()) {
    header.frames_left = 0;
    if (!network_to_host(data, &*header.frames_left, header.data_offset)) {
      return Unexpected("Failed to parse header: not enough data for frames_left"s);
    }
  }
  return header;
}

static std::optional<ScorpioUdpStream::StreamQoS> parse_qos(const std::vector<uint8_t>& data, size_t& offset) {
  ScorpioUdpStream::StreamQoS qos{ 0, ScorpioUdpStream::StreamQoS::Reliability::UNRELIABLE };
  if (!scorpio_utils::network::network_to_host(data, &qos.reliability, offset)) {
    return std::nullopt;
  }
  if (qos.is_reliable() && !scorpio_utils::network::network_to_host(data, &qos.depth, offset)) {
    return std::nullopt;
  }
  return qos;
}

static void serialize_qos(const ScorpioUdpStream::StreamQoS& qos, std::vector<uint8_t>& data, size_t& offset) {
  SCU_DO_AND_ASSERT(scorpio_utils::network::host_to_network(qos.reliability, data, offset),
    "Failed to serialize QoS reliability");
  if (qos.is_reliable()) {
    data.resize(data.size() + sizeof(qos.depth));
    SCU_DO_AND_ASSERT(scorpio_utils::network::host_to_network(qos.depth, data, offset),
      "Failed to serialize QoS depth");
  }
}

// ========================= ScorpioUdp implementation =================================

std::shared_ptr<TimeProvider> ScorpioUdp::get_time_provider() {
  static std::mutex mutex;
  std::lock_guard lock(mutex);
  static std::weak_ptr<TimeProvider> weak_provider;
  if (auto provider = weak_provider.lock()) {
    return provider;
  }
  auto provider = std::make_shared<TimeProvider>();
  weak_provider = provider;
  return provider;
}

std::shared_ptr<ScorpioUdp> ScorpioUdp::create(
#ifdef SCU_UDP_MOCK
  scorpio_utils::network::UdpSocket& socket,
#endif
  std::shared_ptr<logger::Logger> logger
) {
  auto ans = std::shared_ptr<ScorpioUdp>(new ScorpioUdp(
#ifdef SCU_UDP_MOCK
    socket,
#endif
    logger
  ));
  ans->_start_signal.notify(100000);
  return ans;
}

ScorpioUdp::ScorpioUdp(
#ifdef SCU_UDP_MOCK
  scorpio_utils::network::UdpSocket& socket,
#endif
  std::shared_ptr<logger::Logger> logger
)
: _time_provider([] {
      auto result = get_time_provider();
      result->set_time_offset(SCU_UDP_HEARTBEAT_PERIOD / 2);
      return result;
    }()),
#ifdef SCU_UDP_MOCK
  _socket(socket),
#endif
  _random_engine(SCU_AS(uint64_t, std::random_device{ }()) << 32 | SCU_AS(uint64_t, std::random_device{ }())),
  _received_bytes(0),
  _send_bytes(0),
  _auto_accept(false),
  _stop(true),
  _logger(logger),
  _panic(false),
  _panic_count(0),
  _last_panic_time(0),
  _new_connections(nullptr)
{
  SCU_LOG_INFO(_logger, "ScorpioUdp created");
}

ScorpioUdp::~ScorpioUdp() {
  SCU_LOG_INFO(_logger, "ScorpioUdp destructor called");
  _awaiting_connections_channel.close();
  _receiver_channel.close();
  _sender_channel.close();
  _control_sender_channel.close();
  stop();
  std::lock_guard lock(_threads_mutex);
  SCU_ASSERT(_threads.empty(), "ScorpioUdp threads not stopped");
  SCU_LOG_INFO(_logger, "ScorpioUdp destroyed");
}

bool ScorpioUdp::send(
  Ipv4 remote_ip,
  Port remote_port,
  std::vector<uint8_t>&& packet
) {
  return send_impl(remote_ip, remote_port, std::move(packet), true) == SendOutcome::SENT;
}

SendOutcome ScorpioUdp::send_impl(
  Ipv4 remote_ip,
  Port remote_port,
  std::vector<uint8_t>&& packet,
  bool block
) {
  if (SCU_UNLIKELY(!_socket.is_open())) {
    SCU_LOG_ERROR(_logger, "Failed to send packet because socket is not open");
    return SendOutcome::CLOSED;
  }
  SCU_ASSERT(!packet.empty(), "Attempted to send an empty packet");
  // Control traffic (heartbeats, handshakes, closes) gets its own queue so bulk
  // STREAM_DATA cannot starve it - the sender thread drains control first.
  const bool is_control = Code(packet[0]).get_command() != Code::STREAM_DATA;
  auto& channel = is_control ? _control_sender_channel : _sender_channel;
  try {
    UdpData message{
      /*._ip =   */ remote_ip,
      /*._port = */ remote_port,
      /*._data = */ std::move(packet),
    };
    if (block) {
      channel.send<true>(std::move(message));
    } else if (SCU_UNLIKELY(channel.send<false>(std::move(message)).has_value())) {
      return SendOutcome::FULL;
    }
  } catch (const threading::ClosedChannelException&) {
    SCU_LOG_ERROR(_logger, "Failed to send packet because sender channel is closed");
    return SendOutcome::CLOSED;
  }
  return SendOutcome::SENT;
}

bool ScorpioUdp::stop() {
  std::unique_lock lock(_threads_mutex, std::try_to_lock);
  if (!lock.owns_lock()) {
    return false;
  }
  bool expected = false;
  if (SCU_UNLIKELY(!_stop.compare_exchange_strong(expected, true, std::memory_order_relaxed,
    std::memory_order_relaxed))) {
    return false;
  }
  SCU_LOG_INFO(_logger, "ScorpioUdp stopping");
  _auto_accept.store(false, std::memory_order_relaxed);
  _socket.close();
  const auto this_thread_id = std::this_thread::get_id();
  while (!_threads.empty()) {
    if (_threads.back().get_id() == this_thread_id) {
      _threads.back().detach();
    } else if (_threads.back().joinable()) {
      _threads.back().join();
    }
    _threads.pop_back();
  }
  _new_connections.reset();
  SCU_LOG_INFO(_logger, "ScorpioUdp stopped");
  return true;
}

bool ScorpioUdp::start() {
  bool expected = true;
  if (SCU_UNLIKELY(!_stop.compare_exchange_strong(expected, false, std::memory_order_relaxed,
                                                  std::memory_order_relaxed))) {
    return false;
  }
  SCU_LOG_INFO(_logger, "ScorpioUdp starting");
  if (SCU_UNLIKELY(!_socket.open())) {
    panic("Failed to open socket");
    return false;
  }
  _new_connections = std::make_unique<decltype(_new_connections)::element_type>();
  _threads.emplace_back(&ScorpioUdp::receiver_thread, this);
  _threads.emplace_back(&ScorpioUdp::sender_thread, this);
  _threads.emplace_back(&ScorpioUdp::processing_thread, this);
  SCU_LOG_INFO(_logger, "ScorpioUdp started");
  return true;
}

scorpio_utils::Expected<scorpio_utils::Success, std::string> ScorpioUdp::listen(Ipv4 local_ip, Port port) {
  if (SCU_UNLIKELY(_socket.is_bound())) {
    return Unexpected("Socket is already bound"s);
  }
  return _socket.bind(local_ip, port);
}

SCU_NORETURN SCU_COLD void ScorpioUdp::panic(std::string&& message) {
  std::unique_lock<std::mutex> lock(_panic_mutex, std::try_to_lock);
  if (!lock.owns_lock()) {
    // Lock mutex to delay returning from panic so, there is _panic_message set
    lock.lock();
    throw PanicException();
  }
  SCU_LOG_FATAL(_logger, "Panic: {}", message);
  SCU_UNLIKELY_THROW_IF(_panic.load(std::memory_order_relaxed), PanicException, );
  _panic_message = std::move(message);
  _panic_count.fetch_add(1, std::memory_order_relaxed);
  _last_panic_time.store(_time_provider->get_time(), std::memory_order_relaxed);
  bool expected = false;
  if (_panic.compare_exchange_strong(expected, true, std::memory_order_relaxed, std::memory_order_relaxed)) {
    stop();
  }
  throw PanicException();
}

[[maybe_unused]] static auto create_log(
  scorpio_utils::network::Ipv4 local_ip, scorpio_utils::network::Port local_port,
  scorpio_utils::network::MessageHeader message_header,
  const std::vector<uint8_t>& data) {
  std::stringstream ss;
  ss << local_ip << ':' << local_port << " | " <<
    magic_enum::enum_name(SCU_AS(Code::Values, message_header.command)) << " | is_first: " <<
    message_header.is_first;
  if (message_header.stream_number) {
    ss << " | stream_number: " << *message_header.stream_number;
  }
  if (message_header.seq_number) {
    ss << " | seq_number: " << *message_header.seq_number;
  }
  if (message_header.frames_left) {
    ss << " | frames_left: " << *message_header.frames_left;
  }
  ss << " | data_size: " << data.size();
  return ss.str();
}

void ScorpioUdp::receiver_thread() {
  try {
    while (SCU_LIKELY(!_stop.load(std::memory_order_relaxed))) {
      std::vector<uint8_t> data(SCU_UDP_MAX_PACKET_SIZE);
      auto result = _socket.receive(data.data(), data.size());
      if (SCU_UNLIKELY(result.is_err())) {
        panic("Failed to receive data: " + std::move(result).err_value());
      }
      if (result.ok_value().byte_count == 0) {
        continue;
      }
      // SCU_LOG_TRACE(_logger, "Received from: {}",
      // create_log(result.ok_value().remote_ip, result.ok_value().remote_port, parse_header(data).ok().value(), data));
      data.resize(result.ok_value().byte_count);
      _received_bytes.fetch_add(data.size(), std::memory_order_relaxed);
      _receiver_channel.send<true>({
        /*._ip   = */ result.ok_value().remote_ip,
        /*._port = */ result.ok_value().remote_port,
        /*._data = */ std::move(data),
      });
    }
  } catch (const PanicException&) {
  } catch (const threading::ClosedChannelException&) {
  }
}

void ScorpioUdp::sender_thread() {
  try {
    while (SCU_LIKELY(!_stop.load(std::memory_order_relaxed))) {
      // Strict priority: control packets (heartbeats, handshakes) always go out
      // before queued bulk data, so saturation cannot cause false timeouts.
      auto msg_opt = _control_sender_channel.receive();
      if (!msg_opt.has_value()) {
        msg_opt = _sender_channel.receive();
      }
      if (!msg_opt.has_value()) {
        std::this_thread::sleep_for(std::chrono::microseconds(10));
        continue;
      }
      auto& msg = *msg_opt;
      SCU_ASSERT(msg.data.size() <= SCU_UDP_MAX_PACKET_SIZE,
        "UDP message size is too large (" << msg.data.size()
                                          << " bytes), max is " << SCU_UDP_MAX_PACKET_SIZE << " bytes");
      if (SCU_UNLIKELY(!_socket.is_open())) {
        panic("Socket is not open");
      }
      // SCU_LOG_TRACE(_logger, "Sending to: {}",
      // create_log(msg.ip, msg.port, parse_header(msg.data).ok().value(), msg.data));
      _send_bytes.fetch_add(msg.data.size(), std::memory_order_relaxed);
      auto result = _socket.send(msg.data.data(), msg.data.size(), msg.ip, msg.port);
      if (SCU_UNLIKELY(result.is_err())) {
        panic(std::move(result).err_value());
      }
      SCU_ASSERT(result.ok_value() == msg.data.size(), "UDP send less bytes then requested: " << result.ok_value()
                                                                                              << " of "
                                                                                              << msg.data.size());
    }
  } catch (const PanicException&) {
  } catch (const threading::ClosedChannelException&) {
  }
}

void ScorpioUdp::processing_thread() {
  try {
    _start_signal.wait();
    const auto self_weak = weak_from_this();
    std::shared_ptr<ScorpioUdp> self;
    while (SCU_LIKELY((self = self_weak.lock()) && !_stop.load(std::memory_order_relaxed))) {
      SCU_DEFER([&self] { self.reset(); });
      threading::EagerSelectTimeout timeout(50, _time_provider);
      std::visit(VisitorOverloadingHelper{
        [this](UdpData packet) SCU_ALWAYS_INLINE_RAW {
          process_packet(std::move(packet));
        },
        [this](std::weak_ptr<ScorpioUdpConnection> connection) SCU_ALWAYS_INLINE_RAW {
          pull_awaiting_connections(connection);
        },
        [] (Empty) SCU_ALWAYS_INLINE_RAW { /* Timeout */ },
      }, threading::eager_select(_receiver_channel, _awaiting_connections_channel, timeout.start()));
    }
  } catch (const PanicException&) {
  } catch (const threading::ClosedChannelException&) {
  }
}

void ScorpioUdp::handle_ping_packet(const MessageHeader& header, const UdpData& udp_data) {
  if (udp_data.data.size() - header.data_offset - 1 != 0) {
    SCU_UNIMPLEMENTED();
  } else {
    switch (SCU_AS(Code::PingSubCommands, udp_data.data[header.data_offset])) {
      case Code::PingSubCommands::PING: {
          SCU_UNIMPLEMENTED();
        } break;
      case Code::PingSubCommands::PONG: {
          send_or_panic(std::nullopt, _mock_sequence_number, Code::PING, udp_data.ip, udp_data.port, { 1 },
                                "Failed to send PONG response");
        } break;
      default: {
          SCU_UNIMPLEMENTED();
        } break;
    }
  }
}

void ScorpioUdp::handle_connect_packet(const MessageHeader& header, const UdpData& udp_data) {
  if (udp_data.data.size() - header.data_offset != sizeof(Code::ConnectSubCommands) + sizeof(ConnectionId)) {
    // TODO(@Igor): Handle error properly
    SCU_LOG_ERROR(_logger,
      "Invalid CONNECT packet size: expected {} byte for subcommand, got {}",
      sizeof(Code::ConnectSubCommands) + sizeof(ConnectionId), udp_data.data.size() - header.data_offset);
    return;
  }
  SCU_LOG_TRACE(_logger, "Handling CONNECT packet from {}:{}. Subcommand: {}",
    udp_data.ip.str(), udp_data.port, SCU_AS(Code::ConnectSubCommands, udp_data.data[header.data_offset]));
  size_t offset = header.data_offset + sizeof(Code::ConnectSubCommands);
  ConnectionId connection_id;
  SCU_DO_AND_ASSERT(scorpio_utils::network::network_to_host(udp_data.data, &connection_id, offset),
    "Failed to parse connection_id from CONNECT packet");
  switch (SCU_AS(Code::ConnectSubCommands, udp_data.data[header.data_offset])) {
    case Code::ConnectSubCommands::CONNECT: {
        const auto connection = get_connection(udp_data.ip, udp_data.port);
        std::vector<uint8_t> connect_data;
        connect_data.resize(sizeof(Code::ConnectSubCommands) + sizeof(ConnectionId));
        offset = sizeof(Code::ConnectSubCommands);
        SCU_DO_AND_ASSERT(host_to_network(connection_id, connect_data, offset),
          "Failed to convert connection ID to network format for CONNECT packet");
        if (connection) {
          if (connection->connection_id() != connection_id) {
            // TODO(@Igor): Handle error properly
            SCU_LOG_ERROR(_logger,
                          "Received CONNECT for existing connection with different connection_id. ip: {}, port: {}, "
                          "existing connection_id: {}, received connection_id: {}",
              udp_data.ip.str(), udp_data.port, connection->connection_id(), connection_id);
            connection->panic_soft("Received CONNECT for connection with same ip and port but different connection_id");
            // Lets send nothing here it is not the cleanest solution but this way we will not create a new connection
            // and will not reject it either. The client will try again in some time
            // and meanwhile some old packets will be send/dropped
          } else {
            connect_data[0] = AS_BYTE(Code::ConnectSubCommands::ALREADY_CONNECTED);
            send_or_panic(std::nullopt, _mock_sequence_number, Code::CONNECT,
                        udp_data.ip, udp_data.port,
              connect_data, "Failed to send ALREADY_CONNECTED response");
          }
        } else if (_auto_accept.load(std::memory_order_relaxed)) {
          std::shared_ptr<ScorpioUdpConnection> new_connection(new ScorpioUdpConnection(
                      udp_data.ip, udp_data.port, connection_id, false, shared_from_this()));
          new_connection->_start_signal.notify(100000);
          new_connection->_state.store(ScorpioUdpConnection::State::CONNECTING);
          _connections.insert({ { udp_data.ip, udp_data.port }, new_connection });
          SCU_ASSERT(_new_connections, "Channel must exist if auto accept is enabled");
          _new_connections->send<true>(new_connection);
          connect_data[0] = AS_BYTE(Code::ConnectSubCommands::ACCEPTED);
          send_or_panic(std::nullopt, _mock_sequence_number, Code::CONNECT,
                      udp_data.ip, udp_data.port, connect_data, "Failed to send ACCEPTED response");
          new_connection->connected();
        } else {
          connect_data[0] = AS_BYTE(Code::ConnectSubCommands::REJECTED);
          send_or_panic(std::nullopt, _mock_sequence_number, Code::CONNECT,
                      udp_data.ip, udp_data.port, connect_data, "Failed to send REJECTED response");
        }
      } break;
    case Code::ConnectSubCommands::ACCEPTED: {
        auto connection = get_connection(udp_data.ip, udp_data.port);
        if (!connection) {
          SCU_LOG_ERROR(_logger, "Received ACCEPTED for non-existing connection ip: {}, port: {}",
          udp_data.ip.str(), udp_data.port);
          // TODO(@Igor): Handle error properly
          send_or_panic(std::nullopt, _mock_sequence_number, Code::ERROR, udp_data.ip, udp_data.port,
            { }, "Failed to send ERROR response for ACCEPTED subcommand for non-existing connection");
        } else if (connection->connection_id() != connection_id) {
          SCU_LOG_ERROR(_logger, "Received ACCEPTED for connection with different connection_id. ip: {}, port: {}, "
            "existing connection_id: {}, received connection_id: {}",
            udp_data.ip.str(), udp_data.port, connection->connection_id(), connection_id);
          // TODO(@Igor): Handle error properly
          send_or_panic(std::nullopt, _mock_sequence_number, Code::ERROR, udp_data.ip, udp_data.port,
            { }, "Failed to send ERROR response for ACCEPTED subcommand for connection with different connection_id");
        } else if (!connection->connected()) {
          SCU_LOG_WARNING(_logger,
                          "Received ACCEPTED for connection not in CONNECTING state. ip: {}, port: {} - ignoring",
            udp_data.ip.str(), udp_data.port);
          // TODO(@Igor): Handle error properly
        }
      } break;
    case Code::ConnectSubCommands::REJECTED: {
        auto connection = get_connection(udp_data.ip, udp_data.port);
        if (!connection) {
          // TODO(@Igor): Handle error properly
          SCU_LOG_ERROR(_logger, "Received REJECTED for non-existing connection ip: {}, port: {}",
            udp_data.ip.str(), udp_data.port);
        } else if (connection->connection_id() != connection_id) {
          SCU_LOG_ERROR(_logger, "Received REJECTED for connection with different connection_id. ip: {}, port: {}, "
            "existing connection_id: {}, received connection_id: {}",
            udp_data.ip.str(), udp_data.port, connection->connection_id(), connection_id);
          // TODO(@Igor): Handle error properly
        } else {
          SCU_LOG_INFO(_logger, "Connection rejected by {}:{}", udp_data.ip.str(), udp_data.port);
          connection->_state = ScorpioUdpConnection::State::REJECTED;
        }
      } break;
    case Code::ConnectSubCommands::ALREADY_CONNECTED: {
        auto connection = get_connection(udp_data.ip, udp_data.port);
        ScorpioUdpConnection::State expected = ScorpioUdpConnection::State::CONNECTING;
        if (!connection) {
          SCU_LOG_ERROR(_logger, "Received ALREADY_CONNECTED for non-existing connection ip: {}, port: {}",
            udp_data.ip.str(), udp_data.port);
        } else if (connection->connection_id() != connection_id) {
          SCU_LOG_ERROR(_logger, "Received ALREADY_CONNECTED for connection with different connection_id. "
            "ip: {}, port: {}, existing connection_id: {}, received connection_id: {}",
            udp_data.ip.str(), udp_data.port, connection->connection_id(), connection_id);
        } else if (connection->_state.compare_exchange_strong(expected, ScorpioUdpConnection::State::CONNECTED,
        std::memory_order_relaxed, std::memory_order_relaxed)) {
          SCU_LOG_INFO(_logger, "Connection established (ALREADY_CONNECTED) with {}:{}",
          udp_data.ip.str(), udp_data.port);
        } else {
          SCU_LOG_ERROR(_logger, "Received ALREADY_CONNECTED for connection in {} state. ip: {}, port: {}", expected,
            udp_data.ip.str(), udp_data.port);
        }
      } break;
    default: {
        // TODO(@Igor): Handle error properly
        SCU_LOG_ERROR(_logger, "Received unknown CONNECT subcommand: {} from {}:{}", udp_data.data[header.data_offset],
          udp_data.ip.str(), udp_data.port);
      } break;
  }
}

void ScorpioUdp::handle_disconnect_packet(const MessageHeader& header, const UdpData& udp_data) {
  if (udp_data.data.size() - header.data_offset != sizeof(Code::DisconnectSubCommands) + sizeof(ConnectionId)) {
    // TODO(@Igor): Handle error properly
    SCU_LOG_ERROR(_logger,
      "Invalid DISCONNECT packet size: expected {} byte for subcommand, got {}",
      sizeof(Code::DisconnectSubCommands) + sizeof(ConnectionId), udp_data.data.size() - header.data_offset);
    return;
  }
  SCU_LOG_TRACE(_logger, "Handling DISCONNECT packet from {}:{}. Subcommand: {}",
    udp_data.ip.str(), udp_data.port, SCU_AS(Code::DisconnectSubCommands, udp_data.data[header.data_offset]));
  size_t offset = header.data_offset + sizeof(Code::DisconnectSubCommands);
  ConnectionId connection_id;
  SCU_DO_AND_ASSERT(scorpio_utils::network::network_to_host(udp_data.data, &connection_id, offset),
    "Failed to parse connection_id from DISCONNECT packet");
  switch (SCU_AS(Code::DisconnectSubCommands, udp_data.data[header.data_offset])) {
    case Code::DisconnectSubCommands::DISCONNECT: {
        std::vector<uint8_t> data;
        data.resize(sizeof(Code::DisconnectSubCommands) + sizeof(ConnectionId));
        offset = sizeof(Code::DisconnectSubCommands);
        SCU_DO_AND_ASSERT(scorpio_utils::network::host_to_network(connection_id, data, offset),
        "Failed to serialize connection_id for DISCONNECT packet");
        if (auto connection_opt = get_connection(udp_data.ip, udp_data.port);
          connection_opt != nullptr && connection_opt->is_alive() && connection_opt->connection_id() == connection_id) {
          data[0] = AS_BYTE(Code::DisconnectSubCommands::ACCEPTED);
          send_or_panic(std::nullopt, _mock_sequence_number, Code::DISCONNECT,
          udp_data.ip, udp_data.port, data,
          "Failed to send DISCONNECT ACCEPTED response");
          connection_opt->close(false);
        } else {
          // Handle disconnect for non-existing connection
          data[0] = AS_BYTE(Code::DisconnectSubCommands::ALREADY_DISCONNECTED);
          send_or_panic(std::nullopt, _mock_sequence_number, Code::DISCONNECT,
          udp_data.ip, udp_data.port, data,
          "Failed to send DISCONNECT ACCEPTED response for non-existing connection");
        }
      } break;
    case Code::DisconnectSubCommands::ALREADY_DISCONNECTED: {
        // The peer answered one of our packets (typically a heartbeat) with "I have
        // no such connection": it restarted or dropped its state. Fail the local
        // connection right away so the layer above can rebuild it, instead of
        // sending into the void until the 5 s timeout fires. The connection id
        // check makes stale replies to a previous incarnation harmless.
        if (auto connection_opt = get_connection(udp_data.ip, udp_data.port);
          connection_opt != nullptr && connection_opt->connection_id() == connection_id &&
          connection_opt->state() == ScorpioUdpConnection::State::CONNECTED) {
          SCU_LOG_WARNING(_logger,
            "Peer {}:{} reports no state for connection {} - failing the local connection",
            udp_data.ip.str(), udp_data.port, connection_id);
          connection_opt->panic_soft("Peer reports this connection as disconnected (peer restarted?)");
        }
      } break;
    default: {
        // TODO(@Igor): Handle error properly
        SCU_LOG_WARNING(_logger, "Received unknown/unexpected DISCONNECT subcommand: {} from {}:{}",
  udp_data.data[header.data_offset],
  udp_data.ip.str(), udp_data.port);
      } break;
  }
}

SCU_HOT void ScorpioUdp::process_packet(UdpData udp_data) {
  auto header_opt = parse_header(udp_data.data);
  if (SCU_UNLIKELY(header_opt.is_err())) {
    // TODO(@Igor): Handle error properly
    SCU_LOG_ERROR(_logger, "Failed to parse UDP packet header from {}:{}. Error: {}",
      udp_data.ip.str(), udp_data.port, header_opt.err_value());
    return;
  }
  auto header = std::move(header_opt).ok_value();
  switch (header.command) {
    case Code::PING: {
        handle_ping_packet(header, udp_data);
      } break;
    case Code::CONNECT: {
        handle_connect_packet(header, udp_data);
      } break;
    case Code::DISCONNECT: {
        handle_disconnect_packet(header, udp_data);
      } break;
    default: {
        auto connection_opt = get_connection(udp_data.ip, udp_data.port);
        if (connection_opt != nullptr && connection_opt->state() == ScorpioUdpConnection::State::CONNECTED) {
          if (header.command == Code::HEARTBEAT) {
            // Routing-time liveness refresh: heartbeats lead with the connection id,
            // so it can be validated right here, without the connection's processing
            // thread. This keeps a backlogged connection (deep _incoming_packets
            // queue) from false-panicking with "no packets received" while valid
            // heartbeats are in fact arriving. A mismatched id never refreshes
            // liveness - the handler drops such heartbeats entirely, as before.
            ConnectionId heartbeat_connection_id;
            size_t connection_id_offset = header.data_offset;
            if (scorpio_utils::network::network_to_host(udp_data.data, &heartbeat_connection_id,
              connection_id_offset) && heartbeat_connection_id == connection_opt->connection_id()) {
              connection_opt->note_liveness();
            }
          }
          connection_opt->_incoming_packets.send<true>({ header, std::move(udp_data) });
        } else if (connection_opt == nullptr && header.command == Code::HEARTBEAT) {
          // A heartbeat from a peer we have no connection for means the peer still
          // believes it is connected (we likely restarted). Telling it so lets it
          // fail its stale connection immediately instead of waiting for the 5 s
          // timeout. Echoing the received connection id makes the reply harmless
          // for any other incarnation.
          SCU_LOG_WARNING(_logger,
            "Received HEARTBEAT for non-existing connection from {}:{} - replying ALREADY_DISCONNECTED",
            udp_data.ip.str(), udp_data.port);
          reply_unknown_connection_heartbeat(header, udp_data);
        } else {
          // Received connection-oriented packet for non-existing (or still
          // connecting) connection - harmless, ignore.
          SCU_LOG_ERROR(_logger,
            "Received packet for non-existing connection from {}:{}. Command: {}",
            udp_data.ip.str(), udp_data.port, SCU_AS(Code, header.command));
        }
      } break;
  }
}

void ScorpioUdp::reply_unknown_connection_heartbeat(const MessageHeader& header, const UdpData& udp_data) {
  ConnectionId connection_id;
  size_t offset = header.data_offset;
  if (SCU_UNLIKELY(!scorpio_utils::network::network_to_host(udp_data.data, &connection_id, offset))) {
    SCU_LOG_WARNING(_logger, "Failed to parse connection_id from HEARTBEAT for non-existing connection");
    return;
  }
  const auto now = _time_provider->get_time();
  const auto address = std::make_pair(udp_data.ip, udp_data.port);
  const auto reply_time_iter = _unknown_connection_reply_times.find(address);
  if (reply_time_iter != _unknown_connection_reply_times.end() &&
    now - reply_time_iter->second < SCU_UDP_HEARTBEAT_PERIOD) {
    return;
  }
  if (_unknown_connection_reply_times.size() > 1024) {
    for (auto it = _unknown_connection_reply_times.begin(); it != _unknown_connection_reply_times.end(); ) {
      if (now - it->second >= 10 * SCU_UDP_HEARTBEAT_PERIOD) {
        it = _unknown_connection_reply_times.erase(it);
      } else {
        ++it;
      }
    }
  }
  _unknown_connection_reply_times[address] = now;
  std::vector<uint8_t> data(sizeof(Code::DisconnectSubCommands) + sizeof(ConnectionId));
  data[0] = AS_BYTE(Code::DisconnectSubCommands::ALREADY_DISCONNECTED);
  size_t data_offset = sizeof(Code::DisconnectSubCommands);
  SCU_DO_AND_ASSERT(scorpio_utils::network::host_to_network(connection_id, data, data_offset),
    "Failed to serialize connection_id for ALREADY_DISCONNECTED reply");
  // Best effort: if this cannot be sent the peer just falls back to its timeout.
  if (SCU_UNLIKELY(send(std::nullopt, _mock_sequence_number, Code::DISCONNECT, udp_data.ip, udp_data.port, data,
    false) != SendOutcome::SENT)) {
    SCU_LOG_WARNING(_logger, "Failed to send ALREADY_DISCONNECTED reply to {}:{}",
      udp_data.ip.str(), udp_data.port);
  }
}

void ScorpioUdp::pull_awaiting_connections(std::weak_ptr<ScorpioUdpConnection> connection_weak) {
  if (auto connection = connection_weak.lock()) {
    if (get_connection(connection->remote_ip(), connection->remote_port())) {
      connection->panic_soft("Connection already exists");
    } else {
      SCU_ASSERT(connection->state() == ScorpioUdpConnection::State::NEW,
          "Connection in invalid state");
      std::vector<uint8_t> data(sizeof(Code::ConnectSubCommands) + sizeof(ConnectionId));
      data[0] = AS_BYTE(Code::ConnectSubCommands::CONNECT);
      size_t offset = sizeof(Code::ConnectSubCommands);
      SCU_DO_AND_ASSERT(scorpio_utils::network::host_to_network(connection->connection_id(), data, offset),
        "Failed to serialize connection_id for CONNECT packet");
      send_or_panic(std::nullopt, _mock_sequence_number, Code::CONNECT, connection->remote_ip(),
          connection->remote_port(), data, "Failed to send CONNECT packet");
      connection->_state.store(ScorpioUdpConnection::State::CONNECTING, std::memory_order_relaxed);
      auto address_pair = std::make_pair(connection->remote_ip(), connection->remote_port());
      _connections.insert({ address_pair, std::move(connection) });
      SCU_LOG_INFO(_logger, "Added new connection {}:{}", address_pair.first.str(), address_pair.second);
    }
  } else {
    SCU_LOG_TRACE(_logger, "Awaiting connection expired before it could be processed");
  }
}

std::shared_ptr<ScorpioUdpConnection> scorpio_utils::network::ScorpioUdp::get_connection(
  Ipv4 remote_ip,
  Port remote_port) {
  auto connection_iter = _connections.find({ remote_ip, remote_port });
  std::shared_ptr<ScorpioUdpConnection> ans;
  if (connection_iter == _connections.end()) {
    while (auto connection_opt = _awaiting_connections_channel.receive()) {
      if (auto connection = connection_opt->lock()) {
        pull_awaiting_connections(connection);
        if (connection->remote_ip() == remote_ip && connection->remote_port() == remote_port) {
          ans = connection;
          break;
        }
      }
    }
    connection_iter = _connections.find({ remote_ip, remote_port });
    if (connection_iter == _connections.end()) {
      SCU_LOG_TRACE(_logger, "No connection found for {}:{}", remote_ip.str(), remote_port);
      return std::shared_ptr<ScorpioUdpConnection>(nullptr);
    }
  } else {
    ans = connection_iter->second.lock();
    if (!ans) {
      _connections.erase(connection_iter);
      return std::shared_ptr<ScorpioUdpConnection>(nullptr);
    }
  }
  if (!ans->is_alive()) {
    _connections.erase(connection_iter);
    return std::shared_ptr<ScorpioUdpConnection>(nullptr);
  }
  return ans;
}

SCU_HOT std::optional<std::pair<size_t, std::vector<std::vector<uint8_t>>>> generate_packets(
  std::optional<scorpio_utils::network::StreamNumber> stream_number,
  std::atomic<size_t>& sequence_number,
  scorpio_utils::network::Code code,
  const std::vector<uint8_t>& data,
  const std::optional<StreamEpoch> stream_epoch) {
  using scorpio_utils::network::host_to_network;
  const auto header_without_frames_left_size = calculate_header_without_frames_left_size(code) +
    (sizeof(StreamEpoch) * SCU_AS(size_t, stream_epoch.has_value()));
  const auto packets_to_send = packets_count(data.size(), header_without_frames_left_size);
  if (SCU_UNLIKELY(packets_to_send > 65537)) {
    // SCU_LOG_ERROR(_logger, "Data is too large to send: {} bytes, max is {} bytes",
    // packets_to_send, 65537);
    return std::nullopt;
  }
  std::vector<std::vector<uint8_t>> packets;
  packets.reserve(packets_to_send);
  SCU_ASSERT(code.is_command_for_stream() == stream_number.has_value(),
    "Stream number must be provided for stream command and must not be provided for non-stream command");
  const auto first_packet_seq =
    SCU_AS(SeqNumber, sequence_number.fetch_add(packets_to_send, std::memory_order_relaxed));
  auto packet_seq = first_packet_seq;
  size_t packet_pos = 0;
  packets.emplace_back();
  auto generate_header =
    [&packets, &packet_pos, &packet_seq, stream_number, code, stream_epoch, first = true](
    bool last = false) mutable -> void {
      auto code_v = (last ? (code & ~Code(Code::NOT_LAST)) : code | Code(Code::NOT_LAST));
      if (first) {
        code_v = code_v | Code(Code::FIRST);
        first = false;
      }
      SCU_DO_AND_ASSERT(host_to_network(code_v, packets.back(), packet_pos),
      "Failed to convert code to network format");
      if (code.is_command_for_stream()) {
        SCU_DO_AND_ASSERT(host_to_network(
        *stream_number, packets.back(), packet_pos), "Failed to convert stream number to network format");
        SCU_DO_AND_ASSERT(host_to_network(packet_seq++, packets.back(), packet_pos),
          "Failed to convert sequence number to network format");
      }
    };
  const auto packet_size_without_frames_left = SCU_UDP_MAX_PACKET_SIZE - header_without_frames_left_size;
  const auto packet_size_with_frames_left = packet_size_without_frames_left - sizeof(FramesLeft);
  size_t current = 0;
  while (data.size() - current > packet_size_without_frames_left) {
    packets.back().resize(SCU_UDP_MAX_PACKET_SIZE);
    generate_header();
    size_t frames_left = (data.size() - current - packet_size_without_frames_left) / packet_size_with_frames_left;
    if (!host_to_network(static_cast<decltype(MessageHeader::frames_left)::value_type>(frames_left), packets.back(),
      packet_pos)) {
      // panic("Failed to convert frames left to network format");
      return std::nullopt;
    }
    if (stream_epoch.has_value()) {
      SCU_DO_AND_ASSERT(host_to_network(*stream_epoch, packets.back(), packet_pos),
            "Failed to convert stream epoch to network format");
    }
    SCU_ASSERT(packet_pos + packet_size_with_frames_left == packets.back().size(),
      "Packet size miscalculation: " << packet_pos << " + " << packet_size_with_frames_left << " == " <<
      packets.back().size());
    std::memcpy(packets.back().data() + packet_pos, data.data() + current, packet_size_with_frames_left);
    packets.emplace_back();
    current += packet_size_with_frames_left;
    packet_pos = 0;
  }
  packets.back().resize(header_without_frames_left_size + (data.size() - current));
  generate_header(true);
  if (stream_epoch.has_value()) {
    SCU_DO_AND_ASSERT(host_to_network(*stream_epoch, packets.back(), packet_pos),
            "Failed to convert stream epoch to network format");
  }
  std::memcpy(packets.back().data() + packet_pos, data.data() + current, data.size() - current);
  return { { first_packet_seq, packets } };
}

SendOutcome ScorpioUdp::send(
  std::optional<StreamNumber> stream_number,
  std::atomic<size_t>& sequence_number,
  Code code,
  Ipv4 remote_ip,
  Port remote_port,
  const std::vector<uint8_t>& data,
  bool block
) {
  auto packets = generate_packets(stream_number, sequence_number, code, data, std::nullopt);
  if (SCU_UNLIKELY(!packets.has_value())) {
    return SendOutcome::CLOSED;
  }
  for (auto& packet : packets->second) {
    const auto outcome = send_impl(remote_ip, remote_port, std::move(packet), block);
    if (SCU_UNLIKELY(outcome != SendOutcome::SENT)) {
      if (outcome == SendOutcome::CLOSED) {
        SCU_LOG_ERROR(_logger, "Failed to send packet to {}:{}", remote_ip.str(), remote_port);
      }
      return outcome;
    }
  }
  return SendOutcome::SENT;
}

void ScorpioUdp::send_or_panic(
  std::optional<StreamNumber> stream_number,
  std::atomic<size_t>& sequence_number,
  Code code,
  Ipv4 remote_ip,
  Port remote_port,
  const std::vector<uint8_t>& data,
  std::string&& panic_message) {
  switch (send(stream_number, sequence_number, code, remote_ip, remote_port, data, false)) {
    case SendOutcome::SENT:
      break;
    case SendOutcome::FULL:
      // Deliberate drop: the peer retries these replies, and blocking the socket
      // processing thread would stall packet routing for every connection.
      SCU_LOG_WARNING(_logger, "Sender queue full - dropped reply to {}:{}", remote_ip.str(), remote_port);
      break;
    case SendOutcome::CLOSED:
      panic(std::move(panic_message));
  }
}

std::shared_ptr<ScorpioUdpConnection> ScorpioUdp::connect(
  Ipv4 ip,
  Port port) {
  std::shared_ptr<ScorpioUdpConnection> connection(new ScorpioUdpConnection(ip, port, get_random_number(), true,
    shared_from_this()));
  connection->_start_signal.notify(100000);
  _awaiting_connections_channel.send<true>(connection);
  return connection;
}

// ========================= ScorpioUdpConnection implementation =======================

ScorpioUdpConnection::ScorpioUdpConnection(
  Ipv4 remote_ip, Port remote_port, ConnectionId connection_id, bool is_dialer,
  std::shared_ptr<ScorpioUdp> parent)
: _remote_ip(remote_ip),
  _remote_port(remote_port),
  _connection_id{connection_id},
  _is_dialer{is_dialer},
  _sequence_number(0),
  _panic(false),
  _state(State::NEW),
  _parent(std::move(parent)),
  _auto_accept_stream(false),
  _stop(false),
  _time_provider(_parent->_time_provider),
  _last_received_packet_time(_time_provider->get_time()),
  _received_packet_count{0},
  _received_heartbeat_count{0},
  _send_heartbeat_count{0},
  _send_partial_heartbeat_count{0},
  _logger(_parent->_logger),
  _retransmission_count(0),
  _received_bytes(0),
  _send_bytes(0),
  _panic_count(0),
  _last_panic_time(0),
  _next_stream_to_heartbeat(0),
  _stream_exists{false},
  _streams_mask_level_2{0},
  _streams_mask{0},
  _processing_thread(&ScorpioUdpConnection::processing_thread, this) {
  // The processing thread is blocked on _start_signal (notified by the creator
  // after construction), so filling the epoch table here is not racy.
  static_assert(max_streams_count % sizeof(uint64_t) == 0, "Epoch seeding assumes 8-epoch chunks");
  for (size_t i = 0; i < max_streams_count; i += sizeof(uint64_t)) {
    const auto random_value = SCU_AS(uint64_t, _parent->get_random_number());
    for (size_t j = 0; j < sizeof(uint64_t); ++j) {
      _streams_epoch[i + j].store(SCU_AS(StreamEpoch, random_value >> (j * 8)), std::memory_order_relaxed);
    }
  }
}

bool ScorpioUdpConnection::connected() {
  SCU_LOG_INFO(_logger, "Connection {}:{} is now connected", _remote_ip.str(), _remote_port);
  State expected = State::CONNECTING;
  return _state.compare_exchange_strong(
    expected,
    State::CONNECTED,
    std::memory_order_relaxed,
    std::memory_order_relaxed);
}

std::shared_ptr<ScorpioUdpStream> ScorpioUdpConnection::get_stream(StreamNumber stream_number) {
  auto stream = _streams[stream_number].lock();
  if (!stream || !stream->is_alive()) {
    _streams[stream_number].reset();
    return std::shared_ptr<ScorpioUdpStream>();
  }
  return stream;
}

void ScorpioUdpConnection::create_stream_packet_handler(const MessageHeader& header, UdpData&& data) {
  SCU_LOG_TRACE(_logger, "Received CREATE_STREAM packet from {}:{}", _remote_ip.str(), _remote_port);
  if (data.data.size() - header.data_offset < 1) {
    // TODO(@Igor): Handle error properly
    SCU_LOG_ERROR(_logger,
      "Invalid CREATE_STREAM packet size: expected at least 1 byte for subcommand, got {}",
      data.data.size() - header.data_offset);
    return;
  }
  size_t offset = header.data_offset;
  const auto subcommand = SCU_AS(Code::CreateStreamSubCommands, data.data[offset++]);
  ConnectionId connection_id;
  if (!network_to_host(data.data, &connection_id, offset)) {
    // TODO(@Igor): Handle error properly
    SCU_LOG_ERROR(_logger, "Failed to parse connection_id from CREATE_STREAM packet");
    return;
  }
  if (connection_id != _connection_id) {
    SCU_LOG_ERROR(_logger,
      "Dropping CREATE_STREAM packet with mismatched connection_id. ip: {}, port: {}, "
      "existing connection_id: {}, received connection_id: {}",
      _remote_ip.str(), _remote_port, _connection_id, connection_id);
    return;
  }
  // Liveness is refreshed only after the connection id has been validated, so a
  // stale connection incarnation cannot keep this connection alive forever.
  _last_received_packet_time.store(_time_provider->get_time(), std::memory_order_relaxed);
  switch (subcommand) {
    case Code::CreateStreamSubCommands::CREATE: {
        std::optional<Code::CreateStreamSubCommands> response_code;
        StreamNumber stream_number;
        if (!network_to_host(data.data, &stream_number, offset)) {
          // TODO(@Igor): Handle error properly
          SCU_LOG_ERROR(_logger, "Failed to parse CREATE_STREAM stream number");
          return;
        }
        StreamEpoch stream_epoch;
        if (!network_to_host(data.data, &stream_epoch, offset)) {
          // TODO(@Igor): Handle error properly
          SCU_LOG_WARNING(_logger, "Failed to parse CREATE_STREAM stream epoch");
          return;
        }
        auto qos_opt = parse_qos(data.data, offset);
        if (!qos_opt) {
          // TODO(@Igor): Handle error properly
          SCU_LOG_ERROR(_logger, "Failed to parse CREATE_STREAM QoS");
          return;
        }
        bool expected = false;
        if (auto stream = get_stream(stream_number)) {
          if (stream->qos() != *qos_opt) {
            stream->panic("Peer tried to create stream with existing stream number but different QoS");
            response_code = Code::CreateStreamSubCommands::REJECT_SIMILAR_EXISTED;
          } else if (stream->_stream_epoch == stream_epoch) {
            if (stream->state() == ScorpioUdpStream::State::CREATING) {
              stream->connected();
            }
            response_code = Code::CreateStreamSubCommands::ALREADY_EXISTS;
          } else if (stream->_created_by_peer) {
            // The peer created this stream and now announces a new incarnation
            // (different epoch): the local copy is stale. Panic it and send nothing;
            // the peer retries CREATE every heartbeat and succeeds once the app
            // releases the stale stream and the slot frees up.
            SCU_LOG_WARNING(_logger,
              "Stream {} replaced by a newer incarnation from peer (epoch {} -> {})",
              stream_number, stream->_stream_epoch, stream_epoch);
            stream->panic("Replaced by a newer stream incarnation from peer");
          } else if (!_is_dialer) {
            // Cross-create collision on a locally created stream: deterministic
            // tie-break, the dialer's stream wins on both sides, so this (acceptor)
            // side yields. No response; the peer's CREATE retry proceeds once the
            // slot frees up.
            SCU_LOG_WARNING(_logger,
              "Stream {} cross-create collision, yielding to the dialer peer (epoch {} vs {})",
              stream_number, stream->_stream_epoch, stream_epoch);
            stream->panic("Stream number collision, yielding to the dialer peer");
          } else {
            // Dialer side of a cross-create collision: the local stream wins and the
            // peer yields by the same rule, so just ignore the packet.
            SCU_LOG_DEBUG(_logger,
              "Ignoring colliding CREATE for stream {} (dialer wins the tie-break)", stream_number);
          }
        } else if (!(_auto_accept_stream.load(std::memory_order_relaxed) && qos_opt->is_supported())) {
          response_code = Code::CreateStreamSubCommands::REJECT;
        } else if (_stream_exists[stream_number].compare_exchange_strong(
            expected,
            true,
            std::memory_order_relaxed,
            std::memory_order_relaxed)) {
          _streams_epoch[stream_number].store(stream_epoch, std::memory_order_relaxed);
          std::shared_ptr<ScorpioUdpStream> new_stream(new ScorpioUdpStream(
                stream_number, stream_epoch, *qos_opt, true, shared_from_this()));
          new_stream->_state.store(ScorpioUdpStream::State::CREATING, std::memory_order_relaxed);
          new_stream->connected();
          new_stream->activate_stream();
          _new_streams.send<true>(std::move(new_stream));
          response_code = Code::CreateStreamSubCommands::ACCEPT;
        } else {
          // The slot is occupied by a dead-but-still-referenced stream, or claimed by
          // a local create_stream() that has not been activated yet. Transient: drop
          // the packet, the peer retries every heartbeat and gives up after its
          // creation timeout. Never wait for the slot here - this runs on the
          // connection processing thread and spinning would freeze the connection.
          SCU_LOG_DEBUG(_logger,
            "Dropping CREATE for stream {}: slot is busy but there is no live stream", stream_number);
        }
        if (response_code.has_value()) {
          std::vector<uint8_t> response;
          response.reserve(offset - header.data_offset);
          response.push_back(AS_BYTE(*response_code));
          std::ignore =
            std::copy(data.data.begin() + SCU_AS(int64_t, header.data_offset) + sizeof(*response_code),
          data.data.begin() + SCU_AS(int64_t, offset),
            std::back_inserter(response));
          send_or_panic(Code::CREATE_STREAM, response);
        }
      } break;
    case Code::CreateStreamSubCommands::ACCEPT: {
        StreamNumber stream_number;
        if (!network_to_host(data.data, &stream_number, offset)) {
          // TODO(@Igor): Handle error properly
          SCU_LOG_ERROR(_logger, "Failed to parse stream number from ACCEPT CREATE_STREAM packet");
          return;
        }
        auto stream = _streams[stream_number].lock();
        if (!stream) {
          // TODO(@Igor): Handle error properly
          SCU_LOG_WARNING(_logger, "Received ACCEPT for non-existing stream number {} - ignoring", stream_number);
          return;
        }
        if (!stream->is_alive()) {
          // TODO(@Igor): Handle error properly
          SCU_LOG_WARNING(_logger, "Received ACCEPT for stream number {} not in CREATING state - ignoring",
                          stream_number);
          return;
        }
        StreamEpoch stream_epoch;
        if (!network_to_host(data.data, &stream_epoch, offset)) {
          // TODO(@Igor): Handle error properly
          SCU_LOG_WARNING(_logger, "Failed to parse CREATE_STREAM stream epoch");
          return;
        }
        auto qos_opt = parse_qos(data.data, offset);
        if (!qos_opt) {
          // TODO(@Igor): Handle error properly
          SCU_LOG_WARNING(_logger,
                          "Failed to parse QoS from ACCEPT CREATE_STREAM packet for stream number {} - ignoring",
            stream_number);
          return;
        }
        if (*qos_opt != stream->qos()) {
          // TODO(@Igor): Handle error properly
          stream->panic("Peer accepted stream with different QoS");
          SCU_LOG_WARNING(_logger,
                        "Received ACCEPT for stream number {} with different QoS. "
                        "Expected reliability: {}, got: {}. Expected depth: {}, got: {} - ignoring",
            stream_number, stream->qos().reliability, qos_opt->reliability,
            stream->qos().depth, qos_opt->depth);
          return;
        }
        if (stream->_stream_epoch != stream_epoch) {
          // TODO(@Igor): Handle error properly
          stream->panic("Peer accepted stream with different epoch");
          SCU_LOG_WARNING(_logger,
                        "Received ACCEPT for stream number {} with different epoch. "
                        "Expected epoch: {}, got: {} - ignoring",
            stream_number, stream->_stream_epoch, stream_epoch);
          return;
        }
        stream->connected();
      } break;
    case Code::CreateStreamSubCommands::REJECT_SIMILAR_EXISTED: {
        StreamNumber stream_number;
        if (!network_to_host(data.data, &stream_number, offset)) {
          // TODO(@Igor): Handle error properly
          SCU_LOG_ERROR(_logger, "Failed to parse stream number from REJECT_SIMILAR_EXISTED CREATE_STREAM packet");
          return;
        }
        StreamEpoch stream_epoch;
        if (!network_to_host(data.data, &stream_epoch, offset)) {
          // TODO(@Igor): Handle error properly
          SCU_LOG_WARNING(_logger, "Failed to parse CREATE_STREAM stream epoch");
          return;
        }
        auto qos_opt = parse_qos(data.data, offset);
        if (!qos_opt) {
          // TODO(@Igor): Handle error properly
          SCU_LOG_ERROR(_logger,
                        "Failed to parse QoS from REJECT_SIMILAR_EXISTED CREATE_STREAM packet for stream number {}",
                      stream_number);
          return;
        }
        if (auto stream = _streams[stream_number].lock()) {
          if (!stream->is_alive()) {
            SCU_LOG_ERROR(_logger, "Received REJECT_SIMILAR_EXISTED for stream number {} not in CREATING state",
                          stream_number);
            return;
          }
          if (stream->_stream_epoch != stream_epoch) {
            SCU_LOG_ERROR(_logger,
                          "Received REJECT_SIMILAR_EXISTED for stream number {} with different epoch. "
                          "Expected epoch: {}, got: {}",
              stream_number, stream->_stream_epoch, stream_epoch);
            return;
          }
          if (*qos_opt != stream->qos()) {
            SCU_LOG_ERROR(_logger,
                          "Received REJECT_SIMILAR_EXISTED for stream number {} with different QoS. "
                          "Expected reliability: {}, got: {}. Expected depth: {}, got: {}",
              stream_number, stream->qos().reliability, qos_opt->reliability,
              stream->qos().depth, qos_opt->depth);
            return;
          }
          stream->panic("Peer rejected stream creation due to similar existing stream");
        } else {
          SCU_LOG_WARNING(_logger, "Received REJECT_SIMILAR_EXISTED for non-existing stream - ignoring");
        }
      } break;
    case Code::CreateStreamSubCommands::ALREADY_EXISTS: {
        StreamNumber stream_number;
        if (!network_to_host(data.data, &stream_number, offset)) {
          // TODO(@Igor): Handle error properly
          SCU_LOG_ERROR(_logger, "Failed to parse stream number from ALREADY_EXISTS CREATE_STREAM packet");
          return;
        }
        StreamEpoch stream_epoch;
        if (!network_to_host(data.data, &stream_epoch, offset)) {
          // TODO(@Igor): Handle error properly
          SCU_LOG_WARNING(_logger, "Failed to parse CREATE_STREAM stream epoch");
          return;
        }
        auto qos_opt = parse_qos(data.data, offset);
        if (!qos_opt) {
          // TODO(@Igor): Handle error properly
          SCU_LOG_ERROR(_logger, "Failed to parse QoS from ALREADY_EXISTS CREATE_STREAM packet for stream number {}",
                        stream_number);
          return;
        }
        if (auto stream = _streams[stream_number].lock()) {
          if (!stream->is_alive()) {
            SCU_LOG_ERROR(_logger, "Received ALREADY_EXISTS for stream number {} not in CREATING state", stream_number);
            return;
          }
          if (*qos_opt != stream->qos()) {
            SCU_LOG_ERROR(_logger,
                          "Received ALREADY_EXISTS for stream number {} with different QoS. "
                          "Expected reliability: {}, got: {}. Expected depth: {}, got: {}",
              stream_number, stream->qos().reliability, qos_opt->reliability,
              stream->qos().depth, qos_opt->depth);
            return;
          }
          if (stream->_stream_epoch != stream_epoch) {
            SCU_LOG_ERROR(_logger,
                          "Received ALREADY_EXISTS for stream number {} with different epoch. "
                          "Expected epoch: {}, got: {}",
              stream_number, stream->_stream_epoch, stream_epoch);
            return;
          }
          if (stream->state() == ScorpioUdpStream::State::CREATING) {
            stream->connected();
          }
        } else {
          SCU_LOG_ERROR(_logger, "Received ALREADY_EXISTS for non-existing stream number {}", stream_number);
        }
      } break;
    default: {
        SCU_LOG_ERROR(_logger, "Received unknown CREATE_STREAM packet with subcommand: {}", subcommand);
        [[fallthrough]];
      }
    case Code::CreateStreamSubCommands::REJECT: {
        StreamNumber stream_number;
        if (!network_to_host(data.data, &stream_number, offset)) {
          SCU_LOG_ERROR(_logger, "Failed to parse stream number from REJECT CREATE_STREAM packet");
          return;
        }
        auto stream = _streams[stream_number].lock();
        if (!stream) {
          SCU_LOG_ERROR(_logger, "Received REJECT for non-existing stream number {}", stream_number);
          return;
        }
        if (!stream->is_alive()) {
          SCU_LOG_ERROR(_logger, "Received REJECT for stream number {} not in CREATING state", stream_number);
          return;
        }
        StreamEpoch stream_epoch;
        if (!network_to_host(data.data, &stream_epoch, offset)) {
          // TODO(@Igor): Handle error properly
          SCU_LOG_WARNING(_logger, "Failed to parse CREATE_STREAM stream epoch");
          return;
        }
        auto qos_opt = parse_qos(data.data, offset);
        if (!qos_opt) {
          SCU_LOG_ERROR(_logger, "Failed to parse QoS from REJECT CREATE_STREAM packet for stream number {}",
            stream_number);
          return;
        }
        if (*qos_opt != stream->qos()) {
          SCU_LOG_ERROR(_logger,
                        "Received REJECT for stream number {} with different QoS. "
                        "Expected reliability: {}, got: {}. Expected depth: {}, got: {}",
            stream_number, stream->qos().reliability, qos_opt->reliability,
            stream->qos().depth, qos_opt->depth);
          return;
        }
        if (stream->_stream_epoch != stream_epoch) {
          SCU_LOG_ERROR(_logger,
                        "Received REJECT for stream number {} with different epoch. "
                        "Expected epoch: {}, got: {}",
            stream_number, stream->_stream_epoch, stream_epoch);
          return;
        }
        SCU_LOG_INFO(_logger, "Stream number {} was rejected by peer", stream_number);
        stream->_state.store(ScorpioUdpStream::State::REJECTED, std::memory_order_relaxed);
      } break;
  }
}

void ScorpioUdpConnection::close_stream_packet_handler(const MessageHeader& header, UdpData&& data) {
  if (data.data.size() - header.data_offset !=
    sizeof(Code::CloseStreamSubCommands) + sizeof(ConnectionId) + sizeof(StreamNumber) + sizeof(StreamEpoch)) {
    SCU_LOG_ERROR(_logger,
      "Invalid CLOSE_STREAM packet size: expected {} bytes, got {} bytes",
      sizeof(Code::CloseStreamSubCommands) + sizeof(ConnectionId) + sizeof(StreamNumber) + sizeof(StreamEpoch),
      data.data.size() - header.data_offset);
    return;
  }
  size_t offset = header.data_offset;
  const Code::CloseStreamSubCommands subcode = SCU_AS(Code::CloseStreamSubCommands, data.data[offset++]);
  ConnectionId connection_id;
  if (!network_to_host(data.data, &connection_id, offset)) {
    SCU_LOG_ERROR(_logger, "Failed to parse connection_id from CLOSE_STREAM packet");
    return;
  }
  if (connection_id != _connection_id) {
    SCU_LOG_ERROR(_logger,
      "Dropping CLOSE_STREAM packet with mismatched connection_id. ip: {}, port: {}, "
      "existing connection_id: {}, received connection_id: {}",
      _remote_ip.str(), _remote_port, _connection_id, connection_id);
    return;
  }
  // Liveness is refreshed only after the connection id has been validated.
  _last_received_packet_time.store(_time_provider->get_time(), std::memory_order_relaxed);
  StreamNumber stream_number;
  if (!network_to_host(data.data, &stream_number, offset)) {
    SCU_LOG_ERROR(_logger, "Failed to parse stream number from CLOSE_STREAM packet");
    return;
  }
  StreamEpoch stream_epoch;
  if (!network_to_host(data.data, &stream_epoch, offset)) {
    SCU_LOG_ERROR(_logger, "Failed to parse stream epoch from CLOSE_STREAM packet");
    return;
  }
  auto stream = get_stream(stream_number);
  const bool epoch_matches = stream && stream->_stream_epoch == stream_epoch;
  switch (subcode) {
    case Code::CloseStreamSubCommands::CLOSE: {
        Code::CloseStreamSubCommands response_code = Code::CloseStreamSubCommands::ALREADY_CLOSED;
        if (epoch_matches) {
          ScorpioUdpStream::State expected = stream->state();
          bool closed_now = false;
          while (stream->is_alive()) {
            if (stream->_state.compare_exchange_strong(
                expected,
                ScorpioUdpStream::State::CLOSED,
                std::memory_order_relaxed,
                std::memory_order_relaxed)) {
              closed_now = true;
            }
          }
          response_code = closed_now ? Code::CloseStreamSubCommands::CLOSED :
            Code::CloseStreamSubCommands::ALREADY_CLOSED;
        }
        std::vector<uint8_t> response;
        response.resize(sizeof(Code::CloseStreamSubCommands) + sizeof(ConnectionId) + sizeof(StreamNumber) +
          sizeof(StreamEpoch));
        response[0] = AS_BYTE(response_code);
        size_t response_offset = 1;
        SCU_DO_AND_ASSERT(host_to_network(connection_id, response, response_offset),
          "Failed to convert connection ID to network format for CLOSE_STREAM response");
        SCU_DO_AND_ASSERT(host_to_network<StreamNumber>(stream_number, response,
                                                 response_offset), "Failed to convert stream number to network format");
        SCU_DO_AND_ASSERT(host_to_network<StreamEpoch>(stream_epoch, response,
                                                 response_offset), "Failed to convert stream epoch to network format");
        send(Code::CLOSE_STREAM, response, stream_number, std::nullopt, false);
      } break;
    case Code::CloseStreamSubCommands::CLOSED: {
        if (epoch_matches) {
          std::ignore = stream->closed();
        }
      } break;
    case Code::CloseStreamSubCommands::ALREADY_CLOSED: {
        if (epoch_matches) {
          stream->_state.store(ScorpioUdpStream::State::CLOSING, std::memory_order_relaxed);
          SCU_DO_AND_ASSERT(stream->closed(), "Failed to close stream in response to ALREADY_CLOSED");
        }
      } break;
    default: {
        SCU_LOG_ERROR(_logger, "Received unknown CLOSE_STREAM packet with subcommand: {}",
          SCU_AS(Code::CloseStreamSubCommands, subcode));
      } break;
  }
}

void ScorpioUdpConnection::heartbeat_packet_handler(const MessageHeader& header, UdpData&& data) {
  size_t pos = header.data_offset;
  ConnectionId heartbeat_connection_id;
  if (SCU_UNLIKELY(!network_to_host(data.data, &heartbeat_connection_id, pos))) {
    SCU_LOG_ERROR(_logger, "Failed to parse connection_id from HEARTBEAT packet");
    return;
  }
  if (SCU_UNLIKELY(heartbeat_connection_id != _connection_id)) {
    SCU_LOG_WARNING(_logger, "Dropping HEARTBEAT with mismatched connection_id. ip: {}, port: {}, "
      "existing connection_id: {}, received connection_id: {}",
      _remote_ip.str(), _remote_port, _connection_id, heartbeat_connection_id);
    return;
  }
  StreamNumber stream_num;
  // Liveness is refreshed only after the connection id has been validated, so a
  // stale connection incarnation cannot keep this connection alive forever.
  _last_received_packet_time.store(_time_provider->get_time(), std::memory_order_relaxed);
  _last_received_heartbeat_time.store(_time_provider->get_time(), std::memory_order_relaxed);
  _received_heartbeat_count.fetch_add(1, std::memory_order_relaxed);
  while (network_to_host(data.data, &stream_num, pos)) {
    StreamEpoch stream_epoch;
    if (SCU_UNLIKELY(!network_to_host(data.data, &stream_epoch, pos))) {
      SCU_LOG_ERROR(_logger, "Malformed heartbeat packet: missing stream epoch for stream number {}", stream_num);
      break;
    }
    auto stream = get_stream(stream_num);
    if (stream && stream->_stream_epoch == stream_epoch) {
      stream->handle_heartbeat_data(data.data, pos);
    } else {
      SCU_LOG_DEBUG(_logger, "Received heartbeat data for non-existing stream number {} (or mismatched epoch {})",
        stream_num, stream_epoch);
      std::vector<uint8_t> response;
      response.resize(sizeof(Code::CloseStreamSubCommands) + sizeof(ConnectionId) + sizeof(StreamNumber) +
        sizeof(StreamEpoch));
      response[0] = AS_BYTE(Code::CloseStreamSubCommands::ALREADY_CLOSED);
      size_t response_offset = 1;
      SCU_DO_AND_ASSERT(host_to_network(_connection_id, response, response_offset),
        "Failed to convert connection ID to network format for CLOSE_STREAM ALREADY_CLOSED response");
      SCU_DO_AND_ASSERT(host_to_network<StreamNumber>(stream_num, response, response_offset),
        "Failed to convert stream number to network format for CLOSE_STREAM ALREADY_CLOSED response");
      // Echo the epoch from the heartbeat block: the reply must match the peer's
      // live stream incarnation or the peer will (correctly) ignore it.
      SCU_DO_AND_ASSERT(host_to_network<StreamEpoch>(stream_epoch, response, response_offset),
        "Failed to convert stream epoch to network format for CLOSE_STREAM ALREADY_CLOSED response");
      if (SCU_UNLIKELY(send(Code::CLOSE_STREAM, std::move(response), stream_num,
        _sequence_number, false) == SendOutcome::CLOSED)) {
        panic("Failed to send CLOSE_STREAM ALREADY_CLOSED response for non-existing stream");
      }
      if (SCU_UNLIKELY(pos >= data.data.size())) {
        SCU_LOG_ERROR(_logger,
                      "Malformed heartbeat packet: expected ranges byte for stream number {}, "
                      "but no more data available",
          stream_num);
        break;
      }
      const uint8_t ranges = data.data[pos++];
      pos += (SCU_AS(size_t, ranges) * 2 + 1) * sizeof(SeqNumber);
    }
  }
}

SCU_HOT void ScorpioUdpConnection::handle_new_packet(const MessageHeader& header, UdpData&& data) {
  switch (header.command) {
    case Code::CREATE_STREAM: {
        // Liveness refresh happens inside the handler, after connection id validation.
        create_stream_packet_handler(header, std::move(data));
      } break;
    case Code::STREAM_DATA: {
        auto stream = get_stream(header.stream_number.value());
        if (!stream) {
          // TODO(@Igor): Handle error properly
          SCU_LOG_WARNING(_logger, "Received STREAM_DATA packet for non-existing stream number {}",
            header.stream_number.value());
          return;
        }
        SCU_LOG_TRACE(_logger,
                      "Received STREAM_DATA packet for stream number {} with data size {} bytes. seq_number {}",
            header.stream_number.value(), data.data.size(), header.seq_number.value_or(32767));
        stream->handle_data_packet(header, std::move(data));
      } break;
    case Code::ERROR: {
        // SCU_UNIMPLEMENTED();
        panic("Received ERROR packet from peer");
      } break;
    case Code::HEARTBEAT: {
        // Liveness refresh happens inside the handler, after connection id validation.
        heartbeat_packet_handler(header, std::move(data));
      } break;
    case Code::STATUS: {
        // SCU_UNIMPLEMENTED();
      } break;
    case Code::CLOSE_STREAM: {
        close_stream_packet_handler(header, std::move(data));
      } break;
    default:
      SCU_LOG_ERROR(_logger, "Received packet with unknown command: {}", static_cast<CodeType>(header.command));
      return;
  }
}

void ScorpioUdpConnection::pull_awaiting_streams(std::shared_ptr<scorpio_utils::network::ScorpioUdpStream> stream) {
  auto current_weak = _streams[stream->_stream_number];
  auto current = current_weak.lock();
  if (current && current->is_alive()) {
    // Should be unreachable (_stream_exists guards the slot), but if the invariant
    // ever breaks, fail the new stream instead of killing the whole connection.
    SCU_LOG_ERROR(_logger, "Stream {} already exists while activating a new stream - failing the new stream",
      stream->_stream_number);
    stream->panic("Stream with the same stream number already exists");
  } else {
    stream->send_create_packet();
    stream->_state.store(ScorpioUdpStream::State::CREATING, std::memory_order_relaxed);
    stream->activate_stream();
  }
}

void ScorpioUdpConnection::process_packets(
  std::pair<scorpio_utils::network::MessageHeader,
  scorpio_utils::network::UdpData> packet) {
  SCU_LOG_TRACE(_logger, "Processing packet from {}:{}. Packet size: {} bytes",
      packet.second.ip.str(), packet.second.port, packet.second.data.size());
  _received_packet_count.fetch_add(1, std::memory_order_relaxed);
  _received_bytes.fetch_add(packet.second.data.size(), std::memory_order_relaxed);
  handle_new_packet(packet.first, std::move(packet.second));
}

void ScorpioUdpConnection::send_heartbeat() {
  const auto no_packet_timeout = _parent->no_packet_timeout();
  const auto time_since_last_packet = _time_provider->get_time() -
    _last_received_packet_time.load(std::memory_order_relaxed);
  if (SCU_UNLIKELY(time_since_last_packet > no_packet_timeout)) {
    panic(fmt::format("No packets received for {} ms", no_packet_timeout / 1'000'000));
  }
  for (size_t l2 = 0; l2 < _streams_mask_level_2.size(); ++l2) {
    if (!_streams_mask_level_2[l2]) {
      continue;
    }
    for (size_t l1 = 0; l1 < 64; ++l1) {
      const auto l1_idx = (l2 << 6) | l1;
      if (!_streams_mask[l1_idx]) {
        continue;
      }
      for (size_t i = 0; i < 64; ++i) {
        const auto idx = (l1_idx << 6) | i;
        if (auto stream = _streams[idx].lock()) {
          stream->update();
        }
      }
    }
  }
  std::vector<uint8_t> heartbeat_data;
  constexpr size_t packet_size = SCU_UDP_MAX_PACKET_SIZE - calculate_header_without_frames_left_size(Code::HEARTBEAT);
  heartbeat_data.reserve(packet_size);
  // Every heartbeat leads with the connection id so the peer can drop heartbeats
  // meant for a stale connection incarnation (mirrors CONNECT/CREATE_STREAM/...).
  heartbeat_data.resize(sizeof(ConnectionId));
  size_t connection_id_pos = 0;
  SCU_DO_AND_ASSERT(host_to_network(_connection_id, heartbeat_data, connection_id_pos),
    "Failed to convert connection id to network format for HEARTBEAT");
  const auto l2_offset = SCU_AS(size_t, _next_stream_to_heartbeat) >> 12;
  const auto l1_first_start = (SCU_AS(size_t, _next_stream_to_heartbeat) >> 6) & 63;
  const auto idx_first_start = SCU_AS(size_t, _next_stream_to_heartbeat) & 63;
  auto l1_start = l1_first_start;
  auto idx_start = idx_first_start;
  bool full = false;
  for (size_t l2 = 0; l2 < _streams_mask_level_2.size(); ++l2) {
    auto l2_idx = (l2 + l2_offset) % _streams_mask_level_2.size();
    if (!_streams_mask_level_2[l2_idx]) {
      continue;
    }
    for (size_t l1 = l1_start; l1 < 64; ++l1) {
      const auto l1_idx = (l2_idx << 6) | l1;
      if (!_streams_mask[l1_idx]) {
        continue;
      }
      for (size_t i = idx_start; i < 64; ++i) {
        const auto idx = (l1_idx << 6) | i;
        if (auto stream = _streams[idx].lock()) {
          if (stream->is_active() && !stream->append_heartbeat_data(heartbeat_data)) {
            full = true;
            _next_stream_to_heartbeat = SCU_AS(uint16_t, idx);
            l1 = 64;
            l2 = _streams_mask_level_2.size();
            break;
          }
        }
      }
      idx_start = 0;
    }
    l1_start = 0;
  }
  if (!full) {
    for (size_t l1 = 0; l1 <= l1_first_start && !full; ++l1) {
      const auto l1_idx = (l2_offset << 6) | l1;
      if (!_streams_mask[l1_idx]) {
        continue;
      }
      const size_t i_end = (l1 == l1_first_start) ? idx_first_start : SCU_AS(size_t, 64);
      for (size_t i = 0; i < i_end; ++i) {
        const auto idx = (l1_idx << 6) | i;
        if (auto stream = _streams[idx].lock()) {
          if (stream->is_active() && !stream->append_heartbeat_data(heartbeat_data)) {
            full = true;
            _next_stream_to_heartbeat = SCU_AS(uint16_t, idx);
            break;
          }
        }
      }
    }
  }
  switch (send(Code::HEARTBEAT, heartbeat_data, std::nullopt, std::nullopt, false)) {
    case SendOutcome::SENT:
      _send_heartbeat_count.fetch_add(1, std::memory_order_relaxed);
      if (full) {
        _send_partial_heartbeat_count.fetch_add(1, std::memory_order_relaxed);
      }
      break;
    case SendOutcome::FULL: {
        // Deliberate drop: the next heartbeat tick is one period away, while blocking
        // here would wedge the processing thread (it also drains incoming packets)
        // and turn TX backpressure into a false no-packet timeout.
        const auto skipped = _heartbeat_skip_count.fetch_add(1, std::memory_order_relaxed) + 1;
        if (skipped == 1 || skipped % 128 == 0) {
          SCU_LOG_WARNING(_logger, "Sender queue full - skipped heartbeat to {}:{} ({} skipped so far)",
            _remote_ip.str(), _remote_port, skipped);
        }
      } break;
    case SendOutcome::CLOSED:
      panic("Failed to send message in send");
  }
}

void ScorpioUdpConnection::processing_thread() {
  std::weak_ptr<ScorpioUdpConnection> self_weak;
  std::shared_ptr<ScorpioUdpConnection> self;
  // On every exit path: if the connection died abnormally (panic or rejection),
  // cascade the failure to all streams so none of them keeps looking usable.
  // When 'self' is empty the connection is being destructed - its close() joins
  // this thread and then deals with the streams itself.
  SCU_DEFER(([this, &self, &self_weak] {
      if (!self) {
        self = self_weak.lock();
      }
      if (!self) {
        // The connection is being (or already was) destructed - possibly on this
        // very thread via self.reset() - so 'this' must not be dereferenced at all.
        // Teardown joins this thread and then deals with the streams itself.
        return;
      }
      // CLOSED means close() is driving the shutdown and handles the streams after
      // joining this thread; CONNECTED without panic means the same (close() joins
      // first and only then stores CLOSED). Everything else is an abnormal death.
      const auto state = _state.load(std::memory_order_relaxed);
      if (_panic.load(std::memory_order_acquire) ||
      (state != State::CONNECTED && state != State::CLOSED)) {
        panic_streams();
      }
    }));
  try {
    _start_signal.wait();
    self_weak = weak_from_this();
    while (SCU_LIKELY((self = self_weak.lock()) && !_stop.load(std::memory_order_relaxed)) &&
      _state.load(std::memory_order_relaxed) == State::NEW) {
      if (SCU_UNLIKELY(!_parent->is_running())) {
        // The socket died before this connection was ever dispatched - nothing can
        // move it out of NEW anymore, so fail it instead of spinning forever.
        panic_soft("Socket stopped before the connection was established");
        return;
      }
      self.reset();
      std::this_thread::sleep_for(std::chrono::nanoseconds(SCU_UDP_HEARTBEAT_PERIOD / 4));
    }
    if (SCU_UNLIKELY((self = self_weak.lock()) == nullptr || _stop.load(std::memory_order_relaxed))) {
      return;
    }
    std::this_thread::sleep_for(std::chrono::nanoseconds(SCU_UDP_HEARTBEAT_PERIOD));
    {
      std::vector<uint8_t> connect_data;
      connect_data.resize(sizeof(Code::ConnectSubCommands) + sizeof(ConnectionId));
      connect_data[0] = AS_BYTE(Code::ConnectSubCommands::CONNECT);
      size_t offset = 1;
      SCU_DO_AND_ASSERT(host_to_network(_connection_id, connect_data, offset),
        "Failed to convert connection ID to network format");
      self.reset();
      while (SCU_LIKELY((self = self_weak.lock()) && !_stop.load(std::memory_order_relaxed)) &&
        _state.load(std::memory_order_relaxed) == State::CONNECTING) {
        send_or_panic(Code::CONNECT, connect_data);
        self.reset();
        std::this_thread::sleep_for(std::chrono::nanoseconds(SCU_UDP_HEARTBEAT_PERIOD));
      }
    }
    if (SCU_UNLIKELY((self = self_weak.lock()) == nullptr || _stop.load(std::memory_order_relaxed))) {
      return;
    }
    if (SCU_UNLIKELY(_state.load(std::memory_order_relaxed) != State::CONNECTED)) {
      // REJECTED (or otherwise dead) - do not run the heartbeat loop for a
      // connection that never got established; the deferred cleanup panics any
      // streams the application queued up in the meantime.
      SCU_LOG_INFO(_logger, "Connection {}:{} never became CONNECTED (state: {}), stopping",
        _remote_ip.str(), _remote_port, magic_enum::enum_name(_state.load(std::memory_order_relaxed)));
      return;
    }
    threading::EagerSelectTimeout timeout(SCU_UDP_HEARTBEAT_PERIOD, _time_provider);
    self.reset();
    timeout.start();
    while (SCU_LIKELY((self = self_weak.lock()) && !_stop.load(std::memory_order_relaxed))) {
      std::visit(VisitorOverloadingHelper{
        [this](std::pair<scorpio_utils::network::MessageHeader,
        scorpio_utils::network::UdpData> data) SCU_ALWAYS_INLINE_RAW {
          process_packets(std::move(data));
        },
        [this](std::shared_ptr<ScorpioUdpStream> stream) SCU_ALWAYS_INLINE_RAW {
          pull_awaiting_streams(std::move(stream));
        },
        [] (Empty) SCU_ALWAYS_INLINE_RAW { /* Timeout */ },
      }, threading::eager_select(_incoming_packets, _awaiting_streams, timeout));
      if (timeout.is_elapsed()) {
        send_heartbeat();
        timeout.reset();
        timeout.start();
      }
      self.reset();
    }
  } catch (const PanicException&) {
  } catch (const threading::ClosedChannelException&) {
  }
}

void ScorpioUdpConnection::panic_streams() {
  // Runs on the connection processing thread (or after it has been joined), so
  // touching _streams is safe. No stream may outlive its connection in a
  // usable-looking state - the application has to see the failure and rebuild.
  const std::string reason = _panic.load(std::memory_order_acquire) ?
    "Connection panicked: " + _panic_message :
    "Connection is dead (state: " + std::string(magic_enum::enum_name(state())) + ")";
  for (size_t l2 = 0; l2 < _streams_mask_level_2.size(); ++l2) {
    if (!_streams_mask_level_2[l2]) {
      continue;
    }
    for (size_t l1 = 0; l1 < 64; ++l1) {
      const auto l1_idx = (l2 << 6) | l1;
      if (!_streams_mask[l1_idx]) {
        continue;
      }
      for (size_t i = 0; i < 64; ++i) {
        const auto idx = (l1_idx << 6) | i;
        if (auto stream = _streams[idx].lock()) {
          stream->panic(clone(reason));
        }
      }
    }
  }
  // Streams still queued for creation were never activated and would otherwise
  // stay NEW forever with nothing driving them.
  try {
    while (auto stream_opt = _awaiting_streams.receive()) {
      (*stream_opt)->panic(clone(reason));
    }
  } catch (const threading::ClosedChannelException&) {
  }
}

SCU_COLD void ScorpioUdpConnection::panic_soft(std::string&& message) {
  std::unique_lock<std::mutex> lock(_panic_mutex, std::try_to_lock);
  if (!lock.owns_lock()) {
    // No need for waiting since panic_soft shall be called from the socket thread
    return;
  }
  SCU_LOG_FATAL(_logger, "Connection panic: {}", message);
  _panic_message = std::move(message);
  _panic_count.fetch_add(1, std::memory_order_relaxed);
  _last_panic_time.store(_time_provider->get_time(), std::memory_order_relaxed);
  _panic.store(true, std::memory_order_release);
  _state.store(State::ERROR, std::memory_order_relaxed);
  _stop.store(true, std::memory_order_relaxed);
}

SCU_COLD SCU_NORETURN void ScorpioUdpConnection::panic(std::string&& message) {
  std::unique_lock<std::mutex> lock(_panic_mutex, std::try_to_lock);
  if (!lock.owns_lock()) {
    // Lock mutex to delay returning from panic so, there is _panic_message set
    lock.lock();
    throw PanicException();
  }
  SCU_LOG_FATAL(_logger, "Connection panic: {}", message);
  _panic_message = std::move(message);
  _panic_count.fetch_add(1, std::memory_order_relaxed);
  _last_panic_time.store(_time_provider->get_time(), std::memory_order_relaxed);
  _panic.store(true, std::memory_order_release);
  _state.store(State::ERROR, std::memory_order_relaxed);
  _stop.store(true, std::memory_order_relaxed);
  throw PanicException();
}

auto ScorpioUdpConnection::generate_packets(
  Code code, const std::vector<uint8_t>& data, std::optional<StreamNumber> stream_number,
  std::optional<std::reference_wrapper<std::atomic<size_t>>> sequence_number,
  std::optional<StreamEpoch> stream_epoch) {
  return ::generate_packets(
        stream_number,
        sequence_number ? sequence_number->get() : _sequence_number,
        code,
        data,
        stream_epoch);
}

SendOutcome ScorpioUdpConnection::send(
  Code code, const std::vector<uint8_t>& data, std::optional<StreamNumber> stream_number,
  std::optional<std::reference_wrapper<std::atomic<size_t>>> sequence_number, bool block) {
  if (SCU_UNLIKELY(!_parent->is_running())) {
    return SendOutcome::CLOSED;
  }
  const auto outcome = _parent->send(stream_number,
    sequence_number.value_or(std::ref(_sequence_number)).get(),
    code, _remote_ip, _remote_port, data, block);
  if (SCU_UNLIKELY(outcome != SendOutcome::SENT)) {
    if (outcome == SendOutcome::CLOSED) {
      SCU_LOG_ERROR(_logger, "Failed to send packet to {}:{}", _remote_ip.str(), _remote_port);
    }
    return outcome;
  }
  _send_bytes.fetch_add(data.size(), std::memory_order_relaxed);
  return SendOutcome::SENT;
}

SendOutcome ScorpioUdpConnection::send(std::vector<uint8_t>&& packet, bool block) {
  if (SCU_UNLIKELY(!_parent->is_running())) {
    SCU_LOG_ERROR(_logger, "Failed to send packet because parent socket is not running");
    return SendOutcome::CLOSED;
  }
  const auto packet_size = packet.size();
  const auto outcome = _parent->send_impl(_remote_ip, _remote_port, std::move(packet), block);
  if (SCU_UNLIKELY(outcome != SendOutcome::SENT)) {
    if (outcome == SendOutcome::CLOSED) {
      SCU_LOG_ERROR(_logger, "Failed to send packet to {}:{}", _remote_ip.str(), _remote_port);
    }
    return outcome;
  }
  _send_bytes.fetch_add(packet_size, std::memory_order_relaxed);
  return SendOutcome::SENT;
}

void ScorpioUdpConnection::send_or_panic(
  Code code, const std::vector<uint8_t>& data, std::string&& message) {
  switch (send(code, data, std::nullopt, std::nullopt, false)) {
    case SendOutcome::SENT:
      break;
    case SendOutcome::FULL:
      // Deliberate drop: every caller is a control message retried on the next
      // heartbeat, and blocking here would wedge the connection processing
      // thread, turning TX backpressure into a false no-packet timeout.
      SCU_LOG_WARNING(_logger, "Sender queue full - dropped control packet to {}:{}",
        _remote_ip.str(), _remote_port);
      break;
    case SendOutcome::CLOSED:
      panic(std::move(message));
  }
}

bool ScorpioUdpConnection::close(bool send_disconnect) {
  std::unique_lock<std::mutex> lock(_close_mutex, std::try_to_lock);
  if (!lock.owns_lock()) {
    // Another thread is already closing the connection
    // Lock mutex to delay returning from panic so, there is _panic_message set
    lock.lock();
    return false;
  }
  SCU_LOG_INFO(_logger, "Closing connection {}:{}", _remote_ip.str(), _remote_port);
  // Stop the processing thread before touching _streams: the weak_ptr objects in
  // it may only be accessed by that thread or after it has been joined.
  _stop.store(true, std::memory_order_relaxed);
  _new_streams.close();
  _awaiting_streams.close();
  _incoming_packets.close();
  if (_processing_thread.get_id() == std::this_thread::get_id()) {
    _processing_thread.detach();
  } else if (_processing_thread.joinable()) {
    _processing_thread.join();
  }
  for (size_t l2 = 0; l2 < _streams_mask_level_2.size(); ++l2) {
    if (!_streams_mask_level_2[l2]) {
      continue;
    }
    for (size_t l1 = 0; l1 < 64; ++l1) {
      const auto l1_idx = (l2 << 6) | l1;
      if (!_streams_mask[l1_idx]) {
        continue;
      }
      for (size_t i = 0; i < 64; ++i) {
        const auto idx = (l1_idx << 6) | i;
        if (auto stream = _streams[idx].lock()) {
          stream->close();
        }
      }
    }
  }
  if (send_disconnect) {
    std::vector<uint8_t> disconnect_data;
    disconnect_data.resize(sizeof(Code::DisconnectSubCommands) + sizeof(ConnectionId));
    disconnect_data[0] = AS_BYTE(Code::DisconnectSubCommands::DISCONNECT);
    size_t offset = 1;
    SCU_DO_AND_ASSERT(host_to_network(_connection_id, disconnect_data, offset),
      "Failed to convert connection ID to network format for DISCONNECT packet");
    send(Code::DISCONNECT, disconnect_data, std::nullopt, std::nullopt);
  }
  _state.store(State::CLOSED, std::memory_order_relaxed);
  return true;
}

std::shared_ptr<ScorpioUdpStream> ScorpioUdpConnection::create_stream(
  StreamNumber stream_id,
  ScorpioUdpStream::StreamQoS qos) {
  SCU_ASSERT(qos.is_supported(), "UNRELIABLE_LATEST_ONLY and RELIABLE_UNORDERED streams are not supported");
  if (SCU_UNLIKELY(!_parent->is_running())) {
    return std::shared_ptr<ScorpioUdpStream>();
  }
  bool expected = false;
  if (!_stream_exists[stream_id].compare_exchange_strong(
    expected,
    true,
    std::memory_order_relaxed,
    std::memory_order_relaxed
    )) {
    return std::shared_ptr<ScorpioUdpStream>();
  }
  if (!qos.is_reliable() && qos.depth != 0) {
    qos.depth = 0;
  }
  std::shared_ptr<ScorpioUdpStream> stream(new ScorpioUdpStream(stream_id,
    _streams_epoch[stream_id].fetch_add(1, std::memory_order_relaxed), qos, false,
    shared_from_this()));
  _awaiting_streams.send<true>(stream);
  return stream;
}

// ========================= ScorpioUdpStream implementation ===========================

ScorpioUdpStream::ScorpioUdpStream(
  StreamNumber stream_number, StreamEpoch stream_epoch, StreamQoS stream_qos,
  bool created_by_peer, std::shared_ptr<ScorpioUdpConnection> parent)
: _stream_number(stream_number),
  _stream_epoch(stream_epoch),
  _stream_qos(stream_qos),
  _created_by_peer(created_by_peer),
  _creation_time(parent->_time_provider->get_time()),
  _sent_history(stream_qos.is_reliable() ? stream_qos.depth_value() + SCU_UDP_QOS_DEPTH_SAFETY_BUFFER : 0),
  _parent(std::move(parent)),
  _sequence_number(0),
  _least_non_delivered_seq_number(0),
  _state(State::NEW),
  _creation_tries(0),
  _orderer(stream_qos.depth_value() + SCU_UDP_QOS_DEPTH_SAFETY_BUFFER),
  _logger(_parent->_logger),
  _partial_data(std::in_place_index_t<0>{ }),
  _sequence_complement(0),
  _last_greatest_sequence_number(0),
  _stuck_resend_since(),
  _last_heartbeat_time(_parent->_time_provider->get_time()),
  _closing_since(0),
  _duplicate_count(0),
  _out_of_history_drop_count(0),
  _expired_unreliable_fragment_count(0),
  _panic_count(0),
  _last_panic_time(0) {
  SCU_ASSERT(stream_qos.is_reliable() || stream_qos.depth == 0, "Unreliable streams must have depth 0 (no ordering)");
  if (!stream_qos.is_reliable()) {
    _partial_data.emplace<1>();
  }
}

ScorpioUdpStream::~ScorpioUdpStream() {
  SCU_ASSERT(_parent->_stream_exists[_stream_number], "Stream existence was not claimed properly");
  close();
  const size_t mask_index = _stream_number >> 6;
  const size_t mask_position_mask = 1UL << (_stream_number & 63);
  const size_t mask_level2_index = mask_index >> 6;
  const size_t mask_level2_position_mask = 1UL << (mask_index & 63);
  {
    std::lock_guard lock(_parent->_streams_mask_write_mutex);
    uint64_t current_state = _parent->_streams_mask[mask_index].load(std::memory_order_relaxed);
    if ((current_state & ~mask_position_mask) == 0) {
      current_state = _parent->_streams_mask_level_2[mask_level2_index].load(std::memory_order_relaxed);
      do {
        SCU_ASSERT(current_state & mask_level2_position_mask,
         "Stream level 2 mask bit was already cleared on destruction");
      } while (!_parent->_streams_mask_level_2[mask_level2_index].compare_exchange_weak(current_state,
       current_state & ~mask_level2_position_mask,
         std::memory_order_relaxed,
         std::memory_order_relaxed));
      current_state = _parent->_streams_mask[mask_index].load(std::memory_order_relaxed);
    }
    do {
      SCU_ASSERT(current_state & mask_position_mask, "Stream mask bit was already cleared on destruction");
    } while (!_parent->_streams_mask[mask_index].compare_exchange_weak(current_state,
      current_state & ~mask_position_mask,
        std::memory_order_relaxed,
        std::memory_order_relaxed));
  }
  // Deliberately not touching _parent->_streams[_stream_number] here: destructors
  // run on arbitrary application threads and the weak_ptr objects in _streams may
  // only be accessed from the connection processing thread (get_stream cleans up
  // expired entries there). The expired weak_ptr left behind is harmless.
  bool expected = true;
  while (!_parent->_stream_exists[_stream_number].compare_exchange_weak(
    expected,
    false,
    std::memory_order_relaxed,
    std::memory_order_relaxed)) { }
}

bool ScorpioUdpStream::close() {
  State expected = state();
  while (state() <= State::CREATED) {
    // Stored before the CAS so the CLOSING state is never observable with a stale
    // (zero) timestamp - update() would time the stream out immediately otherwise.
    _closing_since.store(_parent->_time_provider->get_time(), std::memory_order_relaxed);
    if (_state.compare_exchange_strong(
        expected,
        State::CLOSING,
        std::memory_order_relaxed,
        std::memory_order_relaxed)) {
      if (!send_close_packet()) {
        panic("Failed to send CLOSE_STREAM packet (maybe connection or socket is closed?)");
        return false;
      }
      return true;
    }
  }
  return false;
}

SCU_HOT bool ScorpioUdpStream::send(Code code, const std::vector<uint8_t>& data, bool block) {
  if (SCU_UNLIKELY(!is_active() && !(state() == State::CLOSING && code == Code::CLOSE_STREAM))) {
    SCU_LOG_ERROR(_logger, "Attempted to send on inactive stream state: {}", magic_enum::enum_name(state()));
    return false;
  }
  if (SCU_UNLIKELY(!_parent->is_alive())) {
    SCU_LOG_ERROR(_logger, "Attempted to send on stream {} whose connection is dead", _stream_number);
    return false;
  }
  auto packets = _parent->generate_packets(
    code,
    data,
    _stream_number,
    _sequence_number,
    (_stream_qos.is_reliable() && code == Code::STREAM_DATA) ? std::optional<StreamEpoch>(_stream_epoch) : std::nullopt
  );
  if (SCU_UNLIKELY(!packets.has_value())) {
    SCU_LOG_ERROR(_logger, "Failed to generate packets for sending {} bytes on stream {}",
      data.size(), _stream_number);
    return false;
  }
  std::unique_lock lock(_sent_history_mutex, std::defer_lock);
  if (_stream_qos.is_reliable()) {
    lock.lock();
  }
  auto seq = packets->first;
  for (auto& packet : packets->second) {
    if (_stream_qos.is_reliable()) {
      const auto pos = seq % _sent_history.size();
      const auto least_non_delivered = _least_non_delivered_seq_number.load(std::memory_order_relaxed);
      if (seq - least_non_delivered >= _sent_history.size()) {
        SCU_LOG_ERROR(_logger,
                      "QoS depth exceeded for stream {}: least non-delivered seq {}, sent seq {}, history size {}",
          _stream_number, least_non_delivered, seq, _sent_history.size());
        panic("QoS depth exceeded " +
          std::to_string(least_non_delivered) + ", sent seq: " + std::to_string(seq));
        return false;
      }
      _sent_history[pos] = SentPacket{
        /*.data           = */ packet,
        /*.seq            = */ seq,
        /*.last_send_time = */ _parent->_time_provider->get_time(),
      };
    }
    SCU_LOG_TRACE(_logger, "Sending packet on stream {}: seq {} (packets left: {})", _stream_number, seq,
      parse_header(packet).ok_value().frames_left.value_or(32767));
    if (SCU_UNLIKELY(_parent->send(std::move(packet), block) != SendOutcome::SENT)) {
      return false;
    }
    ++seq;
  }
  std::atomic_thread_fence(std::memory_order_release);
  return true;
}

SCU_COLD void ScorpioUdpStream::panic(std::string&& message) {
  std::unique_lock<std::mutex> lock(_panic_mutex, std::try_to_lock);
  if (!lock.owns_lock()) {
    // Lock mutex to delay returning from panic so, there is _panic_message set
    lock.lock();
    return;
  }
  if (_state.load(std::memory_order_relaxed) == State::ERROR) {
    return;
  }
  SCU_LOG_FATAL(_logger, "Stream {} panic: {}", _stream_number, message);
  _panic_message = std::move(message);
  _panic_count.fetch_add(1, std::memory_order_relaxed);
  _last_panic_time.store(_parent->_time_provider->get_time(), std::memory_order_relaxed);
  _state.store(State::ERROR, std::memory_order_release);
}

void ScorpioUdpStream::send_create_packet() {
  std::vector<uint8_t> packet;
  constexpr auto minimal_size = sizeof(Code::CreateStreamSubCommands) + sizeof(ConnectionId) +
    sizeof(StreamNumber) + sizeof(StreamQoS::Reliability) + sizeof(StreamEpoch);
  packet.reserve(minimal_size + sizeof(StreamQoS::depth));
  packet.resize(minimal_size);
  packet[0] = AS_BYTE(Code::CreateStreamSubCommands::CREATE);
  size_t offset = 1;
  SCU_DO_AND_ASSERT(host_to_network(_parent->_connection_id, packet,
                                       offset), "Failed to convert connection ID to network format");
  SCU_DO_AND_ASSERT(host_to_network(_stream_number, packet,
                                       offset), "Failed to convert stream number to network format");
  SCU_DO_AND_ASSERT(host_to_network(_stream_epoch, packet,
                                       offset), "Failed to convert stream epoch to network format");
  serialize_qos(_stream_qos, packet, offset);
  _parent->send_or_panic(Code::CREATE_STREAM, packet, "Failed to send CREATE_STREAM command");
}

void ScorpioUdpStream::connected() {
  State expected = State::CREATING;
  if (_state.compare_exchange_strong(
    expected,
    State::CREATED,
    std::memory_order_relaxed,
    std::memory_order_relaxed)) {
    _last_heartbeat_time.store(_parent->_time_provider->get_time(), std::memory_order_relaxed);
  }
}

bool ScorpioUdpStream::closed() {
  State expected = State::CLOSING;
  if (_state.compare_exchange_strong(
    expected,
    State::CLOSED,
    std::memory_order_relaxed,
    std::memory_order_relaxed)) {
    return true;
  }
  return false;
}

bool ScorpioUdpStream::send_close_packet(bool block) {
  std::vector<uint8_t> packet;
  packet.resize(sizeof(Code::CloseStreamSubCommands) + sizeof(ConnectionId) + sizeof(StreamNumber) +
    sizeof(StreamEpoch));
  packet[0] = AS_BYTE(Code::CloseStreamSubCommands::CLOSE);
  size_t offset = 1;
  SCU_DO_AND_ASSERT(host_to_network(_parent->_connection_id, packet,
                                       offset), "Failed to convert connection ID to network format");
  SCU_DO_AND_ASSERT(host_to_network<StreamNumber>(_stream_number, packet,
                                       offset), "Failed to convert stream number to network format");
  SCU_DO_AND_ASSERT(host_to_network<StreamEpoch>(_stream_epoch, packet,
                                       offset), "Failed to convert stream epoch to network format");
  return send(Code::CLOSE_STREAM, packet, block);
}

void ScorpioUdpStream::update() {
  switch (state()) {
    case State::CREATING: {
        if (_parent->_time_provider->get_time() - _creation_time > SCU_UDP_CREATE_RETRY_PERIOD) {
          SCU_LOG_ERROR(_logger, "Stream creation failed after {} ms",
                        SCU_UDP_CREATE_RETRY_PERIOD / 1'000'000);
          _state.store(State::ERROR, std::memory_order_relaxed);
          break;
        }
        send_create_packet();
      } break;
    case State::CREATED: {
        if (_stream_qos.is_reliable()) {
          if (sat_sub(_parent->_time_provider->get_time(), _last_heartbeat_time.load(std::memory_order_relaxed)) >
            _parent->_parent->no_packet_timeout()) {
            SCU_LOG_ERROR(_logger, "Stream {} heartbeat timeout", _stream_number);
            _state.store(State::ERROR, std::memory_order_relaxed);
          }
        } else {
          remove_expired_unreliable_data();
        }
      } break;
    case State::CLOSING: {
        if (sat_sub(_parent->_time_provider->get_time(), _closing_since.load(std::memory_order_relaxed)) >
          _parent->_parent->no_packet_timeout()) {
          // The peer never answered the close. Fail the stream instead of retrying
          // forever - a live stream object pins its stream number, which would make
          // that number unusable for any future stream.
          SCU_LOG_ERROR(_logger, "Stream {} timed out waiting for the close handshake", _stream_number);
          _state.store(State::ERROR, std::memory_order_relaxed);
          break;
        }
        // Non-blocking: retried on every heartbeat tick until CLOSED or the
        // timeout above; blocking would wedge the connection processing thread.
        send_close_packet(false);
      } break;
    default: break;
  }
}

void ScorpioUdpStream::remove_expired_unreliable_data() {
  auto& partial_data = std::get<1>(_partial_data);
  const auto current_time = _parent->_time_provider->get_time();
  std::vector<size_t> to_remove;
  to_remove.reserve(partial_data.received_frames.size());
  for (const auto& [seq_number, data] : partial_data.received_frames) {
    if (current_time - data.receive_time > SCU_UDP_UNRELIABLE_DATA_EXPIRY_NS) {
      to_remove.push_back(seq_number);
    }
  }
  for (const auto& seq_number : to_remove) {
    partial_data.received_frames.erase(seq_number);
    partial_data.first_frames.erase(seq_number);
  }
  _expired_unreliable_fragment_count.fetch_add(SCU_AS(uint64_t, to_remove.size()), std::memory_order_relaxed);
}

void ScorpioUdpStream::handle_data_packet(const MessageHeader& header, UdpData&& data) {
  if (_stream_qos.is_reliable()) {
    StreamEpoch packet_epoch;
    auto offset = header.data_offset;
    if (SCU_UNLIKELY(!network_to_host(data.data, &packet_epoch, offset))) {
      SCU_LOG_ERROR(_logger, "Failed to parse stream epoch from STREAM_DATA packet");
      return;
    }
    if (SCU_UNLIKELY(packet_epoch != _stream_epoch)) {
      SCU_LOG_ERROR(_logger, "Received STREAM_DATA packet with unexpected stream epoch: {}. Expected: {}",
        packet_epoch, _stream_epoch);
      return;
    }
    _parent->_last_received_packet_time.store(_parent->_time_provider->get_time(), std::memory_order_relaxed);
    const auto seq_number = get_packet_number(header.seq_number.value());
    SCU_LOG_TRACE(_logger, "Processing ordered packet on stream {}: seq {}", _stream_number, seq_number);
    switch (_orderer.add(seq_number, { header, std::move(data.data) })) {
      case OrdererAddResult::TOO_NEW:
        panic("Received packet is too new");
        return;
      case OrdererAddResult::TOO_OLD: [[fallthrough]];
      // A retransmission of a packet already delivered, or already buffered awaiting
      // reassembly - safe to ignore, just counted as a duplicate.
      case OrdererAddResult::ALREADY_PRESENT:
        _duplicate_count.fetch_add(1, std::memory_order_relaxed);
        return;
      case OrdererAddResult::SUCCESS: break;
    }
    while (auto packet_opt = _orderer.next()) {
      auto& partial_data = std::get<std::vector<uint8_t>>(_partial_data);
      if (SCU_UNLIKELY(packet_opt->first.is_first && !partial_data.empty())) {
        panic("Received new first packet while previous packet is not complete");
        return;
      }
      std::ignore = partial_data.insert(
        partial_data.end(),
        packet_opt->second.begin() + SCU_AS(int64_t, packet_opt->first.data_offset) + sizeof(StreamEpoch),
        packet_opt->second.end());
      if (!packet_opt->first.frames_left.has_value()) {
        std::vector<uint8_t> complete_data;
        std::swap(complete_data, partial_data);
        _receive.send<true>(complete_data);
      }
    }
  } else {
    _parent->_last_received_packet_time.store(_parent->_time_provider->get_time(), std::memory_order_relaxed);
    if (header.is_first && !header.frames_left.has_value()) {
      _receive.send<true>(std::vector<uint8_t>(
        data.data.begin() + SCU_AS(int64_t, header.data_offset),
        data.data.end()));
      return;
    }
    const auto seq_number = get_packet_number(header.seq_number.value());
    auto& partial_data = std::get<1>(_partial_data);
    // TODO(@Igor): Remove old packets from partial_data to avoid memory bloat
    auto inserted_val = partial_data.received_frames.emplace(
      seq_number,
      UnreliableData{
      /*.receive_time = */ _parent->_time_provider->get_time(),
      /*.header       = */ header,
      /*.data         = */ std::move(data.data)
      }
    );
    if (!inserted_val.second) {
      _duplicate_count.fetch_add(1, std::memory_order_relaxed);
    }
    std::map<size_t, UnreliableData>::iterator start;
    if (header.is_first) {
      auto val = partial_data.first_frames.emplace(seq_number);
      if (!val.second) {
        // SCU_UNIMPLEMENTED();
        return;
      }
      start = std::move(inserted_val.first);
    } else {
      auto iter = partial_data.first_frames.upper_bound(seq_number);
      if (iter == partial_data.first_frames.end()) {
        return;
      }
      SCU_LOG_TRACE(_logger, "Received non-first packet on stream {}: seq {}. First frame seq: {}", _stream_number,
        seq_number, *iter);
      start = partial_data.received_frames.find(*iter);
      SCU_ASSERT(start != partial_data.received_frames.end(), "Inconsistent state");
      SCU_ASSERT(start->second.header.is_first, "Inconsistent state");
      SCU_ASSERT(start->second.header.frames_left.has_value(), "Inconsistent state");
      if (start->second.header.frames_left.value() +
        SCU_AS(size_t, start->second.header.seq_number.value()) + 1 < seq_number) {
        return;
      }
      SCU_ASSERT(start != partial_data.received_frames.end(), "Inconsistent state");
      SCU_ASSERT(start->second.header.is_first, "Inconsistent state");
    }
    bool is_complete = true;
    auto expected_frames_left = SCU_AS(size_t, start->second.header.frames_left.value()) + 1;
    auto current_seq = start->first;
    auto current_it = start;
    while (expected_frames_left != 0 && (current_it = std::next(current_it)) != partial_data.received_frames.end()) {
      if (current_it->first != ++current_seq) {
        is_complete = false;
        break;
      }
      --expected_frames_left;
      if ((expected_frames_left != 0 &&
        (!current_it->second.header.frames_left.has_value() ||
        current_it->second.header.frames_left.value() != expected_frames_left - 1)) ||
        (expected_frames_left == 0 && current_it->second.header.frames_left.has_value())) {
        panic("Inconsistent frames left in unreliable stream");
        return;
      }
    }
    is_complete = is_complete && expected_frames_left == 0;
    if (is_complete) {
      SCU_DO_AND_ASSERT(partial_data.first_frames.erase(start->first) == 1,
        "Inconsistent state after complete unreliable packet received");
      std::vector<uint8_t> complete_data;
      auto iter = start;
      complete_data.reserve(SCU_AS(size_t, SCU_UDP_MAX_PACKET_SIZE * (iter->second.header.frames_left.value() + 2)));
      do {
        std::ignore = complete_data.insert(
            complete_data.end(),
            iter->second.data.begin() + SCU_AS(int64_t, iter->second.header.data_offset),
            iter->second.data.end());
        iter = partial_data.received_frames.erase(iter);
      } while (iter != partial_data.received_frames.end() && !iter->second.header.is_first);
      _receive.send<true>(complete_data);
    }
  }
}

SCU_HOT bool ScorpioUdpStream::send(const std::vector<uint8_t>& data) {
  if (SCU_UNLIKELY(!is_active())) {
    return false;
  }
  if (SCU_UNLIKELY(!send(Code::STREAM_DATA, data))) {
    panic("Failed to send data on stream (maybe connection or socket is closed?)");
    return false;
  }
  return true;
}

size_t ScorpioUdpStream::get_packet_number(const SeqNumber v) noexcept {
  static_assert(!std::is_signed_v<SeqNumber>, "SeqNumber must be unsigned");
  size_t complement;
  if (_last_greatest_sequence_number < v &&
    v - _last_greatest_sequence_number > (std::numeric_limits<SeqNumber>::max() / 2)) {
    complement = SCU_AS(size_t, sat_sub<SeqNumberComplement>(_sequence_complement, 1));
  } else if (_last_greatest_sequence_number > v &&
    _last_greatest_sequence_number - v > (std::numeric_limits<SeqNumber>::max() / 2)) {
    complement = SCU_AS(size_t, ++_sequence_complement);
    _last_greatest_sequence_number = v;
  } else if (_last_greatest_sequence_number < v) {
    complement = SCU_AS(size_t, _sequence_complement);
    _last_greatest_sequence_number = v;
  } else {
    complement = SCU_AS(size_t, _sequence_complement);
  }
  return (complement << (sizeof(SeqNumber) * 8)) + SCU_AS(size_t, v);
}

bool ScorpioUdpStream::append_heartbeat_data(std::vector<uint8_t>& heartbeat_data) const {
  constexpr size_t packet_size = SCU_UDP_MAX_PACKET_SIZE - calculate_header_without_frames_left_size(Code::HEARTBEAT);
  // The connection id sits at the front of every heartbeat, so a single stream
  // block must fit in what remains after it (keeps the forced first block <= 512).
  constexpr size_t stream_region = packet_size - sizeof(ConnectionId);
  constexpr size_t prefix_size = sizeof(StreamNumber) + sizeof(StreamEpoch) + 1;
  static_assert(stream_region >= prefix_size + sizeof(SeqNumber),
    "Packet size is too small to fit any heartbeat data");
  constexpr size_t max_required_size = prefix_size + ((stream_region - prefix_size - sizeof(SeqNumber)) & ~1ul) +
    sizeof(SeqNumber);
  static_assert(max_required_size >= prefix_size + sizeof(SeqNumber),
    "Max required size is too small to fit any heartbeat data");

  if (!_stream_qos.is_reliable()) {
    return true;
  }
  const auto contained = _orderer.get_contained();
  const auto required_size = std::min(
    prefix_size + contained.size() * 2 * sizeof(SeqNumber) - sizeof(SeqNumber), max_required_size);
  // heartbeat_data always leads with the connection id, so "no stream blocks yet"
  // is size == sizeof(ConnectionId), not empty. The first block is force-appended.
  if (heartbeat_data.size() > sizeof(ConnectionId) && heartbeat_data.size() + required_size > packet_size) {
    return false;
  }

  auto pos = heartbeat_data.size();
  heartbeat_data.resize(pos + required_size);
  SCU_DO_AND_ASSERT(host_to_network(_stream_number, heartbeat_data, pos),
    "Failed to convert stream number to network format");
  SCU_DO_AND_ASSERT(host_to_network<StreamEpoch>(_stream_epoch, heartbeat_data, pos),
    "Failed to convert stream epoch to network format");
  const auto contained_count =
    std::min(contained.size() - 1, (max_required_size - prefix_size - sizeof(SeqNumber)) / (2 * sizeof(SeqNumber)));
  heartbeat_data[pos++] = AS_BYTE(contained_count);
  SCU_DO_AND_ASSERT(host_to_network(SCU_AS(SeqNumber, contained[0].second), heartbeat_data, pos),
    "Failed to convert sequence number to network format");

  for (size_t i = 0; i < contained_count; ) {
    ++i;
    const auto& [begin, end] = contained[i];
    SCU_LOG_TRACE(_logger, "Heartbeat stream {} ({}): contained range {} - {}", _stream_number, i, begin, end);
    SCU_DO_AND_ASSERT(host_to_network(SCU_AS(SeqNumber, begin), heartbeat_data, pos),
      "Failed to convert sequence number to network format");
    SCU_DO_AND_ASSERT(host_to_network(SCU_AS(SeqNumber, end), heartbeat_data, pos),
      "Failed to convert sequence number to network format");
  }
  return true;
}

void ScorpioUdpStream::handle_heartbeat_data(const std::vector<uint8_t>& data, size_t& pos) {
  SCU_LOG_TRACE(_logger, "Handling heartbeat data for stream {} with data size {} bytes", _stream_number,
    data.size() - pos);
  if (SCU_UNLIKELY(data.size() <= pos)) {
    SCU_LOG_WARNING(_logger,
                  "Invalid heartbeat data size for stream {}: expected at least 1 byte for range count, got {} "
                  "- ignoring", _stream_number, data.size() - pos);
    return;
  }
  uint8_t range_count = data[pos++];
  if (SCU_UNLIKELY(!_stream_qos.is_reliable())) {
    SCU_LOG_WARNING(_logger, "Received heartbeat for unreliable stream, which is not expected - ignoring");
    pos += (SCU_AS(size_t, range_count) * 2 + 1) * sizeof(SeqNumber);
    // TODO(@Igor): We probably want to send some error
    // close();
    return;
  }
  SeqNumber end;
  if (SCU_UNLIKELY(!network_to_host(data, &end, pos))) {
    SCU_LOG_WARNING(_logger, "Failed to parse end sequence number from heartbeat data for stream {} - ignoring",
                    _stream_number);
    return;
  }
  std::atomic_thread_fence(std::memory_order_acquire);
  std::lock_guard lock(_sent_history_mutex);
  const auto sequence_number = _sequence_number.load(std::memory_order_relaxed);
  const auto now = _parent->_time_provider->get_time();
  const auto previous_least = _least_non_delivered_seq_number.load(std::memory_order_relaxed);
  // 'cursor' walks the peer's reported ranges as a fully-widened sequence number.
  size_t cursor = least_significant_bytes_to_val(previous_least, end);
  if (SCU_UNLIKELY(cursor > sequence_number)) {
    // The peer claims to have received more than was ever sent: stale incarnation
    // or corruption. Skip the whole block (and keep 'pos' consistent for the next
    // stream block) instead of resending garbage from recycled history slots.
    SCU_LOG_WARNING(_logger,
      "Heartbeat for stream {} acknowledges seq {} but only {} packets were sent - ignoring block",
      _stream_number, cursor, sequence_number);
    pos += SCU_AS(size_t, range_count) * 2 * sizeof(SeqNumber);
    return;
  }
  if (cursor > previous_least) {
    // Monotonic on purpose: a reordered old heartbeat must not regress the ack
    // level, or send() could falsely detect a QoS depth overflow.
    // (This is the only place where _least_non_delivered_seq_number is modified.)
    _least_non_delivered_seq_number.store(cursor, std::memory_order_relaxed);
  }
  // This pass's contiguous ack boundary as reported by the peer: everything below it is
  // covered by this heartbeat's own account of what it has received, so stuck-resend
  // tracking below it is stale for at least this pass.
  const auto ack_cursor = cursor;
  enum class ResendResult : uint8_t { SENT, SKIPPED, OUT_OF_HISTORY, FAILED };
  // Set once the sender queue reports full for this pass: further resends are
  // pointless fuel on a saturated link, but the range walk must continue so 'pos'
  // stays consistent for the next stream block and the stuck-resend bookkeeping
  // still runs.
  bool tx_full = false;
  auto try_resend = [this, sequence_number, now, &tx_full](size_t i) -> ResendResult {
      // Packet i is gone from history only once it has been pushed out of the ring buffer,
      // i.e. more than _sent_history.size() newer packets have been sent. Written as an
      // addition (not sat_sub) so it is correct at the boundary and while the buffer is not
      // yet full - in particular seq 0 stays resendable until it is genuinely overwritten
      // (the old `i <= sat_sub(seq, size)` reported seq 0 as lost from the very first packet).
      if (i + _sent_history.size() < sequence_number) {
        _out_of_history_drop_count.fetch_add(1, std::memory_order_relaxed);
        return ResendResult::OUT_OF_HISTORY;
      }
      auto& packet = _sent_history[i % _sent_history.size()];
      if (SCU_UNLIKELY(!packet.has_value() || packet->seq != i)) {
        // Slot never written for this seq: either the owning send() allocated the
        // sequence range but is still waiting for the history lock, or the slot
        // was recycled. Not resendable right now - treat like an evicted packet.
        _out_of_history_drop_count.fetch_add(1, std::memory_order_relaxed);
        return ResendResult::OUT_OF_HISTORY;
      }
      if (now - packet->last_send_time < SCU_UDP_RESEND_INTERVAL) {
        // Recently (re)sent - most likely still in flight. Resending on every
        // heartbeat would turn one loss into a retransmission storm.
        return ResendResult::SKIPPED;
      }
      if (SCU_UNLIKELY(tx_full)) {
        return ResendResult::SKIPPED;
      }
      SCU_LOG_TRACE(_logger, "Resending packet with sequence number {} on stream {}", i, _stream_number);
      switch (_parent->send(clone(packet->data), false)) {
        case SendOutcome::SENT:
          packet->last_send_time = now;
          _parent->_retransmission_count.fetch_add(1, std::memory_order_relaxed);
          return ResendResult::SENT;
        case SendOutcome::FULL:
          // Deliberate drop: the peer's next heartbeat repeats the NACKs. Blocking
          // here (while holding _sent_history_mutex) is what used to wedge the
          // connection processing thread into a false no-packet timeout.
          tx_full = true;
          return ResendResult::SKIPPED;
        case SendOutcome::CLOSED:
          panic("Failed to resend packet (maybe connection or socket is closed?)");
          return ResendResult::FAILED;
      }
      SCU_UNREACHABLE();
    };
  // Records a seq the peer is still requesting that has fallen out of history, keeping
  // its original first-seen time if already tracked (so a persistently-stuck seq is not
  // reset just because a different seq was also seen this pass).
  auto note_stuck = [this, now](size_t i) {
      if (_stuck_resend_since.find(i) != _stuck_resend_since.end()) {
        return;
      }
      constexpr size_t stuck_resend_track_limit = SCU_UDP_STUCK_RESEND_TRACK_LIMIT;
      if (_stuck_resend_since.size() >= stuck_resend_track_limit) {
        return;
      }
      SCU_LOG_WARNING(_logger,
                      "Peer expects resend of packet with sequence number {} on stream {}, "
                      "but it's already out of resend history",
        i, _stream_number);
      _stuck_resend_since.emplace(i, now);
    };
  SeqNumber begin;
  while (range_count--) {
    if (SCU_UNLIKELY(!network_to_host(data, &begin, pos))) {
      SCU_LOG_ERROR(_logger, "Failed to parse begin sequence number from heartbeat data for stream {}", _stream_number);
      return;
    }
    const auto begin_transformed = least_significant_bytes_to_val(cursor, begin);
    if (SCU_UNLIKELY(begin_transformed < cursor || begin_transformed > sequence_number)) {
      // Ranges must be ascending and can never exceed what was actually sent.
      SCU_LOG_WARNING(_logger,
        "Malformed heartbeat range for stream {}: begin {} outside [{}, {}] - ignoring rest of block",
        _stream_number, begin_transformed, cursor, sequence_number);
      pos += sizeof(SeqNumber) + SCU_AS(size_t, range_count) * 2 * sizeof(SeqNumber);
      return;
    }
    for (auto i = cursor; i < begin_transformed; ++i) {
      switch (try_resend(i)) {
        case ResendResult::OUT_OF_HISTORY:
          note_stuck(i);
          break;
        case ResendResult::FAILED:
          return;
        default:
          break;
      }
    }
    if (SCU_UNLIKELY(!network_to_host(data, &end, pos))) {
      SCU_LOG_ERROR(_logger, "Failed to parse end sequence number from heartbeat data for stream {}", _stream_number);
      return;
    }
    cursor = least_significant_bytes_to_val(begin_transformed, end);
    if (SCU_UNLIKELY(cursor <= begin_transformed || cursor > sequence_number)) {
      SCU_LOG_WARNING(_logger,
        "Malformed heartbeat range for stream {}: end {} outside ({}, {}] - ignoring rest of block",
        _stream_number, cursor, begin_transformed, sequence_number);
      pos += SCU_AS(size_t, range_count) * 2 * sizeof(SeqNumber);
      return;
    }
  }
  // Tail probe: packets in [cursor, sequence_number) sit above everything the
  // peer's ranges mention, so the peer cannot NACK them - it does not know they
  // exist. Without this, the last packets of a burst that got lost would never be
  // retransmitted. The scan and the resend budget are bounded per heartbeat;
  // recovery is front-first, so the window slides forward as the peer catches up.
  {
    constexpr size_t tail_scan_limit = 4096;
    const auto tail_scan_end = std::min(sequence_number, cursor + tail_scan_limit);
    size_t budget = SCU_UDP_TAIL_RESEND_BUDGET;
    for (auto i = cursor; i < tail_scan_end && budget != 0; ++i) {
      switch (try_resend(i)) {
        case ResendResult::SENT:
          --budget;
          break;
        case ResendResult::OUT_OF_HISTORY:
          note_stuck(i);
          break;
        case ResendResult::FAILED:
          return;
        default:
          break;
      }
    }
  }
  // Self-heal: an unrecoverable resend request (packet gone from history) can loop
  // forever on a reliable-ordered stream. Distinct new holes are expected and fine; only
  // panic if the SAME seq is still being requested and still out of history after
  // SCU_UDP_TIMEOUT.
  for (auto it = _stuck_resend_since.begin(); it != _stuck_resend_since.end(); ) {
    if (it->first < ack_cursor) {
      // Peer's ack cursor passed this seq - it was delivered, no longer stuck.
      it = _stuck_resend_since.erase(it);
      continue;
    }
    const auto stuck_resend_timeout = _parent->_parent->no_packet_timeout();
    if (now - it->second >= stuck_resend_timeout) {
      SCU_LOG_WARNING(_logger,
                      "Peer has been requesting resend of sequence number {} on stream {} that is out of "
                      "resend history for over {}s - panicking stream to force recovery",
        it->first, _stream_number, stuck_resend_timeout / 1'000'000'000);
      panic("Peer stuck requesting a packet that is no longer in resend history");
      return;
    }
    ++it;
  }
  // Everything is fine, set last heartbeat time to now so we don't panic the stream for timeout
  _last_heartbeat_time.store(_parent->_time_provider->get_time(), std::memory_order_relaxed);
}

void ScorpioUdpStream::activate_stream() {
  static_assert(sizeof(decltype(_parent->_streams_mask)::value_type) == sizeof(uint64_t),
    "Streams mask must be 64-bit");
  static_assert(sizeof(decltype(_parent->_streams_mask_level_2)::value_type) == sizeof(uint64_t),
                "Streams mask must be 64-bit");
  const size_t mask_index = _stream_number >> 6;
  const size_t mask_position_mask = 1UL << (_stream_number & 63);
  const size_t mask_level2_index = mask_index >> 6;
  const size_t mask_level2_position_mask = 1UL << (mask_index & 63);
  std::lock_guard lock(_parent->_streams_mask_write_mutex);
  uint64_t current_state = _parent->_streams_mask[mask_index].load(std::memory_order_relaxed);
  do {
    SCU_ASSERT((current_state & mask_position_mask) == 0, "Stream mask bit is already set");
  } while (!_parent->_streams_mask[mask_index].compare_exchange_weak(current_state,
    current_state | mask_position_mask,
      std::memory_order_relaxed,
      std::memory_order_relaxed));
  current_state = _parent->_streams_mask_level_2[mask_level2_index].load(std::memory_order_relaxed);
  while (!_parent->_streams_mask_level_2[mask_level2_index].compare_exchange_weak(current_state,
    current_state | mask_level2_position_mask,
      std::memory_order_relaxed,
      std::memory_order_relaxed)) { }
  _parent->_streams[_stream_number] = weak_from_this();
}
