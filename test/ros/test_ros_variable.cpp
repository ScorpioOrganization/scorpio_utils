/*
 * scorpio_utils - Scorpio Utility Library for C++
 * Copyright (C) 2026 Igor Zaworski
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program. If not, see <https://www.gnu.org/licenses/>.
 */

#include <gtest/gtest.h>
#include "rclcpp/rclcpp.hpp"
#include "std_msgs/msg/bool.hpp"
#include "scorpio_utils/ros/ros_variable.hpp"
#include "scorpio_utils/testing/ros/subscribe_some.hpp"

#define SCU_UDP_TIMEOUT std::chrono::milliseconds(100)

class RosVariableTest : public ::testing::Test {
protected:
  rclcpp::Node::SharedPtr _node;

  void SetUp() override {
    rclcpp::init(0, nullptr);
    _node = std::make_shared<rclcpp::Node>("test_node");
  }

  void TearDown() override {
    rclcpp::shutdown();
  }
};

TEST_F(RosVariableTest, basicUsage) {
  // Create a RosVariable with initial value
  std_msgs::msg::Bool value;
  value.data = true;

  scorpio_utils::ros::RosVariable<std_msgs::msg::Bool> var(
    _node.get(), std::move(value), "/test_topic/set", "/test_topic/get");

  {
    auto result = scorpio_utils::testing::ros::subscribe_some<std_msgs::msg::Bool>(
        _node, "/test_topic/get", rclcpp::QoS(10).reliable().transient_local(), 2, SCU_UDP_TIMEOUT);

    EXPECT_EQ(result.size(), 1) << "Should receive one message initially";
    ASSERT_GT(result.size(), 0);
    EXPECT_EQ(result[0].data, true) << "Initial value should be true";
  }

  // Check initial value
  EXPECT_EQ(var->data, true) << "Initial value should be true";

  value.data = false;

  // Set a new value
  var.set(value);
  EXPECT_EQ(var->data, false);


  {
    auto result = scorpio_utils::testing::ros::subscribe_some<std_msgs::msg::Bool>(
        _node, "/test_topic/get", rclcpp::QoS(10).reliable().transient_local(), 2, SCU_UDP_TIMEOUT);

    EXPECT_EQ(result.size(), 1) << "Should receive one message initially";
    ASSERT_GT(result.size(), 0);
    EXPECT_EQ(result[0].data, false) << "Initial value should be true";
  }

  // Read the value
  const auto& v = *var;
  EXPECT_EQ(v.data, false);
}

TEST_F(RosVariableTest, edit) {
  std_msgs::msg::Bool value;
  value.data = true;

  scorpio_utils::ros::RosVariable<std_msgs::msg::Bool> var(
    _node.get(), std::move(value), "/test_topic/set", "/test_topic/get");

  {
    auto result = scorpio_utils::testing::ros::subscribe_some<std_msgs::msg::Bool>(
        _node, "/test_topic/get", rclcpp::QoS(10).reliable().transient_local(), 2, SCU_UDP_TIMEOUT);

    EXPECT_EQ(result.size(), 1) << "Should receive one message initially";
    ASSERT_GT(result.size(), 0);
    EXPECT_EQ(result[0].data, true) << "Initial value should be true";
  }

  {
    auto editor = var.edit();
    editor->data = false;
    {
      auto result = scorpio_utils::testing::ros::subscribe_some<std_msgs::msg::Bool>(
        _node, "/test_topic/get", rclcpp::QoS(10).reliable().transient_local(), 2, SCU_UDP_TIMEOUT);

      EXPECT_EQ(result.size(), 1) << "Should receive one message initially";
      ASSERT_GT(result.size(), 0);
      EXPECT_EQ(result[0].data, true) << "Initial value should be true";
    }
  }

  auto result = scorpio_utils::testing::ros::subscribe_some<std_msgs::msg::Bool>(
      _node, "/test_topic/get", rclcpp::QoS(10).reliable().transient_local(), 2, SCU_UDP_TIMEOUT);

  EXPECT_EQ(result.size(), 1) << "Should receive one message initially";
  ASSERT_GT(result.size(), 0);
  EXPECT_EQ(result[0].data, false) << "Initial value should be true";
}

