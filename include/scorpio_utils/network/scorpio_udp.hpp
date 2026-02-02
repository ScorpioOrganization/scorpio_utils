#pragma once

#include <array>
#include <atomic>
#include <cstdint>
#if defined(SCORPIO_UTILS_SUDP_LOG_TO_FILE) && SCORPIO_UTILS_SUDP_LOG_TO_FILE == 1
#include <fstream>
#endif
#include <functional>
#include <limits>
#include <map>
#include <memory>
#include <mutex>
#include <optional>
#include <set>
#include <string>
#include <string_view>
#include <thread>
#include <unordered_map>
#include <utility>
#include <variant>
#include <vector>
#include "scorpio_utils/expected.hpp"
#if defined(SCORPIO_UTILS_SUDP_LOG_TO_FILE) && SCORPIO_UTILS_SUDP_LOG_TO_FILE == 1
#include "scorpio_utils/logger.hpp"
#endif
#include "scorpio_utils/network/orderer.hpp"
#include "scorpio_utils/network/udp.hpp"
#include "scorpio_utils/threading/channel.hpp"
#include "scorpio_utils/threading/eager_select.hpp"
#include "scorpio_utils/threading/signal.hpp"
#include "scorpio_utils/time_provider/lazy_time_provider.hpp"
#include "scorpio_utils/types.hpp"

namespace scorpio_utils::network {
struct UdpData {
  scorpio_utils::network::Ipv4 ip;
  Port port;
  std::vector<uint8_t> data;
};

using CodeType = uint8_t;
using SeqNumber = uint32_t;
using SeqNumberComplement = uint32_t;
using StreamNumber = uint16_t;
using FramesLeft = uint16_t;

struct MessageHeader {
  size_t data_offset;
  std::optional<StreamNumber> stream_number;
  std::optional<SeqNumber> seq_number;
  std::optional<FramesLeft> frames_left;
  uint8_t command;
  bool is_first;
};

struct Code {
  enum Values : CodeType {
    // COMMANDS
    PING,
    CONNECT,
    DISCONNECT,
    STATUS,
    ERROR,
    HEARTBEAT,
    CREATE_STREAM,
    CLOSE_STREAM,
    STREAM_DATA,

    // FLAGS
    FIRST = 0x40,
    NOT_LAST = 0x80,
  };

  enum class PingSubCommands : uint8_t {
    PING,
    PONG,
  };

  enum class ConnectionSubCommands : uint8_t {
    CONNECT,
    ACCEPTED,
    REJECTED,
    ALREADY_CONNECTED,
  };

  enum class DisconnectSubCommands : uint8_t {
    ACCEPTED,
    REJECTED,
    ALREADY_DISCONNECTED,
  };

  enum class CreateStreamSubCommands : uint8_t {
    CREATE,
    ACCEPT,
    REJECT,
    REJECT_SIMILAR_EXISTED,
    ALREADY_EXISTS,
  };

  enum class CloseStreamSubCommands : uint8_t {
    CLOSE,
    CLOSED,
    ALREADY_CLOSED,
  };

  Values value;
  constexpr Code(Values v)  // NOLINT
  : value(v) { }
  constexpr Code(CodeType v)  // NOLINT
  : value(static_cast<Values>(v)) { }
  constexpr operator CodeType() const noexcept {
    return static_cast<CodeType>(value);
  }
  #define BITWISE_OPERATION(op) \
  constexpr Code operator op(const Code& other) const noexcept { \
    return value op other.value; \
  } \
  constexpr Code operator op(const CodeType& other) const noexcept { \
    return value op other; \
  }
  BITWISE_OPERATION(&)
  BITWISE_OPERATION(|)
  BITWISE_OPERATION(^)
  #undef BITWISE_OPERATION
  constexpr Code operator~() const noexcept {
    return static_cast<Values>(~value);
  }
  constexpr bool is_not_last() const noexcept {
    return (value & NOT_LAST) != 0;
  }
  constexpr Code get_command() const noexcept {
    return static_cast<Values>(value & static_cast<CodeType>(0x0f));
  }
  constexpr bool is_command() const noexcept {
    return (value & static_cast<CodeType>(0x0f)) != 0;
  }
  constexpr bool is_flag() const noexcept {
    return (value & static_cast<CodeType>(0x0f)) == 0;
  }
  constexpr bool is_command_for_stream() const noexcept {
    auto v = get_command();
    return v == STREAM_DATA ||
           v == CLOSE_STREAM;
  }
  constexpr bool is_connectionless() const noexcept {
    auto v = get_command();
    return v == PING ||
           v == CONNECT;
  }
  constexpr bool is_first() const noexcept {
    return (value & FIRST) != 0;
  }
};

class ScorpioUdp;
class ScorpioUdpConnection;
class ScorpioUdpStream;

class ScorpioUdpStream : public std::enable_shared_from_this<ScorpioUdpStream> {
public:
  enum class State : uint8_t {
    NEW,
    CREATING,
    CREATED,
    CLOSING,
    CLOSED,
    REJECTED,
    ERROR,
  };

