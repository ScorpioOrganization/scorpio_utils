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

#include <atomic>
#include <string>
#include <utility>
#include "rclcpp/rclcpp.hpp"
#include "scorpio_utils/decorators.hpp"
#include "scorpio_utils/ros/ros_utils.hpp"
#include "scorpio_utils/unique_function.hpp"

namespace scorpio_utils::ros {
/**
 * A class that represents a ROS variable, which can be subscribed to and published.
 * It allows for reading and writing of a ROS message type, with optional callback
 * functionality when the value changes.
 *
 * \tparam T The type of the ROS message.
 * \tparam IgnoreSameValue If true, setting the same value will not trigger a publish or callback.
 */
template<typename T, bool IgnoreSameValue = true>
class RosVariable {
  static_assert(IsMessageType<T>::value, "Type T must be a ROS message type");
  rclcpp::Node* _node;
  typename rclcpp::Subscription<T>::SharedPtr _subscriber;
  typename rclcpp::Publisher<T>::SharedPtr _publisher;
  rclcpp::TimerBase::SharedPtr _timer;
  scorpio_utils::UniqueFunction<void(const T&)> _callback;
  std::atomic<size_t> _edit_count;
  T _value;

  void subscriber_callback(const typename T::SharedPtr msg) {
    if constexpr (IgnoreSameValue) {
      if (_value == *msg) {
        return;
      }
    }
    _value = *msg;
    if (_callback) {
      _callback(_value);
    }
    _publisher->publish(_value);
  }

  void timer_callback() const {
    _publisher->publish(_value);
  }

  SCU_ALWAYS_INLINE void init(bool publish_on_create) {
    if (publish_on_create) {
      _publisher->publish(_value);
    }
  }

public:
  /**
   * Constructor for RosVariable.
   * Initializes the variable with an initial value and sets up the subscriber and publisher.
   * If `publish_on_create` is true, it publishes the initial value immediately.
   *
   * \param node Pointer to the ROS node.
   * \param initial_value The initial value of the variable.
   * \param subscriber_topic_name The topic name for the subscriber - will not be created if empty.
   * \param publisher_topic_name The topic name for the publisher.
   * \param subscriber_qos QoS settings for the subscriber.
   * \param publish_on_create If true, publishes the initial value immediately.
   */
  RosVariable(
    rclcpp::Node* node, T&& initial_value, const std::string& subscriber_topic_name,
    const std::string& publisher_topic_name, const rclcpp::QoS& subscriber_qos = rclcpp::QoS(10),
    bool publish_on_create = true)
  : _node(node),
    _subscriber(subscriber_topic_name.empty() ? nullptr : _node->create_subscription<T>(
      subscriber_topic_name, subscriber_qos,
      std::bind(&RosVariable::subscriber_callback, this, std::placeholders::_1))),
    _publisher(_node->create_publisher<T>(publisher_topic_name, rclcpp::QoS(1).reliable().transient_local())),
    _edit_count(0),
    _value(std::forward<T>(initial_value)) {
    init(publish_on_create);
  }

  /**
   * Constructor for RosVariable.
   * Initializes the variable with an initial value and sets up the subscriber and publisher.
   * If `publish_on_create` is true, it publishes the initial value immediately.
   *
   * \param node Pointer to the ROS node.
   * \param subscriber_topic_name The topic name for the subscriber - will not be created if empty.
   * \param publisher_topic_name The topic name for the publisher.
   * \param subscriber_qos QoS settings for the subscriber.
   * \param publish_on_create If true, publishes the initial value immediately.
   */
  RosVariable(
    rclcpp::Node* node, const std::string& subscriber_topic_name,
    const std::string& publisher_topic_name, const rclcpp::QoS& subscriber_qos = rclcpp::QoS(10),
    bool publish_on_create = true)
  : RosVariable(node, T(), subscriber_topic_name,
      publisher_topic_name, subscriber_qos, publish_on_create) { }

  /**
   * Constructor for RosVariable.
   * Initializes the variable with an initial value and sets up the subscriber and publisher.
   * If `publish_on_create` is true, it publishes the initial value immediately.
   * \param node Pointer to the ROS node.
   * \param initial_value The initial value of the variable.
   * \param topic_name The base topic name for both subscriber and publisher - will append "/set" and "/get".
   * \param subscriber_qos QoS settings for the subscriber.
   * \param publish_on_create If true, publishes the initial value immediately.
   */
  RosVariable(
    rclcpp::Node* node, T&& initial_value, const std::string& topic_name,
    const rclcpp::QoS& subscriber_qos = rclcpp::QoS(10), bool publish_on_create = true)
  : RosVariable(node, std::forward<T>(initial_value), topic_name + "/set",
      topic_name + "/get", subscriber_qos, publish_on_create) { }

