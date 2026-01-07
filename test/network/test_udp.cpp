#include <gtest/gtest.h>
#include <algorithm>
#include <atomic>
#include <chrono>
#include <cmath>
#include <memory>
#include <thread>
#include <unordered_set>
#include <variant>
#include "scorpio_utils/network/udp.hpp"
#include "scorpio_utils/threading/jthread.hpp"

TEST(UdpSocketTest, CreateAndBind) {
  scorpio_utils::network::UdpSocket socket;
  ASSERT_TRUE(socket.open().is_ok()) << "Failed to create UDP socket";
  ASSERT_TRUE(socket.is_open()) << "Failed to create UDP socket";

  auto bind_result = socket.bind(scorpio_utils::network::Ipv4(0x7F000001), 14345);
  ASSERT_TRUE(bind_result.ok()) << "Failed to bind UDP socket: " << bind_result.err()->get();

  EXPECT_TRUE(socket.is_bound()) << "Socket should be bound";
}

TEST(UdpSocketTest, SendAndReceive) {
  std::atomic<bool> server_started(false);
  std::thread server_thread([&server_started]() {
      scorpio_utils::network::UdpSocket socket(scorpio_utils::network::Ipv4(0x7F000001), 14346);
      ASSERT_TRUE(socket.is_open()) << "Failed to create server socket";
      ASSERT_TRUE(socket.is_bound()) << "Server socket should be bound";

      uint8_t buffer[512];
      server_started.store(true, std::memory_order_relaxed);
      auto receive_result = socket.receive(buffer, sizeof(buffer));
      ASSERT_TRUE(receive_result.ok()) << "Failed to receive data: " << receive_result.err()->get();

      EXPECT_EQ(receive_result.ok_value().remote_ip, scorpio_utils::network::Ipv4(0x7F000001u));
      EXPECT_EQ(receive_result.ok_value().remote_port, 14347);
      EXPECT_EQ(receive_result.ok_value().byte_count, 11);
      EXPECT_EQ(std::string(reinterpret_cast<char*>(buffer), receive_result.ok_value().byte_count), "Hello World");
    });
  while (!server_started.load(std::memory_order_relaxed)) {
    std::this_thread::yield();
  }
  std::this_thread::sleep_for(std::chrono::milliseconds(100));
  scorpio_utils::network::UdpSocket socket(scorpio_utils::network::Ipv4(0x7F000001), 14347);
  ASSERT_TRUE(socket.is_open()) << "Failed to create client socket";
  ASSERT_TRUE(socket.is_bound()) << "Client socket should be bound";
  const char* message = "Hello World";
  auto send_result = socket.send(reinterpret_cast<uint8_t*>(const_cast<char*>(message)), 11,
    scorpio_utils::network::Ipv4(0x7F000001), 14346);
  ASSERT_TRUE(send_result.is_ok()) << "Failed to send data: " << send_result.err()->get();
  EXPECT_EQ(send_result.ok()->get(), 11) << "Sent data size should match";
  server_thread.join();
}

TEST(UdpSocketTest, DoubleBind) {
  scorpio_utils::network::UdpSocket socket;
  ASSERT_TRUE(socket.open().is_ok()) << "Failed to create UDP socket";
  ASSERT_TRUE(socket.is_open()) << "Failed to create UDP socket";

  auto bind_result = socket.bind(scorpio_utils::network::Ipv4(0x7F000001), 14348);
  ASSERT_TRUE(bind_result.is_ok()) << "Failed to bind UDP socket: " << bind_result.err()->get();

  EXPECT_TRUE(socket.is_bound()) << "Socket should be bound";

  auto second_bind_result = socket.bind(scorpio_utils::network::Ipv4(0x7F000001), 14348);
  EXPECT_FALSE(second_bind_result.is_ok()) << "Socket should not allow double binding";
}

TEST(UdpSocketTest, MoveAssignment) {
  scorpio_utils::network::UdpSocket socket1(false);
  ASSERT_FALSE(socket1.is_open()) << "Failed to create first UDP socket";
  {
    scorpio_utils::network::UdpSocket socket2(true);
    ASSERT_TRUE(socket2.is_open()) << "Failed to create second UDP socket";
    socket1 = std::move(socket2);
  }
  ASSERT_TRUE(socket1.is_open()) << "Socket should be valid after move assignment";
  EXPECT_FALSE(socket1.is_bound()) << "Socket should not be bound after move assignment";
  auto bind_result = socket1.bind(scorpio_utils::network::Ipv4(0x7F000001), 14349);
  EXPECT_TRUE(bind_result.ok()) << "Failed to bind UDP socket: " << bind_result.err()->get();
  EXPECT_TRUE(socket1.is_bound()) << "Socket should be bound after move assignment";
}