  struct StreamQoS {
    uint16_t depth;
    enum class Reliability : uint8_t {
      UNRELIABLE,
      UNRELIABLE_LATEST_ONLY,
      RELIABLE_UNORDERED,
      RELIABLE_ORDERED,
    } reliability;

    SCU_ALWAYS_INLINE constexpr bool operator==(const StreamQoS& other) const noexcept {
      return depth == other.depth && reliability == other.reliability;
    }
    SCU_ALWAYS_INLINE constexpr bool operator!=(const StreamQoS& other) const noexcept {
      return !(*this == other);
    }

    SCU_ALWAYS_INLINE constexpr size_t depth_value() const noexcept {
      return SCU_AS(size_t, depth == 0 && reliability > Reliability::UNRELIABLE_LATEST_ONLY ? 65536 : depth);
    }
    SCU_ALWAYS_INLINE constexpr bool is_reliable() const noexcept {
      return reliability > Reliability::UNRELIABLE_LATEST_ONLY;
    }
    SCU_ALWAYS_INLINE constexpr bool is_supported() const noexcept {
      return reliability != Reliability::UNRELIABLE_LATEST_ONLY && reliability != Reliability::RELIABLE_UNORDERED;
    }
  };

private:
  friend class ScorpioUdpConnection;
  threading::Channel<std::vector<uint8_t>, 1024 * 1024> _receive;
  const StreamNumber _stream_number;
  const StreamQoS _stream_qos;
  std::vector<std::optional<std::vector<uint8_t>>> _sent_history;
  std::shared_ptr<ScorpioUdpConnection> _parent;
  std::atomic<size_t> _sequence_number;
  std::atomic<size_t> _least_non_delivered_seq_number;
  std::atomic<State> _state;
  std::atomic<size_t> _creation_tries;
  Orderer<std::pair<MessageHeader, std::vector<uint8_t>>> _orderer;
  struct UnreliableData {
    int64_t receive_time;
    MessageHeader header;
    std::vector<uint8_t> data;
  };
  struct UnreliablePartialData {
    std::set<size_t, std::greater<>> first_frames;
    std::map<size_t, UnreliableData> received_frames;
  };
  std::variant<std::vector<uint8_t>, UnreliablePartialData> _partial_data;
  SeqNumberComplement _sequence_complement;
  SeqNumber _last_greatest_sequence_number;
  std::string _panic_message;

#if defined(SCORPIO_UTILS_SUDP_LOG_TO_FILE) && SCORPIO_UTILS_SUDP_LOG_TO_FILE == 1
  void log_to_file(std::string&& message);
#endif

  size_t get_packet_number(SeqNumber seq) noexcept;

  bool send_create_packet();
  bool send_close_packet();
  ScorpioUdpStream(
    StreamNumber stream_number, StreamQoS stream_qos,
    std::shared_ptr<ScorpioUdpConnection> parent);

  bool send(Code code, const std::vector<uint8_t>& data);
  void panic(std::string&& message);

