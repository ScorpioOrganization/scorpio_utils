#include "scorpio_utils/network/scorpio_udp.hpp"

#include <chrono>
#include <cmath>
#include <exception>
#if defined(SCORPIO_UTILS_SUDP_LOG_TO_FILE) && SCORPIO_UTILS_SUDP_LOG_TO_FILE == 1
#include <sstream>
#endif
#include "scorpio_utils/assert.hpp"
#include "scorpio_utils/decorators.hpp"
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

#define AS_BYTE(x) (SCU_AS(uint8_t, x))
#define MAX_PACKET_SIZE (512)
#define QOS_DEPTH_SAFETY_BUFFER (2048)
#define UNRELIABLE_DATA_EXPIRY_NS (500'000'000)  // 500 milliseconds
#define SCU_UDP_DEBUG_LOG_ENABLED (0)
#define HEARTBEAT_PERIOD (50'000'000)
#define TIMEOUT (5'000'000'000)

#if defined(SCORPIO_UTILS_SUDP_LOG_TO_FILE) && SCORPIO_UTILS_SUDP_LOG_TO_FILE == 1
# define SUDP_LOG(message) log_to_file((std::ostringstream() << message).str())
#else
# define SUDP_LOG(message)
#endif

struct PanicException : public std::exception { };