TEST_F(RosVariableTest, timer) {
  std_msgs::msg::Bool value;
  value.data = true;

  scorpio_utils::ros::RosVariable<std_msgs::msg::Bool> var(
    _node.get(), std::move(value), "/test_topic/set", "/test_topic/get");

  var.set_timer(std::chrono::milliseconds(10));
  EXPECT_TRUE(var.is_timer_active()) << "Timer should be active";

  {
    auto result = scorpio_utils::testing::ros::subscribe_some<std_msgs::msg::Bool>(
        _node, "/test_topic/get", rclcpp::QoS(10).reliable().transient_local(), 4, std::chrono::milliseconds(40));

    EXPECT_GE(result.size(), 3) << "Should receive one message initially";
    for (const auto& msg : result) {
      EXPECT_EQ(msg.data, true) << "Timer should publish the value";
    }
  }

  value.data = false;
  var.set(value);

  {
    auto result = scorpio_utils::testing::ros::subscribe_some<std_msgs::msg::Bool>(
        _node, "/test_topic/get", rclcpp::QoS(10).reliable().transient_local(), 4, std::chrono::milliseconds(40));

    EXPECT_GE(result.size(), 3) << "Should receive one message initially";
    for (const auto& msg : result) {
      EXPECT_EQ(msg.data, false) << "Timer should publish the value";
    }
  }

  // Stop the timer
  var.reset_timer();

  {
    auto result = scorpio_utils::testing::ros::subscribe_some<std_msgs::msg::Bool>(
        _node, "/test_topic/get", rclcpp::QoS(10).reliable().transient_local(), 2, SCU_UDP_TIMEOUT);

    EXPECT_EQ(result.size(), 1) << "Should receive one message initially";
    ASSERT_GT(result.size(), 0);
    EXPECT_EQ(result[0].data, false) << "Last value should be false";
  }
}

TEST_F(RosVariableTest, write) {
  std_msgs::msg::Bool value;

  scorpio_utils::ros::RosVariable<std_msgs::msg::Bool> var(
    _node.get(), "/test_topic");

  {
    auto result = scorpio_utils::testing::ros::subscribe_some<std_msgs::msg::Bool>(
        _node, "/test_topic/get", rclcpp::QoS(10).reliable().transient_local(), 2, SCU_UDP_TIMEOUT);

    EXPECT_EQ(result.size(), 1) << "Should receive one message initially";
    ASSERT_GT(result.size(), 0);
    EXPECT_EQ(result[0].data, false) << "Initial value should be true";
  }

  // Check if writable
  EXPECT_TRUE(var.is_writable()) << "Variable should be writable";

  auto publisher = _node->create_publisher<std_msgs::msg::Bool>("/test_topic/set", rclcpp::QoS(10).reliable());
  value.data = true;
  publisher->publish(value);
  rclcpp::spin_some(_node);

  {
    auto result = scorpio_utils::testing::ros::subscribe_some<std_msgs::msg::Bool>(
        _node, "/test_topic/get", rclcpp::QoS(10).reliable().transient_local(), 2, SCU_UDP_TIMEOUT);

    EXPECT_EQ(result.size(), 1) << "Should receive one message initially";
    ASSERT_GT(result.size(), 0);
    EXPECT_EQ(result[0].data, true) << "Initial value should be true";
  }

  var.make_read_only();
  EXPECT_FALSE(var.is_writable()) << "Variable should not be writable anymore";
  value.data = false;
  publisher->publish(value);
  rclcpp::spin_some(_node);
  {
    auto result = scorpio_utils::testing::ros::subscribe_some<std_msgs::msg::Bool>(
        _node, "/test_topic/get", rclcpp::QoS(10).reliable().transient_local(), 2, SCU_UDP_TIMEOUT);

    EXPECT_EQ(result.size(), 1) << "Should receive one message initially";
    ASSERT_GT(result.size(), 0);
    EXPECT_EQ(result[0].data, true) << "Value should not change after making read-only";
  }

  // Reset to writable
  var.make_writable("/test");
  EXPECT_TRUE(var.is_writable()) << "Variable should be writable again";

  value.data = false;
  auto publisher2 = _node->create_publisher<std_msgs::msg::Bool>("/test", rclcpp::QoS(10).reliable());
  publisher2->publish(value);
  rclcpp::spin_some(_node);
  {
    auto result = scorpio_utils::testing::ros::subscribe_some<std_msgs::msg::Bool>(
        _node, "/test_topic/get", rclcpp::QoS(10).reliable().transient_local(), 2, SCU_UDP_TIMEOUT);

    EXPECT_EQ(result.size(), 1) << "Should receive one message initially";
    ASSERT_GT(result.size(), 0);
    EXPECT_EQ(result[0].data, false) << "Value should be true after reset to writable";
  }

  value.data = true;
  publisher->publish(value);
  rclcpp::spin_some(_node);

  {
    auto result = scorpio_utils::testing::ros::subscribe_some<std_msgs::msg::Bool>(
        _node, "/test_topic/get", rclcpp::QoS(10).reliable().transient_local(), 2, SCU_UDP_TIMEOUT);

    EXPECT_EQ(result.size(), 1) << "Should receive one message initially";
    ASSERT_GT(result.size(), 0);
    EXPECT_EQ(result[0].data, false) << "Value should be false after publish";
  }
  value.data = false;
  publisher2->publish(value);
  publisher2->publish(std::move(value));
}