  void connected();
  bool closed();
  void update();
  void handle_data_packet(const MessageHeader& header, UdpData&& data);
  bool append_heartbeat_data(std::vector<uint8_t>& heartbeat_data) const;
  void handle_heartbeat_data(const std::vector<uint8_t>& data, size_t& pos);
  void remove_expired_unreliable_data();

public:
  SCU_ALWAYS_INLINE bool SCU_EAGER_SELECT_IS_READY() noexcept {
    return !_receive.SCU_EAGER_SELECT_IS_READY();
  }

  SCU_ALWAYS_INLINE decltype(auto) SCU_EAGER_SELECT_GET_VALUE() noexcept {
    return _receive.SCU_EAGER_SELECT_GET_VALUE();
  }

  SCU_ALWAYS_INLINE ~ScorpioUdpStream() {
    close();
  }

  bool send(const std::vector<uint8_t>& data);
  template<bool Wait = false>
  SCU_ALWAYS_INLINE auto receive() {
    return _receive.receive<Wait>();
  }
  bool close();
  SCU_ALWAYS_INLINE constexpr auto stream_id() const noexcept {
    return _stream_number;
  }
  SCU_ALWAYS_INLINE bool is_alive() const noexcept {
    return _state.load(std::memory_order_relaxed) < State::CLOSED;
  }
  SCU_ALWAYS_INLINE auto state() const noexcept {
    return _state.load(std::memory_order_relaxed);
  }
  SCU_ALWAYS_INLINE auto qos() const noexcept {
    return _stream_qos;
  }
  SCU_ALWAYS_INLINE auto is_active() const noexcept {
    return _state.load(std::memory_order_relaxed) == State::CREATED;
  }
  SCU_ALWAYS_INLINE auto is_panic() const noexcept {
    return _state.load(std::memory_order_relaxed) == State::ERROR;
  }
  inline std::optional<std::string_view> panic_message() const {
    if (_state.load(std::memory_order_acquire) == State::ERROR) {
      return _panic_message;
    }
    return std::nullopt;
  }
};

class ScorpioUdpConnection : public std::enable_shared_from_this<ScorpioUdpConnection> {
public:
  enum class State : uint8_t {
    NEW,
    CONNECTING,
    CONNECTED,
    CLOSED,
    REJECTED,
    ERROR,
  };

private:
  friend class ScorpioUdp;
  friend class ScorpioUdpStream;
  const Ipv4 _remote_ip;
  const Port _remote_port;
  std::atomic<size_t> _sequence_number;
  std::atomic<bool> _panic;
  std::atomic<State> _state;
  std::shared_ptr<ScorpioUdp> _parent;
  std::string _panic_message;
  threading::Channel<std::shared_ptr<ScorpioUdpStream>, 1024> _new_streams;
  std::atomic<bool> _auto_accept_stream;
  std::atomic<bool> _stop;
  std::shared_ptr<scorpio_utils::time_provider::LazyTimeProvider> _time_provider;
  std::atomic<int64_t> _last_received_packet_time;
  std::thread _processing_thread;
  std::mutex _panic_mutex;
  threading::Signal _start_signal;

#if defined(SCORPIO_UTILS_SUDP_LOG_TO_FILE) && SCORPIO_UTILS_SUDP_LOG_TO_FILE == 1
  void log_to_file(std::string&& message);
#endif

  bool connected();

  threading::Channel<std::pair<MessageHeader, UdpData>, 1024 * 1024> _incoming_packets;
  threading::Channel<std::shared_ptr<ScorpioUdpStream>, 1024> _awaiting_streams;
  std::array<std::weak_ptr<ScorpioUdpStream>, std::numeric_limits<StreamNumber>::max() + 1> _streams;
  std::array<std::atomic<bool>, std::numeric_limits<StreamNumber>::max() + 1> _stream_exists;
  std::vector<std::weak_ptr<ScorpioUdpStream>> _streams_being_created;
  uint16_t _next_stream_to_heartbeat;
  std::shared_ptr<ScorpioUdpStream> get_stream(StreamNumber);
  void handle_new_packet(const MessageHeader& header, UdpData&& data);
  void pull_awaiting_streams(std::shared_ptr<scorpio_utils::network::ScorpioUdpStream> stream);
  void create_stream_packet_handler(const MessageHeader& header, UdpData&& data);
  void close_stream_packet_handler(const MessageHeader& header, UdpData&& data);
  void heartbeat_packet_handler(const MessageHeader& header, UdpData&& data);
  void process_packets(std::pair<scorpio_utils::network::MessageHeader, scorpio_utils::network::UdpData> packet);
  void send_heartbeat();
  void processing_thread();

