#include <gtest/gtest.h>
#include "scorpio_utils/time_utils/time_string.hpp"

TEST(TimeString, BasicUsage) {
  int64_t nanoseconds = 1753037204189000000;
  std::string formatted_time = scorpio_utils::time_utils::time_string(nanoseconds);
  EXPECT_EQ(formatted_time, "2025-07-20_20:46:44");
}
