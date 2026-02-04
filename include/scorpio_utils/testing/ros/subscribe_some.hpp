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

#pragma once

#include <chrono>
#include <string>
#include <thread>
#include <vector>
#include "rclcpp/rclcpp.hpp"
#include "scorpio_utils/optional_utils.hpp"
#include "scorpio_utils/ros/ros_utils.hpp"
#include "scorpio_utils/assert.hpp"

namespace scorpio_utils::testing::ros {
/**
 * Subscribes to a ROS topic and collects a specified number of messages.
 *
 * \tparam T The type of the message to subscribe to.
 * \tparam Sleep The sleep duration between spins, in microseconds - default is `500`.
 * \param node The ROS node to use for subscribing.
 * \param topic_name The name of the topic to subscribe to.
 * \param qos The Quality of Service settings for the subscription - default is `rclcpp::QoS(10)`.
 * \param amount The number of messages to collect - default is `1`.
 * \param timeout The maximum time to wait for messages - default is `5` seconds.
 * \return A vector containing the collected messages.
 * \throw std::runtime_error if ROS is not ok
 */
template<typename T>
[[nodiscard]] std::vector<T> subscribe_some(
  rclcpp::Node::SharedPtr node,
  const std::string& topic_name,
  const rclcpp::QoS& qos = rclcpp::QoS(10),
  size_t amount = 1,
  std::chrono::milliseconds timeout = std::chrono::seconds(5), const size_t sleep = 500) {
  static_assert(scorpio_utils::ros::IsMessageType<T>::value, "Type T must be a ROS message type");

  SCU_ASSERT(rclcpp::ok(), "ROS is not ok, cannot subscribe to topic");
  SCU_ASSERT(node, "Node must not be null");
  SCU_ASSERT(!topic_name.empty(), "Topic name must not be empty");

  std::vector<T> messages;
  auto subscriber = node->create_subscription<T>(
    topic_name, qos, [&messages](const typename T::SharedPtr msg) {
      messages.push_back(*msg);
    });

  // Spin the node to receive messages
  const auto finish_time = timeout != decltype(timeout)::zero() ? std::make_optional(
      std::chrono::steady_clock::now() + timeout) : std::nullopt;
  while ((amount == 0 || messages.size() < amount) &&
    (scorpio_utils::optional_map([](const std::chrono::steady_clock::time_point& time) -> bool {
      return std::chrono::steady_clock::now() < time;
    }, finish_time).value_or(true))) {
    rclcpp::spin_some(node);
    std::this_thread::sleep_for(std::chrono::microseconds(sleep));
  }

  return messages;
}

}  // namespace scorpio_utils::testing::ros
