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
#define TICK_TIME (HEARTBEAT_PERIOD / 2)
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
  SCU_ASSERT(result->second.size() <= MAX_PACKET_SIZE,
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
        return Unexpected("Expected remote IP "s + std::to_string(_remote_ip.ip()) + " but got " +
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
    return "ExpectPacket(" + std::to_string(_remote_ip.ip()) + ":" + std::to_string(_remote_port) + ", " +
           std::to_string(_data.size()) + " bytes)";
  }
  ~ExpectPacket() override = default;
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
      return "CreateConnectionEvent(" + std::to_string(_remote_ip.ip()) + ":" + std::to_string(_remote_port) + ")";
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

    explicit ConnectionIsAlive(std::shared_ptr<ConnectionHandle> handle, bool expect_alive)
    : _handle(std::move(handle)), _expect_alive{expect_alive} { }

public:
    Expected<Success, std::string> execute(
      int64_t,
      UdpSocket&,
      std::shared_ptr<ScorpioUdp>
    ) override {
      SCU_ASSERT(_handle->_connection.has_value(), "Handle does not contain a connection");
      if (SCU_UNLIKELY((*(_handle->_connection))->is_alive() != _expect_alive)) {
        return "Connection is not alive as expected"s;
      }
      return Success();
    }
    std::string name() override {
      return "ConnectionIsAlive(expect_alive=" + std::to_string(_expect_alive) + ")";
    }
    ~ConnectionIsAlive() override = default;
  };

  std::unique_ptr<ConnectionIsAlive> connection_is_alive(bool expect_alive = true) {
    return std::unique_ptr<ConnectionIsAlive>(new ConnectionIsAlive(shared_from_this(), expect_alive));
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
    _time_provider->advance_time(TIMEOUT * 1000000000);
    stabilize_delay();
    _socket->close_channels();
    _connection.reset();
    _time_provider->advance_time(TIMEOUT * 1000000000);
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

TEST_F(ScorpioUdpTester, connect_and_get_closed) {
  std::shared_ptr<ConnectionHandle> connection_handle = ConnectionHandle::create();
  std::vector<EventQueueItem> events;
  events.push_back({ WHERE, 0, std::make_unique<StartScorpioUdp>() });
  events.push_back({ WHERE, 0, connection_handle->create_connection(Ipv4(127, 0, 0, 1), 12345) });
  events.push_back({ WHERE, TICK_TIME, std::make_unique<ExpectPacket>(Ipv4(127, 0, 0, 1), 12345,
  generate_single_packet(Code::CONNECT, { AS_BYTE(Code::ConnectionSubCommands::CONNECT) })) });
  events.push_back({ WHERE, 0, std::make_unique<SendPacket>(Ipv4(127, 0, 0, 1), 12345,
  generate_single_packet(Code::CONNECT, { AS_BYTE(Code::ConnectionSubCommands::ACCEPTED) })) });
  events.push_back({ WHERE, 0, connection_handle->connection_is_alive(true) });
  events.push_back({ WHERE, 0, std::make_unique<SendPacket>(Ipv4(127, 0, 0, 1), 12345,
  generate_single_packet(Code::DISCONNECT, { AS_BYTE(Code::DisconnectSubCommands::DISCONNECT) })) });
  events.push_back({ WHERE, TICK_TIME,
      std::make_unique<ExpectPacket>(Ipv4(127, 0, 0, 1), 12345,
  generate_single_packet(Code::DISCONNECT, { AS_BYTE(Code::DisconnectSubCommands::ACCEPTED) })) });
  execute_test(events);
}

TEST_F(ScorpioUdpTester, accept_connection_and_close) {
  std::shared_ptr<ConnectionHandle> connection_handle = ConnectionHandle::create();
  std::vector<EventQueueItem> events;
  events.push_back({ WHERE, 0, std::make_unique<StartScorpioUdp>() });
  events.push_back({ WHERE, 0, std::make_unique<StartListening>(Ipv4(127, 0, 0, 1), 10001) });
  events.push_back({ WHERE, 0, std::make_unique<SetAutoAccept>(true) });
  events.push_back({ WHERE, TICK_TIME * 4, std::make_unique<SleepEvent>(TICK_TIME * 4) });
  events.push_back({ WHERE, 0, std::make_unique<SendPacket>(Ipv4(127, 0, 0, 1), 12345,
  generate_single_packet(Code::CONNECT, { AS_BYTE(Code::ConnectionSubCommands::CONNECT) })) });
  events.push_back({ WHERE, TICK_TIME * 4, std::make_unique<SleepEvent>(TICK_TIME * 4) });
  events.push_back({ WHERE, 0, std::make_unique<ExpectPacket>(Ipv4(127, 0, 0, 1), 12345,
  generate_single_packet(Code::CONNECT, { AS_BYTE(Code::ConnectionSubCommands::ACCEPTED) })) });
  events.push_back({ WHERE, 0, connection_handle->get_connection(true) });
  events.push_back({ WHERE, 0, connection_handle->close_connection(true) });
  events.push_back({ WHERE, 0, std::make_unique<ExpectPacket>(Ipv4(127, 0, 0, 1), 12345,
  generate_single_packet(Code::DISCONNECT, { AS_BYTE(Code::DisconnectSubCommands::DISCONNECT) })) });
  execute_test(events);
}

TEST_F(ScorpioUdpTester, reject_connection) {
  std::shared_ptr<ConnectionHandle> connection_handle = ConnectionHandle::create();
  std::vector<EventQueueItem> events;
  events.push_back({ WHERE, 0, std::make_unique<StartScorpioUdp>() });
  events.push_back({ WHERE, 0, std::make_unique<StartListening>(Ipv4(127, 0, 0, 1), 10001) });
  events.push_back({ WHERE, 0, std::make_unique<SetAutoAccept>(false) });
  events.push_back({ WHERE, TICK_TIME * 4, std::make_unique<SleepEvent>(TICK_TIME * 4) });
  events.push_back({ WHERE, 0, std::make_unique<SendPacket>(Ipv4(127, 0, 0, 1), 12345,
  generate_single_packet(Code::CONNECT, { AS_BYTE(Code::ConnectionSubCommands::CONNECT) })) });
  events.push_back({ WHERE, TICK_TIME * 4, std::make_unique<SleepEvent>(TICK_TIME * 4) });
  events.push_back({ WHERE, 0, std::make_unique<ExpectPacket>(Ipv4(127, 0, 0, 1), 12345,
  generate_single_packet(Code::CONNECT, { AS_BYTE(Code::ConnectionSubCommands::REJECTED) })) });
  execute_test(events);
}

TEST_F(ScorpioUdpTester, accept_connection_and_get_stream) {
  std::shared_ptr<ConnectionHandle> connection_handle = ConnectionHandle::create();
  std::shared_ptr<StreamHandle> stream_handle = StreamHandle::create(connection_handle);
  std::vector<EventQueueItem> events;
  events.push_back({ WHERE, 0, std::make_unique<StartScorpioUdp>() });
  events.push_back({ WHERE, 0, std::make_unique<StartListening>(Ipv4(127, 0, 0, 1), 10001) });
  events.push_back({ WHERE, 0, std::make_unique<SetAutoAccept>(true) });
  events.push_back({ WHERE, TICK_TIME * 4, std::make_unique<SleepEvent>(TICK_TIME * 4) });
  events.push_back({ WHERE, 0, std::make_unique<SendPacket>(Ipv4(127, 0, 0, 1), 12345,
  generate_single_packet(Code::CONNECT, { AS_BYTE(Code::ConnectionSubCommands::CONNECT) })) });
  events.push_back({ WHERE, TICK_TIME * 4, std::make_unique<SleepEvent>(TICK_TIME * 4) });
  events.push_back({ WHERE, 0, std::make_unique<ExpectPacket>(Ipv4(127, 0, 0, 1), 12345,
  generate_single_packet(Code::CONNECT, { AS_BYTE(Code::ConnectionSubCommands::ACCEPTED) })) });
  events.push_back({ WHERE, 0, connection_handle->get_connection(true) });
  events.push_back({ WHERE, 0, connection_handle->connection_auto_accept_streams(true) });
  events.push_back({ WHERE, 0, std::make_unique<SendPacket>(Ipv4(127, 0, 0, 1), 12345,
  generate_single_packet(Code::CREATE_STREAM,
      { AS_BYTE(Code::CreateStreamSubCommands::CREATE), 0x01, 0x00, 0x00, 0x00 })) });
  events.push_back({ WHERE, TICK_TIME * 4, std::make_unique<SleepEvent>(TICK_TIME * 4) });
  events.push_back({ WHERE, 0, stream_handle->get_stream(true) });
  execute_test(events);
}
