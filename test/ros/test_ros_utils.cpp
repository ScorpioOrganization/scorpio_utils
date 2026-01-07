#include <gtest/gtest.h>
#include "std_msgs/msg/bool.hpp"
#include "scorpio_utils/ros/ros_utils.hpp"

TEST(IsMessageType, ValidMessageType) {
  EXPECT_FALSE(scorpio_utils::ros::IsMessageType<bool>::value);
  EXPECT_TRUE(scorpio_utils::ros::IsMessageType<std_msgs::msg::Bool>::value);
}