  ScorpioUdpConnection(Ipv4 remote_ip, Port remote_port, std::shared_ptr<ScorpioUdp> parent);
  void panic(std::string&& message);
  auto generate_packets(
    Code code, const std::vector<uint8_t>& data, std::optional<StreamNumber> stream_number = std::nullopt,
    std::optional<std::reference_wrapper<std::atomic<size_t>>> sequence_number = std::nullopt);
  bool send(
    Code code, const std::vector<uint8_t>& data, std::optional<StreamNumber> stream_number = std::nullopt,
    std::optional<std::reference_wrapper<std::atomic<size_t>>> sequence_number = std::nullopt);
  bool send(std::vector<uint8_t>&& packet);
  void send_or_panic(
    Code code, const std::vector<uint8_t>& data,
    std::string&& message = "Failed to send message in send");

  std::mutex _close_mutex;

public:
  SCU_ALWAYS_INLINE bool SCU_EAGER_SELECT_IS_READY() noexcept {
    return _awaiting_streams.SCU_EAGER_SELECT_IS_READY();
  }

  SCU_ALWAYS_INLINE decltype(auto) SCU_EAGER_SELECT_GET_VALUE() noexcept {
    return _awaiting_streams.SCU_EAGER_SELECT_GET_VALUE();
  }

  SCU_ALWAYS_INLINE ~ScorpioUdpConnection() {
    if (!close()) {
      _close_mutex.lock();
    }
  }

  bool close();

  [[nodiscard]] std::shared_ptr<ScorpioUdpStream> create_stream(
    StreamNumber stream_id,
    ScorpioUdpStream::StreamQoS qos);

  template<bool Wait = false>
  SCU_ALWAYS_INLINE auto get_accepted_stream() {
    return _new_streams.receive<Wait>();
  }

  SCU_ALWAYS_INLINE auto is_auto_accept_stream() const noexcept {
    return _auto_accept_stream.load(std::memory_order_relaxed);
  }
  SCU_ALWAYS_INLINE auto set_auto_accept_stream(bool auto_accept) noexcept {
    return _auto_accept_stream.exchange(auto_accept, std::memory_order_relaxed);
  }
  SCU_ALWAYS_INLINE auto is_panic() const noexcept {
    return _panic.load(std::memory_order_relaxed);
  }
  inline std::optional<std::string_view> panic_message() const {
    if (_panic.load(std::memory_order_acquire)) {
      return _panic_message;
    }
    return std::nullopt;
  }
  SCU_ALWAYS_INLINE auto remote_ip() const noexcept {
    return _remote_ip;
  }
  SCU_ALWAYS_INLINE auto remote_port() const noexcept {
    return _remote_port;
  }
  SCU_ALWAYS_INLINE auto state() const noexcept {
    return _state.load(std::memory_order_relaxed);
  }
  SCU_ALWAYS_INLINE auto is_alive() const noexcept {
    return _state.load(std::memory_order_relaxed) <= State::CONNECTED;
  }
};

class ScorpioUdp : public std::enable_shared_from_this<ScorpioUdp> {
  friend class ScorpioUdpConnection;
  std::unique_ptr<threading::Channel<std::shared_ptr<ScorpioUdpConnection>>> _new_connections;
  threading::Channel<UdpData, 1024 * 16> _sender_channel;
  threading::Channel<UdpData, 1024 * 16> _receiver_channel;
  threading::Channel<std::weak_ptr<ScorpioUdpConnection>> _awaiting_connections_channel;
  scorpio_utils::network::UdpSocket _socket;
  std::atomic<size_t> _mock_sequence_number;
  std::atomic<bool> _auto_accept;
  std::atomic<bool> _stop;
  std::unordered_map<std::pair<Ipv4, Port>, std::weak_ptr<ScorpioUdpConnection>> _user_connections;
  std::mutex _panic_mutex;
  threading::Signal _start_signal;
#if defined(SCORPIO_UTILS_SUDP_LOG_TO_FILE) && SCORPIO_UTILS_SUDP_LOG_TO_FILE == 1
  scorpio_utils::Logger<std::ofstream> _logger;

