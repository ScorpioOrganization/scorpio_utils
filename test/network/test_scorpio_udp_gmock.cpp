#include <gtest/gtest.h>
#include <cstring>
#include <string>
#include <tuple>
#include "scorpio_utils/threading/signal.hpp"

#if !defined(SCORPIO_UTILS_UDP_GMOCK) || SCORPIO_UTILS_UDP_GMOCK != 1
# error "SCORPIO_UTILS_UDP_GMOCK must be defined to 1 to compile this test"
# ifdef SCORPIO_UTILS_UDP_GMOCK
#  undef SCORPIO_UTILS_UDP_GMOCK
# endif
# define SCORPIO_UTILS_UDP_GMOCK 1
#endif
#include "scorpio_utils/network/udp.hpp"
#include "scorpio_utils/network/scorpio_udp.hpp"

using std::literals::string_literals::operator""s;
using scorpio_utils::network::ScorpioUdp;
using scorpio_utils::network::Ipv4;
using scorpio_utils::network::Port;
using scorpio_utils::Expected;
using scorpio_utils::Unexpected;
using scorpio_utils::Success;
using scorpio_utils::network::UdpSocket;
using scorpio_utils::network::UdpMessageInfo;
using scorpio_utils::threading::Signal;
using scorpio_utils::network::Code;

using testing::Return;
using testing::Eq;
using testing::Gt;
using testing::Ge;
using testing::Ne;
using testing::Le;
using testing::AtMost;
using testing::AtLeast;
using testing::_;

std::shared_ptr<scorpio_utils::testing::MockTimeProvider> get_time_provider();

#define SOCKET_COMMON_SETUP \
  UdpSocket socket; \
  Ipv4 ip(4, 3, 2, 1); \
  Port port = 1234; \
  Signal stop; \
  auto time_provider = get_time_provider(); \
  EXPECT_CALL(socket, open()) \
  .Times(1) \
  .WillOnce(Return(Success::instance())); \
  EXPECT_CALL(socket, is_bound()) \
  .Times(1) \
  .WillOnce(Return(false)); \
  EXPECT_CALL(socket, bind(Eq(ip), Eq(port))) \
  .Times(1) \
  .WillOnce(Return(Success::instance())); \
  EXPECT_CALL(socket, close()) \
  .Times(1) \
  .WillOnce([&stop] { \
      stop.notify_one(); \
      return true; \
  }); \
  EXPECT_CALL(socket, is_open()) \
  .WillRepeatedly(Return(true)); \
  EXPECT_CALL(*time_provider, set_time_offset(Eq(HEARTBEAT_PERIOD / 2))) \
  .Times(AtLeast(1))

#define CREATE_SCORPIO_UDP \
  auto scorpio_udp = ScorpioUdp::create(socket); \
  ASSERT_TRUE(scorpio_udp->start()); \
  ASSERT_TRUE(scorpio_udp->listen(ip, port))

#define CLOSE_SCORPIO_UDP \
  scorpio_udp.reset(); \
  std::this_thread::sleep_for(std::chrono::milliseconds(150))

TEST(ScorpioUdpGmock, Listen) {
  SOCKET_COMMON_SETUP;

  EXPECT_CALL(socket, receive(Ne(nullptr), Gt(0)))
  .Times(AtMost(1))  // This test is not about receive, so allow zero calls
  .WillOnce([&stop](uint8_t*, size_t) -> Expected<UdpMessageInfo, std::string>
    {
      stop.wait();
      return "Failed to receive data"s;
    });

  auto scorpio_udp = ScorpioUdp::create(socket); \
  EXPECT_TRUE(scorpio_udp->start()); \
  auto result = scorpio_udp->listen(ip, port);
  EXPECT_TRUE(result.is_ok()) << "Failed to listen: " << result.err_value();

  time_provider->advance_time(TIMEOUT);

  CLOSE_SCORPIO_UDP;
}

// TEST(ScorpioUdpGmock, Connect) {
// std::atomic<size_t> seq_num{ 0 };
// const auto connect_packet_opt =
// generate_packets(
// std::nullopt,
// seq_num, Code::CONNECT,
// { SCU_AS(uint8_t, Code::ConnectionSubCommands::CONNECT) }
// );
// ASSERT_TRUE(connect_packet_opt.has_value());
// ASSERT_EQ(connect_packet_opt->second.size(), 1);
// ASSERT_EQ(connect_packet_opt->second[0].size(), 2);
// const auto& connect_packet = connect_packet_opt->second[0];

