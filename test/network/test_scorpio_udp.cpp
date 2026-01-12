#include <gtest/gtest.h>

#include <atomic>
#include <chrono>
#include <thread>
#include <tuple>
#include <utility>

#include "scorpio_utils/decorators.hpp"
#include "scorpio_utils/network/scorpio_udp.hpp"

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
  return std::pair(std::move(client_connection), std::move(server_connection_opt).value());
}

TEST(ScorpioUdp, BasicSend) {
  const auto port = PORT;
  std::atomic<bool> server_ready(false);
  std::thread server([&server_ready, port]() {
      auto socket = ScorpioUdp::create();
      ASSERT_TRUE(socket->start());
      std::ignore = socket->set_auto_accept(true);
      ASSERT_TRUE(socket->listen(localhost, port));
      server_ready.store(true, std::memory_order_relaxed);
      std::this_thread::sleep_for(std::chrono::milliseconds(400));
      auto connection = socket->get_accepted_connection();
      EXPECT_TRUE(connection.has_value());
      connection.value()->set_auto_accept_stream(true);
      EXPECT_EQ(connection.value()->state(), ScorpioUdpConnection::State::CONNECTED);
      std::this_thread::sleep_for(std::chrono::seconds(3));
      auto stream = connection.value()->get_accepted_stream();
      ASSERT_TRUE(stream.has_value());
      EXPECT_EQ(stream.value()->state(), ScorpioUdpStream::State::CREATED);
      std::this_thread::sleep_for(std::chrono::seconds(4));
      auto data = stream.value()->receive<false>();
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
  std::this_thread::sleep_for(std::chrono::milliseconds(200));
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
  if (server.joinable()) {
    server.join();
  }
}

TEST(ScorpioUdp, ClientServerCreation) {
  EXPECT_TRUE(create_server(PORT));
  EXPECT_TRUE(create_client());
}

TEST(ScorpioUdp, SendDataOrder) {
  const auto [client_connection, server_connection] = get_client_server_connection(PORT);
  ASSERT_TRUE(client_connection);
  ASSERT_TRUE(server_connection);
  server_connection->set_auto_accept_stream(true);
  client_connection->set_auto_accept_stream(true);
  constexpr size_t NUM_PACKETS = 1000ul;
  auto server_stream =
    server_connection->create_stream(1, { NUM_PACKETS, ScorpioUdpStream::StreamQoS::Reliability::RELIABLE_ORDERED });
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
  for (uint16_t i = 0; i < NUM_PACKETS; ++i) {
    data[0] = static_cast<uint8_t>(i & 0xff);
    data[1] = static_cast<uint8_t>((i >> 8) & 0xff);
    ASSERT_TRUE(client_stream->send(data));
  }
  for (uint16_t i = 0; i < NUM_PACKETS; ++i) {
    auto received_data = server_stream->receive<true>();
    ASSERT_EQ(received_data.size(), 2u);
    uint16_t received_index = static_cast<uint16_t>(received_data[0]) |
      (static_cast<uint16_t>(received_data[1]) << 8);
    EXPECT_EQ(received_index, i);
  }
}

TEST(ScorpioUdp, LargePacket) {
  const auto [client_connection, server_connection] = get_client_server_connection(PORT);
  ASSERT_TRUE(client_connection);
  ASSERT_TRUE(server_connection);
  server_connection->set_auto_accept_stream(true);
  client_connection->set_auto_accept_stream(true);
  constexpr size_t NUM_PACKETS = 1000ul;
  constexpr size_t PACKET_SIZE = 5000ul;
  auto server_stream =
    server_connection->create_stream(1, { NUM_PACKETS* PACKET_SIZE / 500,
        ScorpioUdpStream::StreamQoS::Reliability::RELIABLE_ORDERED });
  std::this_thread::sleep_for(std::chrono::seconds(3));
  ASSERT_TRUE(server_stream);
  ASSERT_TRUE(server_stream->is_active()) << SCU_AS(int, server_stream->state());
  auto client_stream_opt = client_connection->get_accepted_stream();
  ASSERT_TRUE(client_stream_opt.has_value());
  ASSERT_TRUE(client_stream_opt.value()->is_active());
  auto client_stream = std::move(client_stream_opt).value();
  std::vector<uint8_t> data(PACKET_SIZE, 42);
  for (size_t i = 0; i < NUM_PACKETS; ++i) {
    ASSERT_TRUE(client_stream->send(data)) << "Failed to send packet: " << i <<
      "\nClient stream panic message:\n" <<
      client_stream->panic_message().value_or("<no_panic>") <<
      "\nClient connection panic message:\n" <<
      client_connection->panic_message().value_or("<no_panic>") <<
      "\nServer stream panic message:\n" <<
      server_stream->panic_message().value_or("<no_panic>") <<
      "\nServer connection panic message:\n" <<
      server_connection->panic_message().value_or("<no_panic>");
  }
  std::this_thread::sleep_for(std::chrono::seconds(10));
  for (size_t i = 0; i < NUM_PACKETS; ++i) {
    auto received_data = server_stream->receive<false>();
    if (!received_data.has_value()) {
      FAIL() << "Failed to receive data at " << i;
      return;
    }
    ASSERT_EQ(received_data.value().size(), data.size());
    ASSERT_EQ(received_data.value(), data);
  }
}

TEST(ScorpioUdp, LargePacketUnreliable) {
const auto [client_connection, server_connection] = get_client_server_connection(PORT);
ASSERT_TRUE(client_connection);
ASSERT_TRUE(server_connection);
server_connection->set_auto_accept_stream(true);
client_connection->set_auto_accept_stream(true);
constexpr size_t NUM_PACKETS = 1000ul;
  constexpr size_t PACKET_SIZE = 5000ul;
auto server_stream =
    server_connection->create_stream(1, { NUM_PACKETS* PACKET_SIZE / 500,
        ScorpioUdpStream::StreamQoS::Reliability::UNRELIABLE });
std::this_thread::sleep_for(std::chrono::seconds(3));
ASSERT_TRUE(server_stream);
ASSERT_TRUE(server_stream->is_active()) << SCU_AS(int, server_stream->state());
auto client_stream_opt = client_connection->get_accepted_stream();
ASSERT_TRUE(client_stream_opt.has_value());
ASSERT_TRUE(client_stream_opt.value()->is_active());
auto client_stream = std::move(client_stream_opt).value();
std::vector<uint8_t> data(PACKET_SIZE, 42);
for (size_t i = 0; i < NUM_PACKETS; ++i) {
    ASSERT_TRUE(client_stream->send(data)) << "Failed to send packet: " << i << "\nPanic message:\n" <<
      client_stream->panic_message().value_or("<no_panic>");
}
std::this_thread::sleep_for(std::chrono::seconds(10));
for (size_t i = 0; i < NUM_PACKETS; ++i) {
    auto received_data = server_stream->receive<false>();
    if (!received_data.has_value()) {
      FAIL() << "Failed to receive data at " << i;
      return;
    }
    ASSERT_EQ(received_data.value().size(), data.size());
    ASSERT_EQ(received_data.value(), data);
}
}

TEST(ScorpioUdp, LongConnection) {
  const auto [client_connection, server_connection] = get_client_server_connection(PORT);
  ASSERT_TRUE(client_connection);
  ASSERT_TRUE(server_connection);
  server_connection->set_auto_accept_stream(true);
  client_connection->set_auto_accept_stream(true);
  constexpr size_t NUM_PACKETS = 4ul;
  auto server_stream =
    server_connection->create_stream(1, { 1, ScorpioUdpStream::StreamQoS::Reliability::RELIABLE_ORDERED });
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
  for (uint16_t i = 0; i < NUM_PACKETS; ++i) {
    std::this_thread::sleep_for(std::chrono::seconds(5));
    data[0] = static_cast<uint8_t>(i & 0xff);
    data[1] = static_cast<uint8_t>((i >> 8) & 0xff);
    ASSERT_TRUE(client_stream->send(data)) << "Failed to send packet: " << i <<
      "\nClient stream panic message:\n" <<
      client_stream->panic_message().value_or("<no_panic>") <<
      "\nClient connection panic message:\n" <<
      client_connection->panic_message().value_or("<no_panic>") <<
      "\nServer stream panic message:\n" <<
      server_stream->panic_message().value_or("<no_panic>") <<
      "\nServer connection panic message:\n" <<
      server_connection->panic_message().value_or("<no_panic>");
  }
  std::this_thread::sleep_for(std::chrono::seconds(2));
  for (uint16_t i = 0; i < NUM_PACKETS; ++i) {
    auto received_data = server_stream->receive<true>();
    ASSERT_EQ(received_data.size(), 2u);
    uint16_t received_index = static_cast<uint16_t>(received_data[0]) |
      (static_cast<uint16_t>(received_data[1]) << 8);
    EXPECT_EQ(received_index, i);
  }
}
