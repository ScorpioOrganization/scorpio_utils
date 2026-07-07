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
         (code.is_connectionless() ? 0 : sizeof(SeqNumber)) +
         (code.is_command_for_stream() ? sizeof(StreamNumber) : 0);
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
  _auto_accept(false),
  _stop(true),
  _logger(logger),
  _panic(false),
  _new_connections(nullptr)
{
  SCU_LOG_INFO(_logger, "ScorpioUdp created");
}

ScorpioUdp::~ScorpioUdp() {
  SCU_LOG_INFO(_logger, "ScorpioUdp destructor called");
  _awaiting_connections_channel.close();
  _receiver_channel.close();
  _sender_channel.close();
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
  if (SCU_UNLIKELY(!_socket.is_open())) {
    SCU_LOG_ERROR(_logger, "Failed to send packet because socket is not open");
    return false;
  }
  try {
    _sender_channel.send<true>({
      /*._ip =   */ remote_ip,
      /*._port = */ remote_port,
      /*._data = */ std::move(packet),
      });
  } catch (const threading::ClosedChannelException&) {
    SCU_LOG_ERROR(_logger, "Failed to send packet because sender channel is closed");
    return false;
  }
  return true;
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
      auto msg = _sender_channel.receive<true>();
      SCU_ASSERT(msg.data.size() <= SCU_UDP_MAX_PACKET_SIZE,
        "UDP message size is too large (" << msg.data.size()
                                          << " bytes), max is " << SCU_UDP_MAX_PACKET_SIZE << " bytes");
      if (SCU_UNLIKELY(!_socket.is_open())) {
        panic("Socket is not open");
      }
      // SCU_LOG_TRACE(_logger, "Sending to: {}",
      // create_log(msg.ip, msg.port, parse_header(msg.data).ok().value(), msg.data));
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
                      udp_data.ip, udp_data.port, connection_id, shared_from_this()));
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
        if (auto connection_opt = get_connection(udp_data.ip, udp_data.port);
          connection_opt != nullptr && connection_opt->state() == ScorpioUdpConnection::State::CONNECTED) {
          connection_opt->_incoming_packets.send<true>({ header, std::move(udp_data) });
        } else {
          // Received connection-oriented packet for non-existing connection
          SCU_LOG_ERROR(_logger,
            "Received packet for non-existing connection from {}:{}. Command: {}",
            udp_data.ip.str(), udp_data.port, SCU_AS(Code, header.command));
          // SCU_UNIMPLEMENTED();  // This is harmless so, just ignore it for now
        }
      } break;
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
  const std::vector<uint8_t>& data) {
  using scorpio_utils::network::host_to_network;
  const auto header_without_frames_left_size = calculate_header_without_frames_left_size(code);
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
  size_t current = 0;
  const auto first_packet_seq =
    SCU_AS(SeqNumber, sequence_number.fetch_add(packets_to_send, std::memory_order_relaxed));
  auto packet_seq = first_packet_seq;
  size_t packet_pos = 0;
  packets.emplace_back();
  auto generate_header =
    [&packets, &packet_pos, &packet_seq, stream_number, code, first = true](
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
  while (data.size() - current > packet_size_without_frames_left) {
    packets.back().resize(SCU_UDP_MAX_PACKET_SIZE);
    generate_header();
    size_t frames_left = (data.size() - current - packet_size_without_frames_left) / packet_size_with_frames_left;
    if (!host_to_network(static_cast<decltype(MessageHeader::frames_left)::value_type>(frames_left), packets.back(),
      packet_pos)) {
      // panic("Failed to convert frames left to network format");
      return std::nullopt;
    }
    SCU_ASSERT(packet_pos + packet_size_with_frames_left == packets.back().size(),
      "Packet size miscalculation: " << packet_pos << " + " << packet_size_with_frames_left << " == " <<
      packets.back().size());
    std::memcpy(packets.back().data() + packet_pos, data.data() + current, packet_size_with_frames_left);
    packets.emplace_back();
    current += packet_size_with_frames_left;
    packet_pos = 0;
  }
  packets.back().resize(sizeof(Code::Values) +
    (code.is_command_for_stream() ? sizeof(SeqNumber) + sizeof(StreamNumber) : 0) +
    (data.size() - current));
  generate_header(true);
  std::memcpy(packets.back().data() + packet_pos, data.data() + current, data.size() - current);
  return { { first_packet_seq, packets } };
}

bool ScorpioUdp::send(
  std::optional<StreamNumber> stream_number,
  std::atomic<size_t>& sequence_number,
  Code code,
  Ipv4 remote_ip,
  Port remote_port,
  const std::vector<uint8_t>& data
) {
  auto packets = generate_packets(stream_number, sequence_number, code, data);
  if (SCU_UNLIKELY(!packets.has_value())) {
    return false;
  }
  for (auto& packet : packets->second) {
    if (SCU_UNLIKELY(!send(remote_ip, remote_port, std::move(packet)))) {
      SCU_LOG_ERROR(_logger, "Failed to send packet to {}:{}", remote_ip.str(), remote_port);
      return false;
    }
  }
  return true;
}

void ScorpioUdp::send_or_panic(
  std::optional<StreamNumber> stream_number,
  std::atomic<size_t>& sequence_number,
  Code code,
  Ipv4 remote_ip,
  Port remote_port,
  const std::vector<uint8_t>& data,
  std::string&& panic_message) {
  if (SCU_UNLIKELY(!send(stream_number, sequence_number, code, remote_ip, remote_port, data))) {
    panic(std::move(panic_message));
  }
}

std::shared_ptr<ScorpioUdpConnection> ScorpioUdp::connect(
  Ipv4 ip,
  Port port) {
  std::shared_ptr<ScorpioUdpConnection> connection(new ScorpioUdpConnection(ip, port, get_random_number(),
    shared_from_this()));
  connection->_start_signal.notify(100000);
  _awaiting_connections_channel.send<true>(connection);
  return connection;
}

// ========================= ScorpioUdpConnection implementation =======================

ScorpioUdpConnection::ScorpioUdpConnection(
  Ipv4 remote_ip, Port remote_port, ConnectionId connection_id,
  std::shared_ptr<ScorpioUdp> parent)
: _remote_ip(remote_ip),
  _remote_port(remote_port),
  _connection_id{connection_id},
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
  _next_stream_to_heartbeat(0),
  _stream_exists{false},
  _streams_mask_level_2{0},
  _streams_mask{0},
  _processing_thread(&ScorpioUdpConnection::processing_thread, this) {
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
  switch (SCU_AS(Code::CreateStreamSubCommands, data.data[offset++])) {
    case Code::CreateStreamSubCommands::CREATE: {
        Code::CreateStreamSubCommands response_code;
        StreamNumber stream_number;
        if (!network_to_host(data.data, &stream_number, offset)) {
          // TODO(@Igor): Handle error properly
          SCU_LOG_ERROR(_logger, "Failed to parse CREATE_STREAM stream number");
          return;
        }
        auto qos_opt = parse_qos(data.data, offset);
        if (!qos_opt) {
          // TODO(@Igor): Handle error properly
          SCU_LOG_ERROR(_logger, "Failed to parse CREATE_STREAM QoS");
          return;
        }
        std::shared_ptr<ScorpioUdpStream> stream;
        do {
          bool expected = false;
          if (auto stream = get_stream(stream_number)) {
            if (stream->qos() == *qos_opt) {
              if (stream->state() == ScorpioUdpStream::State::CREATING) {
                stream->connected();
              }
              response_code = Code::CreateStreamSubCommands::ALREADY_EXISTS;
            } else {
              stream->panic("Peer tried to create stream with existing stream number but different QoS");
              response_code = Code::CreateStreamSubCommands::REJECT_SIMILAR_EXISTED;
            }
            break;
          } else if (!(_auto_accept_stream.load(std::memory_order_relaxed) && qos_opt->is_supported())) {
            response_code = Code::CreateStreamSubCommands::REJECT;
            break;
          } else if (_stream_exists[stream_number].compare_exchange_strong(
              expected,
              true,
              std::memory_order_relaxed,
              std::memory_order_relaxed)) {
            std::shared_ptr<ScorpioUdpStream> new_stream(new ScorpioUdpStream(
                  stream_number, *qos_opt, shared_from_this()));
            new_stream->_state.store(ScorpioUdpStream::State::CREATING, std::memory_order_relaxed);
            new_stream->connected();
            new_stream->activate_stream();
            _new_streams.send<true>(std::move(new_stream));
            response_code = Code::CreateStreamSubCommands::ACCEPT;
            break;
          }
        } while (true);
        std::vector<uint8_t> response;
        response.reserve(offset - header.data_offset);
        response.push_back(AS_BYTE(response_code));
        std::ignore =
          std::copy(data.data.begin() + SCU_AS(int64_t, header.data_offset) + sizeof(response_code),
        data.data.begin() + SCU_AS(int64_t, offset),
          std::back_inserter(response));
        send_or_panic(Code::CREATE_STREAM, response);
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
        stream->connected();
      } break;
    case Code::CreateStreamSubCommands::REJECT_SIMILAR_EXISTED:
      [[fallthrough]];
    case Code::CreateStreamSubCommands::ALREADY_EXISTS: {
        StreamNumber stream_number;
        if (!network_to_host(data.data, &stream_number, offset)) {
          // TODO(@Igor): Handle error properly
          SCU_LOG_ERROR(_logger, "Failed to parse stream number from ALREADY_EXISTS CREATE_STREAM packet");
          return;
        }
      } break;
    default: {
        SCU_LOG_ERROR(_logger, "Received unknown CREATE_STREAM packet with subcommand: {}",
        static_cast<Code::CreateStreamSubCommands>(data.data[offset - 1]));
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
        SCU_LOG_INFO(_logger, "Stream number {} was rejected by peer", stream_number);
        stream->_state.store(ScorpioUdpStream::State::REJECTED, std::memory_order_relaxed);
      } break;
  }
}

void ScorpioUdpConnection::close_stream_packet_handler(const MessageHeader& header, UdpData&& data) {
  if (data.data.size() - header.data_offset != sizeof(Code::CloseStreamSubCommands) + sizeof(StreamNumber)) {
    SCU_LOG_ERROR(_logger,
      "Invalid CLOSE_STREAM packet size: expected {} bytes, got {} bytes",
      sizeof(Code::CloseStreamSubCommands) + sizeof(StreamNumber),
      data.data.size() - header.data_offset);
    return;
  }
  size_t offset = header.data_offset;
  Code::CloseStreamSubCommands subcode;
  subcode = SCU_AS(Code::CloseStreamSubCommands, data.data[offset++]);
  StreamNumber stream_number;
  if (!network_to_host(data.data, &stream_number, offset)) {
    SCU_LOG_ERROR(_logger, "Failed to parse stream number from CLOSE_STREAM packet");
    return;
  }
  auto stream = get_stream(stream_number);
  switch (subcode) {
    case Code::CloseStreamSubCommands::CLOSE: {
        std::vector<uint8_t> response;
        size_t response_offset = 1;
        response.reserve(3);
        if (!stream) {
          response.emplace_back(AS_BYTE(Code::CloseStreamSubCommands::ALREADY_CLOSED));
        } else {
          ScorpioUdpStream::State expected = stream->state();
          while (stream->is_alive()) {
            if (stream->_state.compare_exchange_strong(
                expected,
                ScorpioUdpStream::State::CLOSED,
                std::memory_order_relaxed,
                std::memory_order_relaxed)) {
              response.emplace_back(AS_BYTE(Code::CloseStreamSubCommands::CLOSED));
            }
          }
          if (response.empty()) {
            response.emplace_back(AS_BYTE(Code::CloseStreamSubCommands::ALREADY_CLOSED));
          }
        }
        response.resize(3);
        SCU_DO_AND_ASSERT(host_to_network<StreamNumber>(stream_number, response,
                                                 response_offset), "Failed to convert stream number to network format");
        send(Code::CLOSE_STREAM, response, stream_number);
      } break;
    case Code::CloseStreamSubCommands::CLOSED: {
        if (stream) {
          std::ignore = stream->closed();
        }
      } break;
    case Code::CloseStreamSubCommands::ALREADY_CLOSED: {
        if (stream) {
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
  StreamNumber stream_num;
  _last_received_heartbeat_time.store(_time_provider->get_time(), std::memory_order_relaxed);
  _received_heartbeat_count.fetch_add(1, std::memory_order_relaxed);
  while (network_to_host(data.data, &stream_num, pos)) {
    if (auto stream = get_stream(stream_num)) {
      stream->handle_heartbeat_data(data.data, pos);
    } else {
      SCU_LOG_DEBUG(_logger, "Received heartbeat data for non-existing stream number {}", stream_num);
      std::vector<uint8_t> response;
      response.reserve(3);
      response.emplace_back(AS_BYTE(Code::CloseStreamSubCommands::ALREADY_CLOSED));
      size_t response_offset = 1;
      response.resize(3);
      SCU_DO_AND_ASSERT(host_to_network<StreamNumber>(stream_num, response, response_offset),
        "Failed to convert stream number to network format for CLOSE_STREAM ALREADY_CLOSED response");
      if (SCU_UNLIKELY(!send(Code::CLOSE_STREAM, std::move(response), stream_num,
        _sequence_number))) {
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
    panic("Stream with the same stream number already exists");
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
  _last_received_packet_time.store(_time_provider->get_time(), std::memory_order_relaxed);
  _received_packet_count.fetch_add(1, std::memory_order_relaxed);
  handle_new_packet(packet.first, std::move(packet.second));
}

void ScorpioUdpConnection::send_heartbeat() {
  const auto time_since_last_packet = _time_provider->get_time() -
    _last_received_packet_time.load(std::memory_order_relaxed);
  if (SCU_UNLIKELY(time_since_last_packet > SCU_UDP_TIMEOUT)) {
    panic("No packets received for 5 seconds");
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
  send_or_panic(Code::HEARTBEAT, heartbeat_data);
  _send_heartbeat_count.fetch_add(1, std::memory_order_relaxed);
  if (full) {
    _send_partial_heartbeat_count.fetch_add(1, std::memory_order_relaxed);
  }
}

void ScorpioUdpConnection::processing_thread() {
  try {
    _start_signal.wait();
    const auto self_weak = weak_from_this();
    std::shared_ptr<ScorpioUdpConnection> self;
    while (SCU_LIKELY((self = self_weak.lock()) && !_stop.load(std::memory_order_relaxed)) &&
      _state.load(std::memory_order_relaxed) == State::NEW) {
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

SCU_COLD void ScorpioUdpConnection::panic_soft(std::string&& message) {
  std::unique_lock<std::mutex> lock(_panic_mutex, std::try_to_lock);
  if (!lock.owns_lock()) {
    // No need for waiting since panic_soft shall be called from the socket thread
    return;
  }
  SCU_LOG_FATAL(_logger, "Connection panic: {}", message);
  _panic_message = std::move(message);
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
  _panic.store(true, std::memory_order_release);
  _state.store(State::ERROR, std::memory_order_relaxed);
  _stop.store(true, std::memory_order_relaxed);
  throw PanicException();
}

auto ScorpioUdpConnection::generate_packets(
  Code code, const std::vector<uint8_t>& data, std::optional<StreamNumber> stream_number,
  std::optional<std::reference_wrapper<std::atomic<size_t>>> sequence_number) {
  return ::generate_packets(
        stream_number,
        sequence_number ? sequence_number->get() : _sequence_number,
        code,
        data);
}

bool ScorpioUdpConnection::send(
  Code code, const std::vector<uint8_t>& data, std::optional<StreamNumber> stream_number,
  std::optional<std::reference_wrapper<std::atomic<size_t>>> sequence_number) {
  if (SCU_UNLIKELY(!_parent->is_running())) {
    return false;
  }
  return _parent->send(stream_number,
    sequence_number.value_or(std::ref(_sequence_number)).get(),
    code, _remote_ip, _remote_port, data);
}

bool ScorpioUdpConnection::send(std::vector<uint8_t>&& packet) {
  if (SCU_UNLIKELY(!_parent->is_running())) {
    SCU_LOG_ERROR(_logger, "Failed to send packet because parent socket is not running");
    return false;
  }
  return _parent->send(_remote_ip, _remote_port, std::move(packet));
}

void ScorpioUdpConnection::send_or_panic(
  Code code, const std::vector<uint8_t>& data, std::string&& message) {
  if (SCU_UNLIKELY(!send(code, data))) {
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
  SCU_LOG_INFO(_logger, "Closing connection {}:{}", _remote_ip.str(), _remote_port);
  if (send_disconnect) {
    std::vector<uint8_t> disconnect_data;
    disconnect_data.resize(sizeof(Code::DisconnectSubCommands) + sizeof(ConnectionId));
    disconnect_data[0] = AS_BYTE(Code::DisconnectSubCommands::DISCONNECT);
    size_t offset = 1;
    SCU_DO_AND_ASSERT(host_to_network(_connection_id, disconnect_data, offset),
      "Failed to convert connection ID to network format for DISCONNECT packet");
    send(Code::DISCONNECT, disconnect_data, std::nullopt, std::nullopt);
  }
  _stop.store(true, std::memory_order_relaxed);
  _state.store(State::CLOSED, std::memory_order_relaxed);
  _new_streams.close();
  _awaiting_streams.close();
  _incoming_packets.close();
  if (_processing_thread.get_id() == std::this_thread::get_id()) {
    _processing_thread.detach();
  } else if (_processing_thread.joinable()) {
    _processing_thread.join();
  }
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
  std::shared_ptr<ScorpioUdpStream> stream(new ScorpioUdpStream(stream_id, qos, shared_from_this()));
  _awaiting_streams.send<true>(stream);
  return stream;
}

// ========================= ScorpioUdpStream implementation ===========================

ScorpioUdpStream::ScorpioUdpStream(
  StreamNumber stream_number, StreamQoS stream_qos,
  std::shared_ptr<ScorpioUdpConnection> parent)
: _stream_number(stream_number),
  _stream_qos(stream_qos),
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
  _stuck_resend_seq(std::nullopt),
  _stuck_resend_since(0) {
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
  _parent->_streams[_stream_number].reset();
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

SCU_HOT bool ScorpioUdpStream::send(Code code, const std::vector<uint8_t>& data) {
  if (SCU_UNLIKELY(!is_active() && !(state() == State::CLOSING && code == Code::CLOSE_STREAM))) {
    SCU_LOG_ERROR(_logger, "Attempted to send on inactive stream state: {}", magic_enum::enum_name(state()));
    return false;
  }
  auto packets = _parent->generate_packets(
    code,
    data,
    _stream_number,
    _sequence_number
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
      _sent_history[pos] = packet;
    }
    SCU_LOG_TRACE(_logger, "Sending packet on stream {}: seq {} (packets left: {})", _stream_number, seq,
      parse_header(packet).ok_value().frames_left.value_or(32767));
    if (SCU_UNLIKELY(!_parent->send(std::move(packet)))) {
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
  _state.store(State::ERROR, std::memory_order_release);
}

void ScorpioUdpStream::send_create_packet() {
  std::vector<uint8_t> packet;
  constexpr auto minimal_size = sizeof(Code::CreateStreamSubCommands) + sizeof(StreamNumber) +
    sizeof(StreamQoS::Reliability);
  packet.reserve(minimal_size + sizeof(StreamQoS::depth));
  packet.resize(minimal_size);
  packet[0] = AS_BYTE(Code::CreateStreamSubCommands::CREATE);
  size_t offset = 1;
  SCU_DO_AND_ASSERT(host_to_network(_stream_number, packet,
                                       offset), "Failed to convert stream number to network format");
  serialize_qos(_stream_qos, packet, offset);
  _parent->send_or_panic(Code::CREATE_STREAM, packet, "Failed to send CREATE_STREAM command");
}

void ScorpioUdpStream::connected() {
  State expected = State::CREATING;
  _state.compare_exchange_strong(
    expected,
    State::CREATED,
    std::memory_order_relaxed,
    std::memory_order_relaxed);
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

bool ScorpioUdpStream::send_close_packet() {
  std::vector<uint8_t> packet;
  packet.resize(3);
  packet[0] = AS_BYTE(Code::CloseStreamSubCommands::CLOSE);
  size_t offset = 1;
  SCU_DO_AND_ASSERT(host_to_network<StreamNumber>(_stream_number, packet,
                                       offset), "Failed to convert stream number to network format");
  return send(Code::CLOSE_STREAM, packet);
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
        if (!_stream_qos.is_reliable()) {
          remove_expired_unreliable_data();
        }
      } break;
    case State::CLOSING: {
        send_close_packet();
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
}

void ScorpioUdpStream::handle_data_packet(const MessageHeader& header, UdpData&& data) {
  if (_stream_qos.is_reliable()) {
    const auto seq_number = get_packet_number(header.seq_number.value());
    SCU_LOG_TRACE(_logger, "Processing ordered packet on stream {}: seq {}", _stream_number, seq_number);
    switch (_orderer.add(seq_number, { header, std::move(data.data) })) {
      case OrdererAddResult::TOO_NEW:
        panic("Received packet is too new");
        [[fallthrough]];
      case OrdererAddResult::TOO_OLD: [[fallthrough]];
      // May be safely ignored
      case OrdererAddResult::ALREADY_PRESENT: return;
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
        packet_opt->second.begin() + SCU_AS(int64_t, packet_opt->first.data_offset),
        packet_opt->second.end());
      if (!packet_opt->first.frames_left.has_value()) {
        std::vector<uint8_t> complete_data;
        std::swap(complete_data, partial_data);
        _receive.send<true>(complete_data);
      }
    }
  } else {
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
      // SCU_UNIMPLEMENTED();
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
  static_assert(packet_size >= sizeof(StreamNumber) + 1 + sizeof(SeqNumber),
    "Packet size is too small to fit any heartbeat data");
  constexpr size_t prefix_size = sizeof(StreamNumber) + 1;
  constexpr size_t max_required_size = prefix_size + ((packet_size - prefix_size - sizeof(SeqNumber)) & ~1ul) +
    sizeof(SeqNumber);
  static_assert(max_required_size >= prefix_size + sizeof(SeqNumber),
    "Max required size is too small to fit any heartbeat data");

  if (!_stream_qos.is_reliable()) {
    return true;
  }
  const auto contained = _orderer.get_contained();
  const auto required_size = std::min(
    prefix_size + contained.size() * 2 * sizeof(SeqNumber) - sizeof(SeqNumber), max_required_size);
  if (!heartbeat_data.empty() && heartbeat_data.size() + required_size > packet_size) {
    return false;
  }

  auto pos = heartbeat_data.size();
  heartbeat_data.resize(pos + required_size);
  SCU_DO_AND_ASSERT(host_to_network(_stream_number, heartbeat_data, pos),
    "Failed to convert stream number to network format");
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
  // Operation loses its atomicity, but it's ok since this is the only place where
  // _least_non_delivered_seq_number is modified
  size_t greatest_seen_val = least_significant_bytes_to_val(
    _least_non_delivered_seq_number.load(std::memory_order_relaxed),
    end);
  _least_non_delivered_seq_number.store(greatest_seen_val, std::memory_order_relaxed);
  SeqNumber begin;
  std::atomic_thread_fence(std::memory_order_acquire);
  std::lock_guard lock(_sent_history_mutex);
  auto sequence_number = _sequence_number.load(std::memory_order_relaxed);
  // Lowest seq (if any) the peer asked to resend that has fallen out of history this pass.
  std::optional<size_t> stuck_resend_seq;
  while (range_count--) {
    if (SCU_UNLIKELY(!network_to_host(data, &begin, pos))) {
      SCU_LOG_ERROR(_logger, "Failed to parse begin sequence number from heartbeat data for stream {}", _stream_number);
      return;
    }
    const auto begin_transformed = least_significant_bytes_to_val(greatest_seen_val, begin);
    for (auto i = least_significant_bytes_to_val(greatest_seen_val, end); i < begin_transformed; ++i) {
      // Packet i is gone from history only once it has been pushed out of the ring buffer,
      // i.e. more than _sent_history.size() newer packets have been sent. Written as an
      // addition (not sat_sub) so it is correct at the boundary and while the buffer is not
      // yet full - in particular seq 0 stays resendable until it is genuinely overwritten
      // (the old `i <= sat_sub(seq, size)` reported seq 0 as lost from the very first packet).
      if (i + _sent_history.size() < sequence_number) {
        if (!stuck_resend_seq.has_value()) {
          stuck_resend_seq = i;
        }
        SCU_LOG_WARNING(_logger,
                        "Peer expects resend of packet with sequence number {} on stream {}, "
                        "but it's already out of resend history",
          i, _stream_number);
        continue;
      }
      const auto& packet = _sent_history[i % _sent_history.size()];
      if (SCU_UNLIKELY(!packet.has_value())) {
        panic("Peer expects unsend message");
        return;
      }
      SCU_LOG_TRACE(_logger, "Resending packet with sequence number {} on stream {}", i, _stream_number);
      _parent->_retransmission_count.fetch_add(1, std::memory_order_relaxed);
      if (SCU_UNLIKELY(!_parent->send(clone(*packet)))) {
        panic("Failed to resend packet (maybe connection or socket is closed?)");
        return;
      }
    }
    if (SCU_UNLIKELY(!network_to_host(data, &end, pos))) {
      SCU_LOG_ERROR(_logger, "Failed to parse end sequence number from heartbeat data for stream {}", _stream_number);
      return;
    }
    greatest_seen_val = least_significant_bytes_to_val(begin_transformed, end);
  }
  // Self-heal: an unrecoverable resend request (packet gone from history) can loop
  // forever on a reliable-ordered stream. Tolerate transient occurrences, but if the peer
  // stays stuck on the same seq for SCU_UDP_TIMEOUT, panic the stream so it gets rebuilt.
  if (stuck_resend_seq.has_value()) {
    const auto now = _parent->_time_provider->get_time();
    if (_stuck_resend_seq == stuck_resend_seq) {
      if (now - _stuck_resend_since >= SCU_UDP_TIMEOUT) {
        SCU_LOG_WARNING(_logger,
                        "Peer has been requesting resend of sequence number {} on stream {} that is out of "
                        "resend history for over {}s - panicking stream to force recovery",
          *stuck_resend_seq, _stream_number, SCU_UDP_TIMEOUT / 1'000'000'000);
        panic("Peer stuck requesting a packet that is no longer in resend history");
        return;
      }
    } else {
      _stuck_resend_seq = stuck_resend_seq;
      _stuck_resend_since = now;
    }
  } else {
    _stuck_resend_seq.reset();
  }
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