  /**
   * Constructor for RosVariable.
   * Initializes the variable with an initial value and sets up the subscriber and publisher.
   * If `publish_on_create` is true, it publishes the initial value immediately.
   * \param node Pointer to the ROS node.
   * \param topic_name The base topic name for both subscriber and publisher - will append "/set" and "/get".
   * \param subscriber_qos QoS settings for the subscriber.
   * \param publish_on_create If true, publishes the initial value immediately.
   * */
  RosVariable(
    rclcpp::Node* node, const std::string& topic_name,
    const rclcpp::QoS& subscriber_qos = rclcpp::QoS(10), bool publish_on_create = true)
  : RosVariable(node, T(), topic_name + "/set", topic_name + "/get", subscriber_qos, publish_on_create) { }

  ~RosVariable() {
    SCU_ASSERT(_edit_count == 0, "RosVariable is being destroyed while still being edited");
  }

  /**
   * \return The current value of the variable.
   */
  SCU_ALWAYS_INLINE const T& get() const noexcept {
    return _value;
  }

  /**
   * Moves the current value of the variable.
   * This allows you to take ownership of the value, leaving the variable in a valid but unspecified state.
   * Use this method with caution, as it will leave the variable without a valid value until set again.
   *
   * \return The current value of the variable, moved out of the variable.
   */
  SCU_ALWAYS_INLINE T&& get_move() {
    return std::move(_value);
  }

  /**
   * \return True if the variable is writable, false otherwise.
   */
  SCU_ALWAYS_INLINE bool is_writable() const {
    return _subscriber != nullptr;
  }

  /**
   * Sets the value of the variable and publishes it.
   * If `IgnoreSameValue` is true, it will not publish if the value is the same as the current one.
   *
   * \param value The new value to set.
   */
  void set(const T& value) {
    if constexpr (IgnoreSameValue) {
      if (_value == value) {
        return;
      }
    }
    _value = value;
    _publisher->publish(_value);
  }

  /**
   * Sets the value of the variable and publishes it.
   * If `IgnoreSameValue` is true, it will not publish if the value is the same as the current one.
   * \param value The new value to set.
   */
  void set(T&& value) {
    if constexpr (IgnoreSameValue) {
      if (_value == value) {
        return;
      }
    }
    _value = std::forward<T>(value);
    _publisher->publish(_value);
  }

  /**
   * \return A pointer to the ROS node associated with this variable.
   */
  SCU_ALWAYS_INLINE rclcpp::Node * get_node() noexcept {
    return _node;
  }

  /**
   * Resets the subscriber, making the variable read-only.
   * This means it will no longer receive updates from the topic.
   * After calling this, you can still read the value, but you cannot set it anymore via topic updates.
   * If you want to make it writable again, use `make_writable()`.
   * \note This does not affect the publisher, which can still be used to publish the current value.
   */
  SCU_ALWAYS_INLINE void make_read_only() {
    _subscriber.reset();
  }

  /**
   * Makes the variable writable again by creating a new subscriber.
   * This allows the variable to receive updates from the specified topic.
   * \param subscriber_topic_name The topic name for the subscriber.
   * \param subscriber_qos QoS settings for the subscriber.
   */
  void make_writable(const std::string& subscriber_topic_name, rclcpp::QoS subscriber_qos = rclcpp::QoS(10)) {
    _subscriber = _node->create_subscription<T>(
      subscriber_topic_name, subscriber_qos,
      std::bind(&RosVariable::subscriber_callback, this, std::placeholders::_1));
  }

  SCU_ALWAYS_INLINE const T& operator*() const noexcept {
    return get();
  }

  SCU_ALWAYS_INLINE const T* operator->() const noexcept {
    return &get();
  }

  /**
   * Resets the timer if it is active.
   * This is useful if you want to stop the periodic publishing of the variable.
   */
  SCU_ALWAYS_INLINE void reset_timer() {
    if (_timer) {
      _timer.reset();
    }
  }

  /**
   * Sets a timer to periodically publish the current value.
   * This is useful if you want to automatically publish the value at regular intervals.
   *
   * \param period The period for the timer in milliseconds.
   */
  template<typename Rep, typename Period>
  SCU_ALWAYS_INLINE void set_timer(std::chrono::duration<Rep, Period> period) {
    _timer = _node->create_wall_timer(period, std::bind(&RosVariable::timer_callback, this));
  }

