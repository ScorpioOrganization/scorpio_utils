#include <gtest/gtest.h>

#include <atomic>
#include <chrono>
#include <set>
#include <thread>
#include <tuple>
#include <utility>

#include "scorpio_utils/decorators.hpp"
#include "scorpio_utils/network/scorpio_udp.hpp"
#include "scorpio_utils/threading/jthread.hpp"

using scorpio_utils::network::Ipv4;
using scorpio_utils::network::localhost;
using scorpio_utils::network::Port;
using scorpio_utils::network::ScorpioUdp;
using scorpio_utils::network::ScorpioUdpConnection;
using scorpio_utils::network::ScorpioUdpStream;

#define PORT SCU_AS(Port, 12307u + SCU_COUNTER)
// #define PORT SCU_AS(Port, 12007u)

auto create_server(scorpio_utils::network::Port port) {
  auto socket = ScorpioUdp::create();
  if (!socket->start()) {
    return std::shared_ptr<ScorpioUdp>(nullptr);
  }
  std::ignore = socket->set_auto_accept(true);
  if (!socket->listen(localhost, port)) {
    return std::shared_ptr<ScorpioUdp>(nullptr);
  }
  return socket;
}

auto create_client() {
  auto socket = ScorpioUdp::create();
  if (!socket->start()) {
    return std::shared_ptr<ScorpioUdp>(nullptr);
  }
  return socket;
}

auto get_client_server_connection(scorpio_utils::network::Port port) {
  auto server = create_server(port);
  auto client = create_client();
  if (!server || !client) {
    return std::pair<std::shared_ptr<ScorpioUdpConnection>, std::shared_ptr<ScorpioUdpConnection>>(nullptr,
      nullptr);
  }
  auto client_connection = client->connect(localhost, port);
  std::this_thread::sleep_for(std::chrono::milliseconds(500));
  auto server_connection_opt = server->get_accepted_connection();
  if (!server_connection_opt.has_value() || !client_connection || !client_connection->is_alive() ||
    !server_connection_opt.value()->is_alive()) {
    return std::pair<std::shared_ptr<ScorpioUdpConnection>, std::shared_ptr<ScorpioUdpConnection>>(nullptr,
      nullptr);
  }
  std::this_thread::sleep_for(std::chrono::seconds(3));
  return std::pair(std::move(client_connection), std::move(server_connection_opt).value());
}

TEST(MockScorpioUdp, BasicSend) {
  const auto port = PORT;
  std::atomic<bool> server_ready(false);
  scorpio_utils::threading::JThread server([&server_ready, port]() {
      auto socket = ScorpioUdp::create();
      ASSERT_TRUE(socket->start());
      std::ignore = socket->set_auto_accept(true);
      ASSERT_TRUE(socket->listen(localhost, port));
      server_ready.store(true, std::memory_order_relaxed);
      std::this_thread::sleep_for(std::chrono::milliseconds(400));
      auto connection = socket->get_accepted_connection();
      ASSERT_TRUE(connection.has_value());
      connection.value()->set_auto_accept_stream(true);
      std::this_thread::sleep_for(std::chrono::seconds(3));
      EXPECT_EQ(connection.value()->state(), ScorpioUdpConnection::State::CONNECTED);
      std::this_thread::sleep_for(std::chrono::seconds(3));
      auto stream = connection.value()->get_accepted_stream();
      ASSERT_TRUE(stream.has_value());
      EXPECT_EQ(stream.value()->state(), ScorpioUdpStream::State::CREATED);
      std::this_thread::sleep_for(std::chrono::seconds(4));
      std::optional<std::vector<uint8_t>> data;
      EXPECT_NO_THROW(data = stream.value()->receive<false>());
      ASSERT_TRUE(data.has_value());
      EXPECT_EQ(data.value(), (std::vector<uint8_t>{ 'H', 'e', 'l', 'l', 'o', ' ', 'W', 'o', 'r', 'l', 'd', '!' }));
      std::this_thread::sleep_for(std::chrono::seconds(5));
    });
  const auto socket = ScorpioUdp::create();
  ASSERT_TRUE(socket->start());
  while (!server_ready.load(std::memory_order_relaxed)) {
    std::this_thread::yield();
  }
  std::this_thread::sleep_for(std::chrono::milliseconds(100));
  const auto connection = socket->connect(localhost, port);
  ASSERT_TRUE(connection);
  std::this_thread::sleep_for(std::chrono::seconds(1));
  EXPECT_EQ(connection->state(), ScorpioUdpConnection::State::CONNECTED);
  std::this_thread::sleep_for(std::chrono::seconds(1));
  const auto stream = connection->create_stream(1, { 0, ScorpioUdpStream::StreamQoS::Reliability::RELIABLE_ORDERED });
  ASSERT_TRUE(stream);
  std::this_thread::sleep_for(std::chrono::seconds(2));
  EXPECT_EQ(connection->state(), ScorpioUdpConnection::State::CONNECTED);
  EXPECT_TRUE(stream->is_active());
  EXPECT_TRUE(stream->is_alive());
  ASSERT_TRUE(stream->send(std::vector<uint8_t>{ 'H', 'e', 'l', 'l', 'o', ' ', 'W', 'o', 'r', 'l', 'd', '!' }));
  EXPECT_EQ(stream->state(), ScorpioUdpStream::State::CREATED);
  // Join the server while the client (socket/connection/stream) is still alive, so the server's
  // connection stays open when it calls get_accepted_stream() instead of throwing ClosedChannelException.
  server.join();
}