TEST_F(RosVariableTest, callback) {
  std_msgs::msg::Bool value;
  value.data = true;

  scorpio_utils::ros::RosVariable<std_msgs::msg::Bool> var(
    _node.get(), std::move(value), "/test_topic/set", "/test_topic/get");

  bool callback_called = false;
  var.set_callback([&callback_called, &var](const std_msgs::msg::Bool& msg) {
      callback_called = true;
      EXPECT_EQ(msg, var.get()) << "Callback should receive the current value";
  });

  auto publisher = _node->create_publisher<std_msgs::msg::Bool>("/test_topic/set", rclcpp::QoS(10).reliable());

  {
    auto result = scorpio_utils::testing::ros::subscribe_some<std_msgs::msg::Bool>(
        _node, "/test_topic/get", rclcpp::QoS(10).reliable().transient_local(), 2, SCU_UDP_TIMEOUT);

    EXPECT_EQ(result.size(), 1) << "Should receive one message initially";
    ASSERT_GT(result.size(), 0);
    EXPECT_EQ(result[0].data, true) << "Initial value should be true";
  }

  EXPECT_FALSE(callback_called) << "Callback should be called after set";

  publisher->publish(value);
  rclcpp::spin_some(_node);
  EXPECT_FALSE(callback_called) << "Callback should not be called on publish";

  value.data = false;
  publisher->publish(value);
  rclcpp::spin_some(_node);
  EXPECT_TRUE(callback_called) << "Callback should be called on publish after set";

  callback_called = false;
  var.reset_callback();
  publisher->publish(value);
  rclcpp::spin_some(_node);
  EXPECT_FALSE(callback_called) << "Callback should not be called after reset";
}

TEST_F(RosVariableTest, sameValue) {
  std::vector<std_msgs::msg::Bool> values;
  values.reserve(2);

  auto subscriber = _node->create_subscription<std_msgs::msg::Bool>(
    "/test_topic/get", rclcpp::QoS(10).reliable().transient_local(),
    [&values](const std_msgs::msg::Bool::SharedPtr msg) {
      values.push_back(*msg);
    });

  scorpio_utils::ros::RosVariable<std_msgs::msg::Bool> var(
    _node.get(), "/test_topic/set", "/test_topic/get");

  rclcpp::spin_some(_node);

  ASSERT_EQ(values.size(), 1) << "Should receive one message initially";
  EXPECT_EQ(values[0].data, false) << "Initial value should be false";

  std_msgs::msg::Bool value;

  var.set(value);

  rclcpp::spin_some(_node);
  ASSERT_EQ(values.size(), 1) << "Should not receive a message on same value";
  EXPECT_EQ(values[0].data, false) << "Value should remain unchanged";

  value.data = true;
  var.set(value);

  rclcpp::spin_some(_node);
  ASSERT_EQ(values.size(), 2) << "Should receive a message on different value";
  EXPECT_EQ(values[1].data, true) << "Value should be updated to true";

  value.data = true;
  var.set(std::move(value));
  rclcpp::spin_some(_node);
  ASSERT_EQ(values.size(), 2) << "Should not receive a message on same value";
  EXPECT_EQ(values[1].data, true) << "Value should remain unchanged";
}