// const auto connect_accept_opt =
// generate_packets(
// std::nullopt,
// seq_num, Code::CONNECT,
// { SCU_AS(uint8_t, Code::ConnectionSubCommands::ACCEPTED) }
// );
// ASSERT_TRUE(connect_accept_opt.has_value());
// ASSERT_EQ(connect_accept_opt->second.size(), 1);
// ASSERT_EQ(connect_accept_opt->second[0].size(), 2);
// const auto& connect_accept_packet = connect_accept_opt->second[0];

// const auto disconnect_opt =
// generate_packets(
// std::nullopt,
// seq_num, Code::DISCONNECT,
// { SCU_AS(uint8_t, Code::DisconnectSubCommands::DISCONNECT) }
// );
// ASSERT_TRUE(disconnect_opt.has_value());
// ASSERT_EQ(disconnect_opt->second.size(), 1);
// ASSERT_EQ(disconnect_opt->second[0].size(), 2);
// const auto& disconnect_packet = disconnect_opt->second[0];

// const auto disconnect_accept_opt =
// generate_packets(
// std::nullopt,
// seq_num, Code::DISCONNECT,
// { SCU_AS(uint8_t, Code::DisconnectSubCommands::ACCEPTED) }
// );
// ASSERT_TRUE(disconnect_accept_opt.has_value());
// ASSERT_EQ(disconnect_accept_opt->second.size(), 1);
// ASSERT_EQ(disconnect_accept_opt->second[0].size(), 2);
// const auto& disconnect_accept_packet = disconnect_accept_opt->second[0];

// SOCKET_COMMON_SETUP;

// Signal connection_attempted;
// Signal disconnect_attempted;

// Ipv4 remote_ip(1, 2, 3, 4);
// Port remote_port = 4321;

// EXPECT_CALL(socket, send(Ne(nullptr), Le(2), Eq(remote_ip), Eq(remote_port)))
// .WillOnce([&connection_attempted, &connect_packet](uint8_t* packet, size_t s, Ipv4, Port) -> Expected<size_t,
// std::string> {
// EXPECT_EQ(s, 2);
// if (s != 2) {
// return 0;
// }
// connection_attempted.notify_one();
// EXPECT_EQ(connect_packet[0], packet[0]);
// EXPECT_EQ(connect_packet[1], packet[1]);
// connection_attempted.notify_one();
// return 2;
// })
// .WillRepeatedly([&disconnect_attempted, &disconnect_packet](uint8_t* packet, size_t s, Ipv4,
// Port) -> Expected<size_t, std::string> {
// if (s == 1) {
// return 1;
// }
// disconnect_attempted.notify_one();
// EXPECT_EQ(disconnect_packet[0], packet[0]);
// EXPECT_EQ(disconnect_packet[1], packet[1]);
// return 2;
// });

// EXPECT_CALL(socket, receive(Ne(nullptr), Ge(2)))
// .WillOnce([&connection_attempted, &connect_accept_packet, remote_ip, remote_port](uint8_t* dest,
// size_t) -> Expected<UdpMessageInfo,
// std::string> {
// connection_attempted.wait();
// std::memcpy(dest, connect_accept_packet.data(), connect_accept_packet.size());
// return UdpMessageInfo{ 2, remote_ip, remote_port };
// })
// .WillRepeatedly([&disconnect_attempted, &disconnect_accept_packet, remote_ip, remote_port](uint8_t* dest,
// size_t) -> Expected<UdpMessageInfo, std::string> {
// disconnect_attempted.wait();
// std::memcpy(dest, disconnect_accept_packet.data(), disconnect_accept_packet.size());
// return UdpMessageInfo{ 2, remote_ip, remote_port };
// });

// CREATE_SCORPIO_UDP;

// auto connection = scorpio_udp->connect(remote_ip, remote_port);
// std::this_thread::sleep_for(std::chrono::milliseconds(1000));
// connection.reset();
// std::this_thread::sleep_for(std::chrono::milliseconds(100));

// CLOSE_SCORPIO_UDP;
// }
