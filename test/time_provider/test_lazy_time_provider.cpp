#include <gtest/gtest.h>
#include <thread>
#include "scorpio_utils/time_provider/lazy_time_provider.hpp"

TEST(LazyTimeProvider, BasicUsage) {
  const int64_t base_offset = 100'000'000;
  scorpio_utils::time_provider::LazyTimeProvider time_provider(base_offset);

  EXPECT_EQ(time_provider.get_time_offset(), base_offset);
  auto time = time_provider.get_time();
  auto current_time = std::chrono::system_clock::now().time_since_epoch().count();
  EXPECT_LE(current_time - time, time_provider.get_time_offset());
  std::this_thread::sleep_for(std::chrono::nanoseconds(time_provider.get_time_offset() * 2));
  auto next_time = time_provider.get_time();
  EXPECT_GT(next_time, time);
  EXPECT_LT(next_time - time, base_offset * 3);
  time_provider.set_time_offset(base_offset * 2);
  EXPECT_EQ(time_provider.get_time_offset(), base_offset * 2);
  std::this_thread::sleep_for(std::chrono::nanoseconds(time_provider.get_time_offset()));
  time = time_provider.get_time();
  std::this_thread::sleep_for(std::chrono::nanoseconds(time_provider.get_time_offset()));
  next_time = time_provider.get_time();
  EXPECT_GT(next_time, time);
  EXPECT_LT((next_time - time) % time_provider.get_time_offset(), base_offset);
  EXPECT_GT(next_time - time, base_offset);
}
