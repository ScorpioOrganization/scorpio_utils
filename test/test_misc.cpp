#include <gtest/gtest.h>

#include "scorpio_utils/misc.hpp"

TEST(least_significant_bytes_to_val, BasicTests) {
  using scorpio_utils::least_significant_bytes_to_val;

  uint32_t last_val = 0;
  for (uint32_t cur_val = 0; cur_val < 100000; ++cur_val) {
    last_val = least_significant_bytes_to_val<uint32_t, uint8_t>(last_val, SCU_AS(uint8_t, cur_val));
    // Assert instead of EXPECT to stop at first failure - later values depend on earlier ones
    ASSERT_EQ(last_val, cur_val) << "Failed at cur_val=" << cur_val;
  }
}

TEST(least_significant_bytes_to_val, BiggerStepsTest) {
  using scorpio_utils::least_significant_bytes_to_val;

  uint32_t last_val = 0;
  for (uint32_t cur_val = 0; cur_val < 2000000000; cur_val += 30000) {
    last_val = least_significant_bytes_to_val<uint32_t, uint16_t>(last_val, SCU_AS(uint16_t, cur_val));
    // Assert instead of EXPECT to stop at first failure - later values depend on earlier ones
    ASSERT_EQ(last_val, cur_val) << "Failed at cur_val=" << cur_val;
  }
}