TEST(MockScorpioUdp, ClientServerCreation) {
  EXPECT_TRUE(create_server(PORT));
  EXPECT_TRUE(create_client());
}

TEST(MockScorpioUdp, SendDataOrder) {
  const auto [client_connection, server_connection] = get_client_server_connection(PORT);
  ASSERT_TRUE(client_connection);
  ASSERT_TRUE(server_connection);
  server_connection->set_auto_accept_stream(true);
  client_connection->set_auto_accept_stream(true);
  auto server_stream =
    server_connection->create_stream(1, { 1000, ScorpioUdpStream::StreamQoS::Reliability::RELIABLE_ORDERED });
  std::this_thread::sleep_for(std::chrono::seconds(3));
  ASSERT_TRUE(server_stream);
  ASSERT_TRUE(server_stream->is_active()) << SCU_AS(int, server_stream->state());
  auto client_stream_opt = client_connection->get_accepted_stream();
  ASSERT_TRUE(client_stream_opt.has_value());
  ASSERT_TRUE(client_stream_opt.value()->is_active());
  auto client_stream = std::move(client_stream_opt).value();
  std::vector<uint8_t> data;
  data.push_back(0);
  data.push_back(0);
  for (uint16_t i = 0; i < 1000; ++i) {
    data[0] = static_cast<uint8_t>(i & 0xff);
    data[1] = static_cast<uint8_t>((i >> 8) & 0xff);
    ASSERT_TRUE(client_stream->send(data));
  }
  for (uint16_t i = 0; i < 1000; ++i) {
    auto received_data = server_stream->receive<true>();
    ASSERT_EQ(received_data.size(), 2u);
    uint16_t received_index = static_cast<uint16_t>(received_data[0]) |
      (static_cast<uint16_t>(received_data[1]) << 8);
    EXPECT_EQ(received_index, i);
  }
}

// TEST(MockScorpioUdp, LargePacket) {
// const auto [client_connection, server_connection] = get_client_server_connection(PORT);
// ASSERT_TRUE(client_connection);
// ASSERT_TRUE(server_connection);
// server_connection->set_auto_accept_stream(true);
// client_connection->set_auto_accept_stream(true);
// auto server_stream =
// server_connection->create_stream(1, { 1000, ScorpioUdpStream::StreamQoS::Reliability::RELIABLE_ORDERED });
// std::this_thread::sleep_for(std::chrono::seconds(3));
// ASSERT_TRUE(server_stream);
// ASSERT_TRUE(server_stream->is_active()) << SCU_AS(int, server_stream->state());
// auto client_stream_opt = client_connection->get_accepted_stream();
// ASSERT_TRUE(client_stream_opt.has_value());
// ASSERT_TRUE(client_stream_opt.value()->is_active());
// auto client_stream = std::move(client_stream_opt).value();
// std::vector<uint8_t> data(5000, 42);
// for (size_t i = 0; i < 1000; ++i) {
// ASSERT_TRUE(client_stream->send(data));
// }
// std::this_thread::sleep_for(std::chrono::seconds(10));
// for (size_t i = 0; i < 1000; ++i) {
// auto received_data = server_stream->receive<false>();
// if (!received_data.has_value()) {
// FAIL() << "Failed to receive data at " << i;
// return;
// }
// ASSERT_EQ(received_data.value().size(), data.size());
// ASSERT_EQ(received_data.value(), data);
// }
// }

// TEST(MockScorpioUdp, LargePacketUnreliable) {
// const auto [client_connection, server_connection] = get_client_server_connection(PORT);
// ASSERT_TRUE(client_connection);
// ASSERT_TRUE(server_connection);
// server_connection->set_auto_accept_stream(true);
// client_connection->set_auto_accept_stream(true);
// auto server_stream =
// server_connection->create_stream(1, { 0, ScorpioUdpStream::StreamQoS::Reliability::UNRELIABLE });
// std::this_thread::sleep_for(std::chrono::seconds(3));
// ASSERT_TRUE(server_stream);
// ASSERT_TRUE(server_stream->is_active()) << SCU_AS(int, server_stream->state());
// auto client_stream_opt = client_connection->get_accepted_stream();
// ASSERT_TRUE(client_stream_opt.has_value());
// ASSERT_TRUE(client_stream_opt.value()->is_active());
// auto client_stream = std::move(client_stream_opt).value();
// std::vector<uint8_t> data(5000, 42);
// for (size_t i = 0; i < 1000; ++i) {
// ASSERT_TRUE(client_stream->send(data));
// }
// std::this_thread::sleep_for(std::chrono::seconds(10));
// std::set<std::vector<uint8_t>> received_packets;
// while (auto received_data = server_stream->receive<false>()) {
// ASSERT_TRUE(received_packets.insert(received_data.value()).second);
// }
// ASSERT_GE(received_packets.size(), 500ul);
// }

