#include <gtest/gtest.h>
#include <atomic>
#include <cstdint>
#include <chrono>
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
#define UQ std::make_unique

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

class EventInTime {
public:
  virtual Expected<Success, std::string> execute(
    int64_t time,
    UdpSocket& socket,
    std::shared_ptr<ScorpioUdp> connection
  ) = 0;
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
};

class SetAutoAccept final : public EventInTime {
  bool _auto_accept;

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
};

class StartListening final : public EventInTime {
  Ipv4 _ip;
  Port _port;

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
};

class SendPacket final : public EventInTime {
  Expected<UdpMessageInfo, std::string> _return_value;
  std::vector<uint8_t> _data;

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
};

class ExpectPacket final : public EventInTime {
  Ipv4 _remote_ip;
  Port _remote_port;
  std::vector<uint8_t> _data;

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
        return Unexpected("Expected data " + std::to_string(_data.size()) + " bytes but got " + std::to_string(
          data.size()) + " bytes");
      }
      return Success();
    }
    return Unexpected("No packet was sent"s);
  }
};

class ScorpioUdpTester : public ::testing::Test {
  std::shared_ptr<MockTimeProvider> _time_provider;
  std::unique_ptr<UdpSocket> _socket;
  std::shared_ptr<ScorpioUdp> _connection;

  SCU_ALWAYS_INLINE void stabilize_delay() {
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
  }

protected:
  void SetUp() override {
    _socket = std::make_unique<decltype(_socket)::element_type>();
    _time_provider = get_time_provider();
    _time_provider->set_time(0);
    _connection = ScorpioUdp::create(*_socket);
    stabilize_delay();
  }

  void TearDown() override {
    _time_provider->advance_time(TIMEOUT * 1000000000);
    stabilize_delay();
    _connection.reset();
    _time_provider->advance_time(TIMEOUT * 1000000000);
    stabilize_delay();
    _socket.reset();
    _time_provider.reset();
  }

  void execute_test(const std::vector<std::pair<int64_t, std::unique_ptr<EventInTime>>>& events) {
    // Check event list is not broken
    for (const auto& [time_offset, event] : events) {
      SCU_ASSERT(time_offset >= 0, "Time offset shall not be less than 0 but it is: " << time_offset);
      SCU_ASSERT(event.get() != nullptr, "Event shall not be a nullptr");
    }

    size_t i = 0;
    for (auto&& [time_offset, event] : events) {
      _time_provider->advance_time(time_offset);
      if (time_offset != 0) {
        stabilize_delay();
      }
      auto result = event->execute(_time_provider->get_time(), *_socket, _connection);
      if (SCU_UNLIKELY(result.is_err())) {
        EXPECT_TRUE(result) << "Test no.: " << i << " failed: " << result.err_value();
        return;
      }
      ++i;
    }
  }
};

TEST_F(ScorpioUdpTester, create_connection) {
  std::vector<std::pair<int64_t, std::unique_ptr<EventInTime>>> events;
    events.emplace_back(0, std::make_unique<StartScorpioUdp>());
    events.emplace_back(0, std::make_unique<StartListening>(Ipv4(127, 0, 0, 1), 10001));
    events.emplace_back(0, std::make_unique<SetAutoAccept>(true));
    events.emplace_back(0,
    std::make_unique<SendPacket>(Ipv4(127, 0, 0, 1), 12345,
    generate_single_packet(Code::CONNECT, { AS_BYTE(Code::ConnectionSubCommands::CONNECT) })));
    events.emplace_back(TICK_TIME,
    std::make_unique<ExpectPacket>(Ipv4(127, 0, 0, 1), 12345,
    generate_single_packet(Code::CONNECT, { AS_BYTE(Code::ConnectionSubCommands::ACCEPTED) })));
  execute_test(events);
}