SCU_HOT SCU_CONST_FUNC constexpr size_t packets_count(size_t data_size, size_t header_without_frames_left) {
  size_t packets = SCU_AS(size_t, data_size != 0);
  if (data_size > (MAX_PACKET_SIZE - header_without_frames_left)) {
    data_size -= (MAX_PACKET_SIZE - header_without_frames_left);
    auto packet_size = (MAX_PACKET_SIZE - header_without_frames_left - sizeof(FramesLeft));
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

static std::shared_ptr<scorpio_utils::time_provider::LazyTimeProvider> get_time_provider() {
  static std::mutex mutex;
  std::lock_guard lock(mutex);
  static std::weak_ptr<scorpio_utils::time_provider::LazyTimeProvider> weak_provider;
  if (auto provider = weak_provider.lock()) {
    return provider;
  }
  auto provider = std::make_shared<scorpio_utils::time_provider::LazyTimeProvider>();
  weak_provider = provider;
  return provider;
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

std::shared_ptr<ScorpioUdp> ScorpioUdp::create() {
  auto ans = std::shared_ptr<ScorpioUdp>(new ScorpioUdp());
  ans->_start_signal.notify(100000);
  return ans;
}

ScorpioUdp::ScorpioUdp()
: _new_connections(nullptr),
  _auto_accept(false),
  _stop(true)
#if defined(SCORPIO_UTILS_SUDP_LOG_TO_FILE) && SCORPIO_UTILS_SUDP_LOG_TO_FILE == 1
  , _logger("scorpio_udp_log.txt")
#endif
{
  SUDP_LOG("ScorpioUdp created");
}

ScorpioUdp::~ScorpioUdp() {
  SUDP_LOG("ScorpioUdp destructor called");
  _awaiting_connections_channel.close();
  _receiver_channel.close();
  _sender_channel.close();
  stop();
  std::lock_guard lock(_threads_mutex);
  SCU_ASSERT(_threads.empty(), "ScorpioUdp threads not stopped");
  SUDP_LOG("ScorpioUdp destroyed");
}

bool ScorpioUdp::send(
  Ipv4 remote_ip,
  Port remote_port,
  std::vector<uint8_t>&& packet
) {
  if (SCU_UNLIKELY(!_socket.is_open())) {
    return false;
  }
  try {
    _sender_channel.send<true>({
      /*._ip =   */ remote_ip,
      /*._port = */ remote_port,
      /*._data = */ std::move(packet),
      });
  } catch (const threading::ClosedChannelException&) {
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
  SUDP_LOG("ScorpioUdp stopping");
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
  SUDP_LOG("ScorpioUdp stopped");
  return true;
}

bool ScorpioUdp::start() {
  bool expected = true;
  if (SCU_UNLIKELY(!_stop.compare_exchange_strong(expected, false, std::memory_order_relaxed,
                                                  std::memory_order_relaxed))) {
    return false;
  }
  SUDP_LOG("ScorpioUdp starting");
  _socket.open();
  _stop.store(false, std::memory_order_relaxed);
  _new_connections = std::make_unique<decltype(_new_connections)::element_type>();
  _threads.emplace_back(&ScorpioUdp::receiver_thread, this);
  _threads.emplace_back(&ScorpioUdp::sender_thread, this);
  _threads.emplace_back(&ScorpioUdp::processing_thread, this);
  SUDP_LOG("ScorpioUdp started");
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
#if SCU_UDP_DEBUG_LOG_ENABLED == 1
  std::cerr << "Panic: " << message << "\n" << std::flush;
#endif
  SCU_UNLIKELY_THROW_IF(_panic.load(std::memory_order_relaxed), PanicException, );
  _panic_message = std::move(message);
  bool expected = false;
  if (_panic.compare_exchange_strong(expected, true, std::memory_order_relaxed, std::memory_order_relaxed)) {
    stop();
  }
  throw PanicException();
}

void ScorpioUdp::receiver_thread() {
  try {
    while (SCU_LIKELY(!_stop.load(std::memory_order_relaxed))) {
      std::vector<uint8_t> data(MAX_PACKET_SIZE);
      auto result = _socket.receive(data.data(), data.size());
      if (SCU_UNLIKELY(result.is_err())) {
        panic("Failed to receive data: " + std::move(result).err_value());
        break;
      }
      if (result.ok_value().byte_count == 0) {
        continue;
      }
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
      SCU_ASSERT(msg.data.size() <= MAX_PACKET_SIZE,
        "UDP message size is too large (" << msg.data.size()
                                          << " bytes), max is " << MAX_PACKET_SIZE << " bytes");
      if (SCU_UNLIKELY(!_socket.is_open())) {
        panic("Socket is not open");
      }
      auto result = _socket.send(msg.data.data(), msg.data.size(), msg.ip, msg.port);
      if (SCU_UNLIKELY(result.is_err())) {
        panic(std::move(result).err_value());
      }
      SCU_ASSERT(result.ok_value() == msg.data.size(), "UDP send less bytes then requested");
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
      std::visit(VisitorOverloadingHelper{
        [this](UdpData packet) SCU_ALWAYS_INLINE_RAW {
          process_packet(std::move(packet));
        },
        [this](std::weak_ptr<ScorpioUdpConnection> connection) SCU_ALWAYS_INLINE_RAW {
          pull_awaiting_connections(connection);
        },
      }, threading::eager_select(_receiver_channel, _awaiting_connections_channel));
    }
  } catch (const PanicException&) {
  } catch (const threading::ClosedChannelException&) {
  }
#if SCU_UDP_DEBUG_LOG_ENABLED == 1
  std::cerr << "Processing thread exited\n";
#endif
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
  if (udp_data.data.size() - header.data_offset != 1) {
    // TODO(@Igor): Handle error properly
#if SCU_UDP_DEBUG_LOG_ENABLED == 1
    std::cerr << "Invalid CONNECT packet size\n";
#endif
    return;
  }
#if SCU_UDP_DEBUG_LOG_ENABLED == 1
  std::cerr << "SUBCOMMAND: " << SCU_AS(int, udp_data.data[header.data_offset]) << '\n';
#endif
  switch (SCU_AS(Code::ConnectionSubCommands, udp_data.data[header.data_offset])) {
    case Code::ConnectionSubCommands::CONNECT: {
        if (get_connection(udp_data.ip, udp_data.port)) {
          send_or_panic(std::nullopt, _mock_sequence_number, Code::CONNECT,
                      udp_data.ip, udp_data.port,
            { AS_BYTE(Code::ConnectionSubCommands::ALREADY_CONNECTED) },
                    "Failed to send ALREADY_CONNECTED response");
        } else if (_auto_accept.load(std::memory_order_relaxed)) {
#if SCU_UDP_DEBUG_LOG_ENABLED == 1
          std::cerr << "CONNECTING\n";
#endif
          std::shared_ptr<ScorpioUdpConnection> new_connection(new ScorpioUdpConnection(
                      udp_data.ip, udp_data.port, shared_from_this()));
          new_connection->_start_signal.notify(100000);
          new_connection->_state.store(ScorpioUdpConnection::State::CONNECTING);
          _connections.insert({ { udp_data.ip, udp_data.port }, new_connection });
          SCU_ASSERT(_new_connections, "Channel must exist if auto accept is enabled");
          _new_connections->send<true>(new_connection);
          send_or_panic(std::nullopt, _mock_sequence_number, Code::CONNECT,
                      udp_data.ip, udp_data.port,
            { AS_BYTE(Code::ConnectionSubCommands::ACCEPTED) },
                      "Failed to send ACCEPTED response");
          new_connection->connected();
        } else {
          send_or_panic(std::nullopt, _mock_sequence_number, Code::CONNECT,
                      udp_data.ip, udp_data.port,
            { AS_BYTE(Code::ConnectionSubCommands::REJECTED) },
                      "Failed to send REJECTED response");
        }
      } break;
    case Code::ConnectionSubCommands::ACCEPTED: {
        auto connection = get_connection(udp_data.ip, udp_data.port);
        if (!connection) {
#if SCU_UDP_DEBUG_LOG_ENABLED == 1
          std::cerr << "Received ACCEPTED for non-existing connection\n";
#endif
          // TODO(@Igor): Handle error properly
        } else if (!connection->connected()) {
#if SCU_UDP_DEBUG_LOG_ENABLED == 1
          std::cerr << "Received ACCEPTED for connection not in CONNECTING state\n";
#endif
          // TODO(@Igor): Handle error properly
        }
      } break;
    case Code::ConnectionSubCommands::REJECTED: {
        auto connection = get_connection(udp_data.ip, udp_data.port);
        if (!connection) {
          // TODO(@Igor): Handle error properly
#if SCU_UDP_DEBUG_LOG_ENABLED == 1
          std::cerr << "Got connection response for non existing connection request\n";
#endif
        } else {
          connection->_state = ScorpioUdpConnection::State::REJECTED;
        }
      } break;
    case Code::ConnectionSubCommands::ALREADY_CONNECTED: {
        auto connection = get_connection(udp_data.ip, udp_data.port);
        if (!connection) {
          // TODO(@Igor): Handle properly
#if SCU_UDP_DEBUG_LOG_ENABLED == 1
          std::cerr << "Received 'ALREADY_CONNECTED' packet from non existing connection\n";
#endif
        }
      } break;
    default: {
        // TODO(@Igor): Handle error properly
#if SCU_UDP_DEBUG_LOG_ENABLED == 1
        std::cerr << "Received unknown connect packet\n";
#endif
      } break;
  }
}

SCU_HOT void ScorpioUdp::process_packet(UdpData udp_data) {
  auto header_opt = parse_header(udp_data.data);
  if (SCU_UNLIKELY(header_opt.is_err())) {
    // TODO(@Igor): Handle error properly
#if SCU_UDP_DEBUG_LOG_ENABLED == 1
    std::cerr << "Failed to parse UDP packet header: " << header_opt.err_value() << "\n";
#endif
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
    default: {
        if (auto connection_opt = get_connection(udp_data.ip, udp_data.port);
          connection_opt != nullptr && connection_opt->state() == ScorpioUdpConnection::State::CONNECTED) {
          connection_opt->_incoming_packets.send<true>({ header, std::move(udp_data) });
        } else {
          // Received connection-oriented packet for non-existing connection
          // SCU_UNIMPLEMENTED();  // This is harmless so, just ignore it for now
        }
      } break;
  }
}

void ScorpioUdp::pull_awaiting_connections(std::weak_ptr<ScorpioUdpConnection> connection_weak) {
  if (auto connection = connection_weak.lock()) {
    if (get_connection(connection->remote_ip(), connection->remote_port())) {
      connection->panic("Connection already exists");
    } else {
      SCU_ASSERT(connection->state() == ScorpioUdpConnection::State::NEW,
          "Connection in invalid state");
      send_or_panic(std::nullopt, _mock_sequence_number, Code::CONNECT, connection->remote_ip(),
          connection->remote_port(), { AS_BYTE(Code::ConnectionSubCommands::CONNECT) });
      connection->_state.store(ScorpioUdpConnection::State::CONNECTING, std::memory_order_relaxed);
      _connections.insert({ { connection->remote_ip(), connection->remote_port() }, std::move(connection) });
#if SCU_UDP_DEBUG_LOG_ENABLED == 1
      std::cerr << "Added new connection " << connection->remote_ip().str() << ":" << connection->remote_port() <<
        "\n";
#endif
    }
  } else {
#if SCU_UDP_DEBUG_LOG_ENABLED == 1
    std::cerr << "Awaiting connection weak_ptr is expired\n";
#endif
  }
}

std::shared_ptr<ScorpioUdpConnection> scorpio_utils::network::ScorpioUdp::get_connection(
  Ipv4 remote_ip,
  Port remote_port) {
  auto connection_iter = _connections.find({ remote_ip, remote_port });
  for (auto& [key, weak_conn] : _connections) {
    if (auto conn = weak_conn.lock()) {
#if SCU_UDP_DEBUG_LOG_ENABLED == 1
      std::cerr << "Connection: " << key.first.str() << ":" << key.second << " State: "
                << SCU_AS(int, conn->state()) << "\n";
#endif
    } else {
#if SCU_UDP_DEBUG_LOG_ENABLED == 1
      std::cerr << "Connection: " << key.first.str() << ":" << key.second << " State: expired\n";
#endif
    }
  }
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
#if SCU_UDP_DEBUG_LOG_ENABLED == 1
      std::cerr << "No connection found for " << remote_ip.str() << ":" << remote_port << "\n";
#endif
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

SCU_HOT std::optional<std::pair<size_t, std::vector<std::vector<uint8_t>>>> ScorpioUdp::generate_packets(
  std::optional<StreamNumber> stream_number,
  std::atomic<size_t>& sequence_number,
  Code code,
  const std::vector<uint8_t>& data) {
  const auto header_without_frames_left_size = calculate_header_without_frames_left_size(code);
  const auto packets_to_send = packets_count(data.size(), header_without_frames_left_size);
  if (SCU_UNLIKELY(!_socket.is_open() || packets_to_send > 65537)) {
#if SCU_UDP_DEBUG_LOG_ENABLED == 1
    std::cerr << "Socket is not open or data is too large\n";
#endif
    return { };
  }
  std::vector<std::vector<uint8_t>> packets;
  packets.reserve(packets_to_send);
  if (code.is_command_for_stream() != stream_number.has_value()) {
    panic("Stream number is required for command: " + std::to_string(static_cast<CodeType>(code)));
  }
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
        stream_number.value(), packets.back(), packet_pos), "Failed to convert stream number to network format");
        SCU_DO_AND_ASSERT(host_to_network(packet_seq++, packets.back(), packet_pos),
          "Failed to convert sequence number to network format");
      }
    };
  const auto packet_size_without_frames_left = MAX_PACKET_SIZE - header_without_frames_left_size;
  const auto packet_size_with_frames_left = packet_size_without_frames_left - sizeof(FramesLeft);
  while (data.size() - current > packet_size_without_frames_left) {
    packets.back().resize(MAX_PACKET_SIZE);
    generate_header();
    size_t frames_left = (data.size() - current - packet_size_without_frames_left) / packet_size_with_frames_left;
    if (!host_to_network(static_cast<decltype(MessageHeader::frames_left)::value_type>(frames_left), packets.back(),
      packet_pos)) {
      panic("Failed to convert frames left to network format");
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
  std::copy(data.data() + current, data.data() + data.size(), packets.back().data() + packet_pos);
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
    send(remote_ip, remote_port, std::move(packet));
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
  std::shared_ptr<ScorpioUdpConnection> connection(new ScorpioUdpConnection(ip, port, shared_from_this()));
  connection->_start_signal.notify(100000);
  _awaiting_connections_channel.send<true>(connection);
  return connection;
}

#if defined(SCORPIO_UTILS_SUDP_LOG_TO_FILE) && SCORPIO_UTILS_SUDP_LOG_TO_FILE == 1
void ScorpioUdp::log_to_file(std::string&& message) {
  _logger.log(std::move(message));
}
#endif

// ========================= ScorpioUdpConnection implementation =======================

ScorpioUdpConnection::ScorpioUdpConnection(Ipv4 remote_ip, Port remote_port, std::shared_ptr<ScorpioUdp> parent)
: _remote_ip(remote_ip),
  _remote_port(remote_port),
  _sequence_number(0),
  _state(State::NEW),
  _parent(std::move(parent)),
  _stop(false),
  _time_provider([]() {
      auto result = get_time_provider();
      result->set_time_offset(HEARTBEAT_PERIOD / 2);
      return result;
    }()),
  _last_received_packet_time(_time_provider->get_time()),
  _processing_thread(&ScorpioUdpConnection::processing_thread, this),
  _next_stream_to_heartbeat(0) {
}

bool ScorpioUdpConnection::connected() {
#if SCU_UDP_DEBUG_LOG_ENABLED == 1
  std::cerr << "MAYBE CONNECTED TO: " << _remote_ip << ":" << _remote_port << '\n';
#endif
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
#if SCU_UDP_DEBUG_LOG_ENABLED == 1
  std::cerr << "CREATE STREAM PACKET RECEIVED\n";
#endif
  if (data.data.size() - header.data_offset < 1) {
    // TODO(@Igor): Handle error properly
#if SCU_UDP_DEBUG_LOG_ENABLED == 1
    std::cerr << "Invalid CREATE_STREAM packet size\n";
#endif
    return;
  }
  size_t offset = header.data_offset;
  switch (SCU_AS(Code::CreateStreamSubCommands, data.data[offset++])) {
    case Code::CreateStreamSubCommands::CREATE: {
        Code::CreateStreamSubCommands response_code;
        StreamNumber stream_number;
        if (!network_to_host(data.data, &stream_number, offset)) {
          // TODO(@Igor): Handle error properly
#if SCU_UDP_DEBUG_LOG_ENABLED == 1
          std::cerr << "Failed to parse CREATE_STREAM stream number\n";
#endif
          return;
        }
        auto qos_opt = parse_qos(data.data, offset);
        if (!qos_opt) {
          // TODO(@Igor): Handle error properly
#if SCU_UDP_DEBUG_LOG_ENABLED == 1
          std::cerr << "Failed to parse CREATE_STREAM QoS\n";
#endif
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
          } else if (!_auto_accept_stream.load(std::memory_order_relaxed)) {
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
            _streams[stream_number] = new_stream;
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
#if SCU_UDP_DEBUG_LOG_ENABLED == 1
          std::cerr << "Failed to parse ACCEPT CREATE_STREAM stream number\n";
#endif
          return;
        }
        auto stream = _streams[stream_number].lock();
        if (!stream) {
          // TODO(@Igor): Handle error properly
#if SCU_UDP_DEBUG_LOG_ENABLED == 1
          std::cerr << "Received ACCEPT for non-existing stream\n";
#endif
          return;
        }
        if (!stream->is_alive()) {
          // TODO(@Igor): Handle error properly
#if SCU_UDP_DEBUG_LOG_ENABLED == 1
          std::cerr << "Received ACCEPT for stream not in CREATING state\n";
#endif
          return;
        }
        auto qos_opt = parse_qos(data.data, offset);
        if (!qos_opt) {
          // TODO(@Igor): Handle error properly
#if SCU_UDP_DEBUG_LOG_ENABLED == 1
          std::cerr << "Failed to parse ACCEPT CREATE_STREAM QoS\n";
#endif
          return;
        }
        if (*qos_opt != stream->qos()) {
          // TODO(@Igor): Handle error properly
          stream->panic("Peer accepted stream with different QoS");
#if SCU_UDP_DEBUG_LOG_ENABLED == 1
          std::cerr << "Received ACCEPT for stream with different QoS\n";
          std::cerr << "Expected reliability: " << SCU_AS(int, stream->qos().reliability)
                    << " got: " << SCU_AS(int, qos_opt->reliability) << "\n";
          std::cerr << "Expected depth: " << stream->qos().depth
                    << " got: " << qos_opt->depth << "\n";
#endif
          return;
        }
        stream->connected();
      } break;
    case Code::CreateStreamSubCommands::REJECT_SIMILAR_EXISTED:
      [[fallthrough]];
    case Code::CreateStreamSubCommands::REJECT: {
        StreamNumber stream_number;
        if (!network_to_host(data.data, &stream_number, offset)) {
          // TODO(@Igor): Handle error properly
#if SCU_UDP_DEBUG_LOG_ENABLED == 1
          std::cerr << "Failed to parse REJECT CREATE_STREAM stream number\n";
#endif
          return;
        }
        auto stream = _streams[stream_number].lock();
        if (!stream) {
          // TODO(@Igor): Handle error properly
#if SCU_UDP_DEBUG_LOG_ENABLED == 1
          std::cerr << "Received REJECT for non-existing stream\n";
#endif
          return;
        }
        if (!stream->is_alive()) {
          // TODO(@Igor): Handle error properly
#if SCU_UDP_DEBUG_LOG_ENABLED == 1
          std::cerr << "Received REJECT for stream not in CREATING state\n";
#endif
          return;
        }
        auto qos_opt = parse_qos(data.data, offset);
        if (!qos_opt) {
          // TODO(@Igor): Handle error properly
#if SCU_UDP_DEBUG_LOG_ENABLED == 1
          std::cerr << "Failed to parse REJECT CREATE_STREAM QoS\n";
#endif
          return;
        }
        if (*qos_opt != stream->qos()) {
          // TODO(@Igor): Handle error properly
#if SCU_UDP_DEBUG_LOG_ENABLED == 1
          std::cerr << "Received REJECT for stream with different QoS\n";
#endif
          return;
        }
        stream->_state.store(ScorpioUdpStream::State::REJECTED, std::memory_order_relaxed);
      } break;
    case Code::CreateStreamSubCommands::ALREADY_EXISTS: {
        StreamNumber stream_number;
        if (!network_to_host(data.data, &stream_number, offset)) {
          // TODO(@Igor): Handle error properly
#if SCU_UDP_DEBUG_LOG_ENABLED == 1
          std::cerr << "Failed to parse ALREADY_EXISTS CREATE_STREAM stream number\n";
#endif
          return;
        }
      } break;
    default: {
        // TODO(@Igor): Handle error properly
#if SCU_UDP_DEBUG_LOG_ENABLED == 1
        std::cerr << "Received unknown CREATE_STREAM packet\n";
#endif
      } break;
  }
}

void ScorpioUdpConnection::close_stream_packet_handler(const MessageHeader& header, UdpData&& data) {
  if (data.data.size() - header.data_offset != sizeof(Code::CloseStreamSubCommands) + sizeof(StreamNumber)) {
    // TODO(@Igor): Handle error properly
#if SCU_UDP_DEBUG_LOG_ENABLED == 1
    std::cerr << "Invalid CLOSE_STREAM packet size\n";
#endif
    return;
  }
  size_t offset = header.data_offset;
  Code::CloseStreamSubCommands subcode;
  subcode = SCU_AS(Code::CloseStreamSubCommands, data.data[offset++]);
  StreamNumber stream_number;
  if (!network_to_host(data.data, &stream_number, offset)) {
    // TODO(@Igor): Handle error properly
    std::cerr << "Failed to parse CLOSE_STREAM stream number\n";
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
        SCU_DO_AND_ASSERT(host_to_network<uint16_t>(stream_number, response,
                                                 response_offset), "Failed to convert stream number to network format");
        send(Code::CLOSE_STREAM, response);
      } break;
    case Code::CloseStreamSubCommands::CLOSED:
      [[fallthrough]];
    case Code::CloseStreamSubCommands::ALREADY_CLOSED: {
        if (stream) {
          std::ignore = stream->closed();
        }
      } break;
    default: {
        // TODO(@Igor): Handle error properly
#if SCU_UDP_DEBUG_LOG_ENABLED == 1
        std::cerr << "Received unknown CLOSE_STREAM packet\n";
#endif
      } break;
  }
}

void ScorpioUdpConnection::heartbeat_packet_handler(const MessageHeader& header, UdpData&& data) {
  size_t pos = header.data_offset;
  StreamNumber stream_num;
  while (network_to_host(data.data, &stream_num, pos)) {
    if (auto stream = get_stream(stream_num)) {
      stream->handle_heartbeat_data(data.data, pos);
    } else {
      // TODO(@Igor): Handle non-existing stream found inside heartbeat
    }
  }
}

SCU_HOT void ScorpioUdpConnection::handle_new_packet(const MessageHeader& header, UdpData&& data) {
  switch (header.command) {
    case Code::DISCONNECT: {
        _state.store(State::CLOSED, std::memory_order_relaxed);
        send_or_panic(Code::DISCONNECT, { AS_BYTE(Code::DisconnectSubCommands::ACCEPTED) });
        // TODO(@Igor): Better handling
      }
      break;
    case Code::CREATE_STREAM: {
        create_stream_packet_handler(header, std::move(data));
      } break;
    case Code::STREAM_DATA: {
        auto stream = get_stream(header.stream_number.value());
#if SCU_UDP_DEBUG_LOG_ENABLED == 1
        std::cerr << "STREAM DATA PACKET RECEIVED for stream number " << header.stream_number.value() << " data size "
                  << data.data.size() << " bytes and stream " << (stream ? "exists" : "does not exist") << "\n";
#endif
        if (!stream) {
          // TODO(@Igor): Handle error properly
#if SCU_UDP_DEBUG_LOG_ENABLED == 1
          std::cerr << "Received STREAM_DATA for non-existing stream\n";
#endif
          return;
        }
        stream->handle_data_packet(header, std::move(data));
      } break;
    case Code::ERROR: {
        // SCU_UNIMPLEMENTED();
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
#if SCU_UDP_DEBUG_LOG_ENABLED == 1
      std::cerr << "Unknown command received: " << std::to_string(static_cast<CodeType>(header.command)) << "\n";
#endif
      return;
  }
}

void ScorpioUdpConnection::pull_awaiting_streams(std::shared_ptr<scorpio_utils::network::ScorpioUdpStream> stream) {
  auto current_weak = _streams[stream->_stream_number];
  auto current = current_weak.lock();
  if (current && current->is_alive()) {
    panic("Stream with the same stream number already exists");
  } else {
    SCU_DO_AND_ASSERT(stream->send_create_packet(), "First CREATE_STREAM packet send failed");
    send_or_panic(Code::CREATE_STREAM, { }, "Failed to send CREATE_STREAM command");
    stream->_state.store(ScorpioUdpStream::State::CREATING, std::memory_order_relaxed);
    _streams[stream->_stream_number] = stream;
  }
}

void ScorpioUdpConnection::process_packets(
  std::pair<scorpio_utils::network::MessageHeader,
  scorpio_utils::network::UdpData> packet) {
#if SCU_UDP_DEBUG_LOG_ENABLED == 1
  std::cerr << "Processing packet from " << packet.second.ip.str() << ":" << packet.second.port << "of size "
            << packet.second.data.size() << " bytes\n";
#endif
  _last_received_packet_time.store(_time_provider->get_time(), std::memory_order_relaxed);
  handle_new_packet(packet.first, std::move(packet.second));
}

void ScorpioUdpConnection::send_heartbeat() {
  const auto time_since_last_packet = _time_provider->get_time() -
    _last_received_packet_time.load(std::memory_order_relaxed);
  if (SCU_UNLIKELY(time_since_last_packet > TIMEOUT)) {
    panic("No packets received for 5 seconds");
  }
  // *  - While giving streams some CPU time look only for active streams (not iterate while 65536 streams)
  for (auto& weak_stream : _streams) {
    if (auto stream = weak_stream.lock()) {
      stream->update();
    }
  }
  std::vector<uint8_t> heartbeat_data;
  const auto first_stream = _next_stream_to_heartbeat;
  std::shared_ptr<ScorpioUdpStream> first_handled;
  constexpr size_t packet_size = MAX_PACKET_SIZE - calculate_header_without_frames_left_size(Code::HEARTBEAT);
  heartbeat_data.reserve(packet_size);
  do {
    if (auto stream = _streams[_next_stream_to_heartbeat++].lock()) {
      if (!first_handled) {
        first_handled = stream;
      } else if (stream == first_handled) {
        break;
      }
      if (!stream->append_heartbeat_data(heartbeat_data) || _next_stream_to_heartbeat == first_stream) {
        break;
      }
    }
  } while (_next_stream_to_heartbeat != first_stream);
  send_or_panic(Code::HEARTBEAT, heartbeat_data);
}

void ScorpioUdpConnection::processing_thread() {
  try {
    _start_signal.wait();
    const auto self_weak = weak_from_this();
    std::shared_ptr<ScorpioUdpConnection> self;
    while (SCU_LIKELY((self = self_weak.lock()) && !_stop.load(std::memory_order_relaxed)) &&
      _state.load(std::memory_order_relaxed) == State::NEW) {
      SCU_DEFER([&self] { self.reset(); });
      std::this_thread::sleep_for(std::chrono::nanoseconds(HEARTBEAT_PERIOD / 4));
    }
    std::this_thread::sleep_for(std::chrono::nanoseconds(HEARTBEAT_PERIOD));
    while (SCU_LIKELY((self = self_weak.lock()) && !_stop.load(std::memory_order_relaxed)) &&
      _state.load(std::memory_order_relaxed) == State::CONNECTING) {
      SCU_DEFER([&self] { self.reset(); });
      send_or_panic(Code::CONNECT, { AS_BYTE(Code::ConnectionSubCommands::CONNECT) });
      std::this_thread::sleep_for(std::chrono::nanoseconds(HEARTBEAT_PERIOD));
    }
    threading::EagerSelectTimeout timeout(HEARTBEAT_PERIOD, _time_provider);
    timeout.start();
    while (SCU_LIKELY((self = self_weak.lock()) && !_stop.load(std::memory_order_relaxed))) {
      SCU_DEFER([&self] { self.reset(); });
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
    }
  } catch (const PanicException&) {
  } catch (const threading::ClosedChannelException&) {
  }
#if SCU_UDP_DEBUG_LOG_ENABLED == 1
  std::cerr << "Processing thread exited\n";
#endif
}

SCU_COLD SCU_NORETURN void ScorpioUdpConnection::panic(std::string&& message) {
  std::unique_lock<std::mutex> lock(_panic_mutex, std::try_to_lock);
  if (!lock.owns_lock()) {
    // Lock mutex to delay returning from panic so, there is _panic_message set
    lock.lock();
    throw PanicException();
  }
#if SCU_UDP_DEBUG_LOG_ENABLED == 1
  std::cerr << "Connection panic: " << message << "\n";
#endif
  _panic_message = std::move(message);
  _panic.store(true, std::memory_order_release);
  _state.store(State::ERROR, std::memory_order_relaxed);
  _stop.store(true, std::memory_order_relaxed);
  throw PanicException();
}

auto ScorpioUdpConnection::generate_packets(
  Code code, const std::vector<uint8_t>& data, std::optional<StreamNumber> stream_number,
  std::optional<std::reference_wrapper<std::atomic<size_t>>> sequence_number) {
  return _parent->generate_packets(
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
  return SCU_LIKELY(_parent->is_running() && _parent->send(_remote_ip, _remote_port, std::move(packet)));
}

void ScorpioUdpConnection::send_or_panic(
  Code code, const std::vector<uint8_t>& data, std::string&& message) {
  if (SCU_UNLIKELY(!send(code, data))) {
    panic(std::move(message));
  }
}

bool ScorpioUdpConnection::close() {
  std::unique_lock<std::mutex> lock(_close_mutex, std::try_to_lock);
  if (!lock.owns_lock()) {
    // Another thread is already closing the connection
    // Lock mutex to delay returning from panic so, there is _panic_message set
    lock.lock();
    return false;
  }
  for (auto& weak_stream : _streams) {
    if (auto stream = weak_stream.lock()) {
      stream->close();
    }
  }
  _stop.store(true, std::memory_order_relaxed);
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

#if defined(SCORPIO_UTILS_SUDP_LOG_TO_FILE) && SCORPIO_UTILS_SUDP_LOG_TO_FILE == 1
void ScorpioUdpConnection::log_to_file(std::string&& message) {
  _parent->log_to_file(std::move(message));
}
#endif

// ========================= ScorpioUdpStream implementation ===========================

ScorpioUdpStream::ScorpioUdpStream(
  StreamNumber stream_number, StreamQoS stream_qos,
  std::shared_ptr<ScorpioUdpConnection> parent)
: _stream_number(stream_number),
  _stream_qos(stream_qos),
  _sent_history(stream_qos.is_reliable() ? stream_qos.depth_value() + QOS_DEPTH_SAFETY_BUFFER : 0),
  _parent(parent),
  _sequence_number(0),
  _least_non_delivered_seq_number(0),
  _state(State::NEW),
  _creation_tries(0),
  _orderer(stream_qos.depth_value()),
  _partial_data(std::in_place_index_t<0>{ }),
  _sequence_complement(0),
  _last_greatest_sequence_number(0) {
  SCU_ASSERT(stream_qos.is_reliable() || stream_qos.depth == 0, "Unreliable streams must have depth 0 (no ordering)");
  if (!stream_qos.is_reliable()) {
    _partial_data.emplace<1>();
  }
}

ScorpioUdpStream::~ScorpioUdpStream() {
  close();
  bool expected = true;
  SCU_ASSERT(_parent->_stream_exists[_stream_number].compare_exchange_strong(
      expected,
      false,
      std::memory_order_relaxed,
      std::memory_order_relaxed
    ), "Stream existence flag was already false on destruction");
}

bool ScorpioUdpStream::close() {
  State expected = state();
  while (is_active()) {
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
  if (SCU_UNLIKELY(!is_active())) {
#if SCU_UDP_DEBUG_LOG_ENABLED == 1
    std::cerr << "Attempted to send on inactive stream state: " << static_cast<int>(state()) << "\n";
#endif
    return false;
  }
  auto packets = _parent->generate_packets(
    code,
    data,
    _stream_number,
    _sequence_number
  );
  if (SCU_UNLIKELY(!packets.has_value())) {
#if SCU_UDP_DEBUG_LOG_ENABLED == 1
    std::cerr << "Failed to generate packets for sending " << data.size() << " bytes on stream "
              << _stream_number << "\n";
#endif
    return false;
  }
  auto seq = packets->first;
  for (auto& packet : packets->second) {
    if (_stream_qos.is_reliable()) {
      const auto pos = seq % _sent_history.size();
      const auto least_non_delivered = _least_non_delivered_seq_number.load(std::memory_order_relaxed);
      if (seq - least_non_delivered >= _sent_history.size()) {
#if SCU_UDP_DEBUG_LOG_ENABLED == 1
        std::cerr << "QoS depth exceeded\n" <<
          "Least non-delivered: " << least_non_delivered << ", sent seq: " << seq << "\n";
#endif
        panic("QoS depth exceeded " +
          std::to_string(least_non_delivered) + ", sent seq: " + std::to_string(seq));
        return false;
      }
      _sent_history[pos] = packet;
    }
    if (SCU_UNLIKELY(!_parent->send(std::move(packet)))) {
      return false;
    }
    ++seq;
  }
  std::atomic_thread_fence(std::memory_order_release);
  return true;
}

SCU_COLD void ScorpioUdpStream::panic(std::string&& message) {
  static std::mutex panic_mutex;
  std::unique_lock<std::mutex> lock(panic_mutex, std::try_to_lock);
  if (!lock.owns_lock()) {
    // Lock mutex to delay returning from panic so, there is _panic_message set
    lock.lock();
    return;
  }
  if (_state.load(std::memory_order_relaxed) == State::ERROR) {
    return;
  }
#if SCU_UDP_DEBUG_LOG_ENABLED == 1
  std::cerr << "Stream panic: " << message << "\n";
#endif
  _panic_message = std::move(message);
  _state.store(State::ERROR, std::memory_order_release);
}

bool ScorpioUdpStream::send_create_packet() {
  if (SCU_UNLIKELY(_creation_tries.fetch_add(1, std::memory_order_relaxed) > 10)) {
    panic("Failed to create stream after 10 tries");
    return false;
  }
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
  return true;
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
    _parent->_streams[_stream_number].reset();
    return true;
  }
  return false;
}

bool ScorpioUdpStream::send_close_packet() {
  std::vector<uint8_t> packet;
  packet.resize(3);
  packet[0] = AS_BYTE(Code::CloseStreamSubCommands::CLOSE);
  size_t offset = 1;
  SCU_DO_AND_ASSERT(host_to_network<uint16_t>(_stream_number, packet,
                                       offset), "Failed to convert stream number to network format");
  return send(Code::CLOSE_STREAM, packet);
}

void ScorpioUdpStream::update() {
  switch (state()) {
    case State::CREATING: {
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
    if (current_time - data.receive_time > UNRELIABLE_DATA_EXPIRY_NS) {
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
#if SCU_UDP_DEBUG_LOG_ENABLED == 1
    std::cerr << "Processing ordered packet: " << seq_number << "\n";
#endif
    switch (_orderer.add(seq_number, { header, std::move(data.data) })) {
      case OrdererAddResult::TOO_NEW:
        panic("Received packet is too new or too old");
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
#if SCU_UDP_DEBUG_LOG_ENABLED == 1
      std::cerr << "Current seq number: " << seq_number << ", first frame seq: " << *iter << "\n";
#endif
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
      complete_data.reserve(SCU_AS(size_t, MAX_PACKET_SIZE * (iter->second.header.frames_left.value() + 2)));
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
    complement = SCU_AS(size_t, _sequence_complement++);
    _last_greatest_sequence_number = v;
  } else if (_last_greatest_sequence_number < v) {
    complement = SCU_AS(size_t, _sequence_complement);
    _last_greatest_sequence_number = v;
  } else {
    complement = SCU_AS(size_t, _sequence_complement);
  }
  return complement * SCU_AS(size_t, std::numeric_limits<SeqNumber>::max()) + SCU_AS(size_t, v);
}

bool ScorpioUdpStream::append_heartbeat_data(std::vector<uint8_t>& heartbeat_data) const {
  constexpr size_t packet_size = MAX_PACKET_SIZE - calculate_header_without_frames_left_size(Code::HEARTBEAT);
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
  SCU_ASSERT(required_size >= prefix_size + sizeof(SeqNumber),
    "Required size is too small to fit any heartbeat data");
  if (!heartbeat_data.empty() && heartbeat_data.size() + required_size > packet_size) {
    return false;
  }

  auto pos = heartbeat_data.size();
  heartbeat_data.resize(pos + required_size);
  SCU_DO_AND_ASSERT(host_to_network(_stream_number, heartbeat_data, pos),
    "Failed to convert stream number to network format");
  const auto contained_count = (required_size - prefix_size) / (sizeof(SeqNumber) * 2);
  heartbeat_data[pos++] = AS_BYTE(contained_count);
  SCU_DO_AND_ASSERT(host_to_network(SCU_AS(SeqNumber, contained.front().second), heartbeat_data, pos),
    "Failed to convert sequence number to network format");

  for (size_t i = 1; i < contained_count; ++i) {
    const auto& [begin, end] = contained[i];
    SCU_DO_AND_ASSERT(host_to_network(SCU_AS(SeqNumber, begin), heartbeat_data, pos),
      "Failed to convert sequence number to network format");
    SCU_DO_AND_ASSERT(host_to_network(SCU_AS(SeqNumber, end), heartbeat_data, pos),
      "Failed to convert sequence number to network format");
  }
  return true;
}

void ScorpioUdpStream::handle_heartbeat_data(const std::vector<uint8_t>& data, size_t& pos) {
  if (SCU_UNLIKELY(!_stream_qos.is_reliable())) {
#if SCU_UDP_DEBUG_LOG_ENABLED == 1
    std::cerr << "Received heartbeat for unreliable stream\n";
#endif
    return;
  }
  if (SCU_UNLIKELY(data.size() <= pos)) {
    return;
  }
  uint8_t range_count = data[pos++];
  SeqNumber end;
  if (SCU_UNLIKELY(!network_to_host(data, &end, pos))) {
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
  while (range_count--) {
    if (SCU_UNLIKELY(!network_to_host(data, &begin, pos))) {
      return;
    }
    const auto begin_transformed = least_significant_bytes_to_val(greatest_seen_val, begin);
    for (auto i = least_significant_bytes_to_val(greatest_seen_val, end); i < begin_transformed; ++i) {
      if (i <= _sequence_number.load(std::memory_order_relaxed) - _stream_qos.depth_value()) {
        continue;
      }
      const auto& packet = _sent_history[i % _sent_history.size()];
      if (SCU_UNLIKELY(!packet.has_value())) {
        panic("Peer expects unsend message");
        return;
      }
      if (SCU_UNLIKELY(!_parent->send(clone(*packet)))) {
        panic("Failed to resend packet (maybe connection or socket is closed?)");
        return;
      }
    }
    if (SCU_UNLIKELY(!network_to_host(data, &end, pos))) {
      return;
    }
    greatest_seen_val = least_significant_bytes_to_val(begin_transformed, end);
  }
}

#if defined(SCORPIO_UTILS_SUDP_LOG_TO_FILE) && SCORPIO_UTILS_SUDP_LOG_TO_FILE == 1
void ScorpioUdpStream::log_to_file(std::string&& message) {
  _parent->log_to_file(std::move(message));
}
#endif
