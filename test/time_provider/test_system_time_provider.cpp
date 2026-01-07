#include <gtest/gtest.h>
#include <thread>
#include "scorpio_utils/time_provider/system_time_provider.hpp"

TEST(SystemTimeProvider, BasicUsage) {
  scorpio_utils::time_provider::SystemTimeProvider time_provider;

  auto time = time_provider.get_time();
  auto current_time = std::chrono::system_clock::now().time_since_epoch().count();

  EXPECT_LE(current_time - time, 10000);
  std::this_thread::sleep_for(std::chrono::milliseconds(10));

  time = time_provider.get_time();
  auto next_time = time_provider.get_time();
  EXPECT_GT(next_time, time);
  EXPECT_LT(next_time - time, 20000);
}