  /**
   * \return True if the timer is active, false otherwise.
   * This can be used to check if the variable is currently being published periodically.
   */
  SCU_ALWAYS_INLINE bool is_timer_active() const {
    return _timer != nullptr;
  }

  /**
   * Sets a callback that will be called whenever the variable is updated via ros topic.
   * The callback will receive the current value of the variable.
   *
   * \param callback The callback function to set.
   * This should be a callable that takes a const reference to T.
   * \note If the callback is already set, it will be replaced with the new one
   * and the previous callback will be discarded.
   * If `IgnoreSameValue` is true, the callback will not be called when the value is the same as the current one.
   */
  SCU_ALWAYS_INLINE void set_callback(scorpio_utils::UniqueFunction<void(const T&)>&& callback) {
    _callback = std::forward<decltype(callback)>(callback);
  }

  /**
   * Resets the callback, making it inactive.
   * This means that the variable will no longer call the callback when it is updated.
   * After calling this, you can set a new callback using `set_callback()`.
   */
  SCU_ALWAYS_INLINE void reset_callback() {
    _callback.reset();
  }

  /**
   * \return True if the callback is active, false otherwise.
   * This can be used to check if the variable has a callback set.
   */
  SCU_ALWAYS_INLINE bool is_callback_active() const {
    return _callback.has_value();
  }

  /**
   * Recreates the publisher with the specified topic name and QoS settings.
   * This is useful if you want to change the topic or QoS settings for publishing.
   *
   * \param publisher_topic_name The new topic name for the publisher.
   * \param publisher_ros QoS settings for the publisher - default is `rclcpp::QoS(1).reliable().transient_local()`.
   */
  SCU_ALWAYS_INLINE void recreate_publisher(
    const std::string& publisher_topic_name,
    rclcpp::QoS publisher_ros = rclcpp::QoS(1).reliable().transient_local()) {
    _publisher = _node->create_publisher<T>(publisher_topic_name, publisher_ros);
  }

  class RosVariableEditor {
    friend RosVariable<T, IgnoreSameValue>;

    RosVariable& _variable;
    T _value;

    explicit RosVariableEditor(RosVariable& variable)
    : _variable(variable) { }

public:
    ~RosVariableEditor() {
      _variable.set(std::move(_value));
      --_variable._edit_count;
    }

    SCU_ALWAYS_INLINE RosVariableEditor& operator=(const T& value) {
      _value = value;
      return *this;
    }
    SCU_ALWAYS_INLINE RosVariableEditor& operator=(T&& value) {
      _value = std::forward<T>(value);
      return *this;
    }
    SCU_ALWAYS_INLINE T& operator*() {
      return _value;
    }
    SCU_ALWAYS_INLINE T* operator->() {
      return &_value;
    }
    /**
     * Flushes the changes made in this editor to the variable.
     * This will set the variable's value to the current value of this editor.
     * It is called automatically when the editor goes out of scope, but can also be called
     * explicitly if you want to apply the changes immediately.
     */
    SCU_ALWAYS_INLINE RosVariableEditor& flush() {
      _variable.set(_value);
      return *this;
    }
    /**
     * Sets a field of the variable's value to a new value.
     * This allows you to modify a specific field of the variable's value directly.
     * \tparam U The type of the field to set.
     * \param field Pointer to the field of the variable's value to set.
     * \param value The new value to set for the field.
     * \return A reference to this editor, allowing for method chaining.
     */
    template<typename U>
    SCU_ALWAYS_INLINE RosVariableEditor& set(U T::*field, const U& value) {
      _value.*field = value;
      return *this;
    }
    /**
     * Sets a field of the variable's value to a new value.
     * This allows you to modify a specific field of the variable's value directly.
     * \tparam U The type of the field to set.
     * \param field Pointer to the field of the variable's value to set.
     * \param value The new value to set for the field.
     * \return A reference to this editor, allowing for method chaining.
     */
    template<typename U>
    SCU_ALWAYS_INLINE RosVariableEditor& set(U T::*field, U&& value) {
      _value.*field = std::forward<U>(value);
      return *this;
    }
  };
  /**
   * Returns an editor for the variable.
   * This allows you to modify the variable's value in a scoped manner.
   * The changes will be applied when the editor goes out of scope.
   *
   * \return A RosVariableEditor that can be used to modify the variable's value.
   * \warning Editor should never outlive the variable it is editing.
   */
  [[nodiscard]] SCU_ALWAYS_INLINE RosVariableEditor edit() & {
    ++_edit_count;
    return RosVariableEditor(*this);
  }
};
}  // namespace scorpio_utils::ros
