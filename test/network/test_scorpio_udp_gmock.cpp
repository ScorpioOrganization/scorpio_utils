#include <gtest/gtest.h>
#include <string>
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
using scorpio_utils::Success;
using scorpio_utils::network::UdpSocket;
using scorpio_utils::network::UdpMessageInfo;
using scorpio_utils::threading::Signal;

using testing::Return;
using testing::Eq;
using testing::Gt;
using testing::Ne;
using testing::AtMost;

TEST(ScorpioUdpGmock, ScorpioUdpListen) {
  UdpSocket socket;
  Signal stop;
  Ipv4 ip(4, 3, 2, 1);
  Port port = 1234;

  EXPECT_CALL(socket, receive(Ne(nullptr), Gt(0)))
  .Times(AtMost(1))  // This test is not about receive, so allow zero calls
  .WillOnce([&stop](uint8_t*, size_t) -> Expected<UdpMessageInfo, std::string>
    {
      stop.wait();
      return "Failed to receive data"s;
    });
  EXPECT_CALL(socket, open())
  .Times(1)
  .WillOnce(Return(Success::instance()));
  EXPECT_CALL(socket, is_bound())
  .Times(1)
  .WillOnce(Return(false));
  EXPECT_CALL(socket, bind(Eq(ip), Eq(port)))
  .Times(1)
  .WillOnce(Return(Success::instance()));
  EXPECT_CALL(socket, close())
  .Times(1)
  .WillOnce([&stop] {
      stop.notify_one();
      return true;
  });

  auto scorpio_udp = ScorpioUdp::create(socket);


  ASSERT_TRUE(scorpio_udp->start());

  auto result = scorpio_udp->listen(ip, port);
  ASSERT_TRUE(result.is_ok()) << "Listen failed: " << result.err_value();

  scorpio_udp.reset();
  std::this_thread::sleep_for(std::chrono::milliseconds(150));
}