TEST(UdpSocketTest, SendAndReceiveOnClosedSocket) {
  scorpio_utils::network::UdpSocket socket;
  ASSERT_FALSE(socket.is_open()) << "Socket should not be open";

  uint8_t buffer[512];
  auto receive_result = socket.receive(buffer, sizeof(buffer));
  EXPECT_TRUE(receive_result.is_err()) << "Receive should fail on closed socket";

  const char* message = "Hello World";
  auto send_result = socket.send(reinterpret_cast<uint8_t*>(const_cast<char*>(message)), 11,
                                 scorpio_utils::network::localhost, 14350);
  EXPECT_TRUE(send_result.is_err()) << "Send should fail on closed socket";
}

TEST(UdpSocketTest, CloseWhileReceiving) {
  scorpio_utils::network::UdpSocket socket(scorpio_utils::network::localhost, 14350);
  ASSERT_TRUE(socket.open()) << "Failed to create UDP socket";

  std::atomic<bool> receive_started(false);
  std::atomic<bool> receive_finished(false);
  std::thread receive_thread([&]() {
      uint8_t buffer[512];
      receive_started.store(true, std::memory_order_relaxed);
      auto receive_result = socket.receive(buffer, sizeof(buffer));
      while (receive_result.is_ok()) {
        EXPECT_EQ(receive_result.ok_value().byte_count, 0);
        receive_result = socket.receive(buffer, sizeof(buffer));
      }
      EXPECT_TRUE(receive_result.is_err()) << "Receive should fail after socket is closed";
      receive_finished.store(true, std::memory_order_relaxed);
    });
  while (!receive_started.load(std::memory_order_relaxed)) {
    std::this_thread::yield();
  }
  std::this_thread::sleep_for(std::chrono::milliseconds(100));
  EXPECT_TRUE(socket.close()) << "Failed to close socket";
  receive_thread.join();
  EXPECT_TRUE(receive_finished.load(std::memory_order_relaxed)) <<
    "Receive thread should finish after socket is closed";
}

TEST(UdpSocketTest, MoveConstructor) {
  scorpio_utils::network::UdpSocket socket1(true);
  ASSERT_TRUE(socket1.is_open()) << "Failed to create first UDP socket";
  scorpio_utils::network::UdpSocket socket2(std::move(socket1));
  ASSERT_TRUE(socket2.is_open()) << "Socket should be valid after move construction";
  EXPECT_FALSE(socket2.is_bound()) << "Socket should not be bound after move construction";
  auto bind_result = socket2.bind(scorpio_utils::network::Ipv4(0x7F000001), 14351);
  EXPECT_TRUE(bind_result.ok()) << "Failed to bind UDP socket: " << bind_result.err()->get();
  EXPECT_TRUE(socket2.is_bound()) << "Socket should be bound after move construction";
  EXPECT_FALSE(socket1.is_open()) << "Original socket should be invalid after move construction";
}

TEST(UdpSocketTest, HighLoad) {
  scorpio_utils::network::UdpSocket socket1(scorpio_utils::network::localhost, 14352);
  scorpio_utils::network::UdpSocket socket2(scorpio_utils::network::localhost, 14353);
  ASSERT_TRUE(socket1.is_open()) << "Failed to create first UDP socket";
  ASSERT_TRUE(socket2.is_open()) << "Failed to create second UDP socket";

  constexpr size_t NUM_PACKETS = 10000;

  scorpio_utils::threading::JThread receiver_thread([&]() {
      std::unordered_set<uint32_t> received_values;
      uint8_t buffer[512];
      for (size_t i = 0; i < NUM_PACKETS; ++i) {
        auto receive_result = socket1.receive(buffer, sizeof(buffer));
        if (receive_result.err()) {
          break;
        }
        EXPECT_EQ(std::move(receive_result).ok_value().byte_count, 4);
        EXPECT_TRUE(received_values.insert(*reinterpret_cast<uint32_t*>(buffer)).second) <<
          "Duplicate packet received: " << *reinterpret_cast<uint32_t*>(buffer);
      }
#ifdef SCU_MOCK_UDP_PACKET_LOSS_PERCENTAGE
      EXPECT_GE(received_values.size(), SCU_AS(size_t, NUM_PACKETS * 0.7)) << "Too many packets were lost";
#else
      EXPECT_EQ(received_values.size(), NUM_PACKETS) << "Some packets were lost or duplicated";
#endif
    });

  for (size_t i = 0; i < NUM_PACKETS; ++i) {
    uint32_t value = static_cast<uint32_t>(i);
    auto send_result = socket2.send(reinterpret_cast<uint8_t*>(&value), sizeof(value),
                                    scorpio_utils::network::localhost, 14352);
    ASSERT_TRUE(send_result.is_ok()) << "Failed to send data: " << send_result.err()->get();
    EXPECT_EQ(send_result.ok()->get(), sizeof(value));
  }
  SCU_JTHREAD([&]() {
      std::this_thread::sleep_for(std::chrono::seconds(10));
      socket1.close();
      socket2.close();
  });
}