  void log_to_file(std::string&& message);
#endif

  std::string _panic_message;
  std::atomic<bool> _panic;
  void panic(std::string&& message);
  std::optional<std::pair<size_t, std::vector<std::vector<uint8_t>>>> generate_packets(
    std::optional<StreamNumber> stream_number,
    std::atomic<size_t>& sequence_number,
    Code code,
    const std::vector<uint8_t>& data);
  bool send(
    std::optional<StreamNumber> stream_number,
    std::atomic<size_t>& sequence_number,
    Code code,
    Ipv4 remote_ip,
    Port remote_port,
    const std::vector<uint8_t>& data);
  bool send(
    Ipv4 remote_ip,
    Port remote_port,
    std::vector<uint8_t>&& packet
  );
  void send_or_panic(
    std::optional<StreamNumber> stream_number,
    std::atomic<size_t>& sequence_number,
    Code code,
    Ipv4 remote_ip,
    Port remote_port,
    const std::vector<uint8_t>& data,
    std::string&& panic_message = "Failed to send UDP packet");

  ScorpioUdp();

  void sender_thread();
  void receiver_thread();

  std::unordered_map<std::pair<Ipv4, Port>, std::weak_ptr<ScorpioUdpConnection>> _connections;
  void processing_thread();
  void process_packet(UdpData udp_data);
  void handle_ping_packet(const MessageHeader& header, const UdpData& data);
  void handle_connect_packet(const MessageHeader& header, const UdpData& data);
  void pull_awaiting_connections(std::weak_ptr<ScorpioUdpConnection> connection_weak);
  std::shared_ptr<ScorpioUdpConnection> get_connection(Ipv4 remote_ip, Port remote_port);

  std::recursive_mutex _threads_mutex;
  std::vector<std::thread> _threads;

public:
  SCU_ALWAYS_INLINE bool SCU_EAGER_SELECT_IS_READY() noexcept {
    return _new_connections && _new_connections->SCU_EAGER_SELECT_IS_READY();
  }

  SCU_ALWAYS_INLINE decltype(auto) SCU_EAGER_SELECT_GET_VALUE() noexcept {
    return _new_connections->SCU_EAGER_SELECT_GET_VALUE();
  }

  [[nodiscard]] static std::shared_ptr<ScorpioUdp> create();
  ~ScorpioUdp();

  bool start();
  Expected<Success, std::string> listen(Ipv4, Port port);

private:
  // This shall be public but it can't kill every process correctly
  // - right now it is used in destructor which stops channels first
  // so, it works - but then channels cannot be reopened
  bool stop();

public:
  [[nodiscard]] std::shared_ptr<ScorpioUdpConnection> connect(Ipv4 ip, Port port);

  SCU_ALWAYS_INLINE auto is_running() const noexcept {
    return _stop.load(std::memory_order_relaxed) == false;
  }

  SCU_ALWAYS_INLINE auto set_auto_accept(bool auto_accept) noexcept {
    return _auto_accept.exchange(auto_accept, std::memory_order_relaxed);
  }

  template<bool Wait = false>
  SCU_ALWAYS_INLINE auto get_accepted_connection() {
    return _new_connections ? _new_connections->receive<Wait>() : std::nullopt;
  }

  SCU_ALWAYS_INLINE auto is_panic() const noexcept {
    return _panic.load(std::memory_order_relaxed);
  }

  inline std::optional<std::string_view> panic_message() const {
    if (_panic.load(std::memory_order_acquire)) {
      return _panic_message;
    }
    return std::nullopt;
  }

  SCU_ALWAYS_INLINE auto get_existing_connection(Ipv4 remote_ip, Port remote_port) {
    return _user_connections[{ remote_ip, remote_port }].lock();
  }
};
}  // namespace scorpio_utils::network