// Soak test for the self-healing pass: several reliable streams churn through
// create -> send batch -> receiver-side close -> recreate cycles, all under the
// mock link's 10% loss and 10-50 ms delay. The receiver closes a stream only
// after the full batch arrived, so a CLOSED client stream proves delivery; a
// panicked incarnation is simply retried with a fresh epoch (exercising the
// stale-incarnation CREATE handling). The only requirement is progress: every
// stream must complete all rounds before the deadline - no freeze, no zombie.
TEST(MockScorpioUdp, StreamChurnUnderLossSelfHeals) {
  const auto [client_connection, server_connection] = get_client_server_connection(PORT);
  ASSERT_TRUE(client_connection);
  ASSERT_TRUE(server_connection);
  server_connection->set_auto_accept_stream(true);

  constexpr uint16_t kStreams = 6;
  constexpr int kRounds = 3;
  constexpr int kMessages = 10;
  const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(90);

  std::atomic<bool> server_stop{ false };
  scorpio_utils::threading::JThread server_thread([&server_stop, &server_connection]() {
      std::vector<std::pair<std::shared_ptr<ScorpioUdpStream>, int>> active;
      try {
        while (!server_stop.load(std::memory_order_relaxed)) {
          while (auto accepted = server_connection->get_accepted_stream()) {
            active.emplace_back(std::move(*accepted), 0);
          }
          for (auto it = active.begin(); it != active.end(); ) {
            auto& [stream, received] = *it;
            while (auto data = stream->receive<false>()) {
              EXPECT_EQ(data->size(), 4u);
              ++received;
            }
            if (received >= kMessages) {
              // Full batch confirmed - close from the receiving side.
              std::ignore = stream->close();
              it = active.erase(it);
            } else if (!stream->is_alive()) {
              // Stale/failed incarnation - forget it, the client retries.
              it = active.erase(it);
            } else {
              ++it;
            }
          }
          std::this_thread::sleep_for(std::chrono::milliseconds(5));
        }
      } catch (const scorpio_utils::threading::ClosedChannelException&) {
      }
    });

  struct ClientStream {
    std::shared_ptr<ScorpioUdpStream> stream;
    int round = 0;
    bool batch_sent = false;
  };
  std::vector<ClientStream> streams(kStreams);
  size_t completed = 0;
  while (completed < kStreams && std::chrono::steady_clock::now() < deadline) {
    for (uint16_t s = 0; s < kStreams; ++s) {
      auto& cs = streams[s];
      if (cs.round >= kRounds) {
        continue;
      }
      if (!cs.stream) {
        cs.stream = client_connection->create_stream(
          static_cast<scorpio_utils::network::StreamNumber>(100 + s),
          { 64, ScorpioUdpStream::StreamQoS::Reliability::RELIABLE_ORDERED });
        cs.batch_sent = false;
        continue;
      }
      if (!cs.stream->is_alive()) {
        // CLOSED = server confirmed the whole batch; ERROR = failed incarnation,
        // retry the same round with a fresh stream (and a fresh epoch).
        const bool round_delivered = cs.stream->state() == ScorpioUdpStream::State::CLOSED;
        cs.stream.reset();
        if (round_delivered) {
          ++cs.round;
          if (cs.round == kRounds) {
            ++completed;
          }
        }
        continue;
      }
      if (cs.stream->is_active() && !cs.batch_sent) {
        bool all_sent = true;
        for (int m = 0; m < kMessages && all_sent; ++m) {
          all_sent = cs.stream->send({
            static_cast<uint8_t>(s & 0xff), static_cast<uint8_t>(s >> 8),
            static_cast<uint8_t>(cs.round), static_cast<uint8_t>(m) });
        }
        cs.batch_sent = all_sent;
      }
    }
    std::this_thread::sleep_for(std::chrono::milliseconds(5));
  }
  EXPECT_EQ(completed, kStreams) << "not all streams completed their rounds before the deadline";
  EXPECT_TRUE(client_connection->is_alive());
  EXPECT_TRUE(server_connection->is_alive());
  server_stop.store(true, std::memory_order_relaxed);
  server_thread.join();
}
