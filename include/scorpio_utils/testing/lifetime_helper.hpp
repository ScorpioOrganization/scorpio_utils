#pragma once

#include <atomic>
#include <iostream>
#include <vector>
#include "scorpio_utils/decorators.hpp"

namespace scorpio_utils::testing {
/**
 * A class to demonstrate the lifetime management of objects.
 * This class can be used to track the creation, copying, moving, and destruction of objects.
 * It can be useful for debugging and understanding object lifetimes in C++.
 */
class LifetimeHelper {
public:
  enum class EventType : char {
    CREATED,
    COPY,
    MOVE,
    COPY_ASSIGN,
    MOVE_ASSIGN,
    HAS_BEEN_COPIED,
    HAS_BEEN_MOVED,
  };

private:
  static std::atomic<size_t> _created_counter;
  static std::atomic<size_t> _destroyed_counter;
  mutable std::vector<EventType> _event_log;
  mutable std::vector<EventType> _value_event_log;
  size_t _copy_assign_count;
  size_t _move_assign_count;
  mutable size_t _copy_count;
  size_t _move_count;

public:
  LifetimeHelper()
  : _event_log{EventType::CREATED},
    _value_event_log{EventType::CREATED},
    _copy_assign_count(0),
    _move_assign_count(0),
    _copy_count(0),
    _move_count(0) {
    _created_counter.fetch_add(1, std::memory_order_relaxed);
    #ifdef SCU_LIFETIME_HELPER_PRINTS
    std::cout << "LifetimeHelper created\n";
    #endif
  }
  ~LifetimeHelper() {
    _destroyed_counter.fetch_add(1, std::memory_order_relaxed);
    #ifdef SCU_LIFETIME_HELPER_PRINTS
    std::cout << "LifetimeHelper destroyed\n";
    #endif
  }
  LifetimeHelper(const LifetimeHelper& other)
  : _event_log{EventType::COPY},
    _value_event_log{other._value_event_log},
    _copy_assign_count(0),
    _move_assign_count(0),
    _copy_count(0),
    _move_count(0) {
    _value_event_log.push_back(EventType::COPY);
    other._event_log.push_back(EventType::HAS_BEEN_COPIED);
    _created_counter.fetch_add(1, std::memory_order_relaxed);
    ++other._copy_count;
    #ifdef SCU_LIFETIME_HELPER_PRINTS
    std::cout << "LifetimeHelper copied\n";
    #endif
  }
  LifetimeHelper(LifetimeHelper&& other)
  : _event_log{EventType::MOVE},
    _value_event_log{other._value_event_log},
    _copy_assign_count(0),
    _move_assign_count(0),
    _copy_count(0),
    _move_count(0) {
    _value_event_log.push_back(EventType::MOVE);
    other._event_log.push_back(EventType::HAS_BEEN_MOVED);
    _created_counter.fetch_add(1, std::memory_order_relaxed);
    ++other._move_count;
    #ifdef SCU_LIFETIME_HELPER_PRINTS
    std::cout << "LifetimeHelper moved\n";
    #endif
  }
  LifetimeHelper& operator=(const LifetimeHelper& other) {
    _value_event_log = other._value_event_log;
    _value_event_log.push_back(EventType::COPY);
    _event_log.push_back(EventType::COPY_ASSIGN);
    other._event_log.push_back(EventType::HAS_BEEN_COPIED);
    ++_copy_assign_count;
    ++other._copy_count;
    #ifdef SCU_LIFETIME_HELPER_PRINTS
    std::cout << "LifetimeHelper copy assigned\n";
    #endif
    return *this;
  }
  LifetimeHelper& operator=(LifetimeHelper&& other) {
    _value_event_log = other._value_event_log;
    _value_event_log.push_back(EventType::MOVE);
    _event_log.push_back(EventType::MOVE_ASSIGN);
    other._event_log.push_back(EventType::HAS_BEEN_MOVED);
    ++_move_assign_count;
    other._move_count++;
    #ifdef SCU_LIFETIME_HELPER_PRINTS
    std::cout << "LifetimeHelper move assigned\n";
    #endif
    return *this;
  }

  /**
   * Returns the amount of times this object has been copied.
   * \return The number of times this object has been copied.
   */
  SCU_ALWAYS_INLINE auto get_copy_count() const {
    return _copy_count;
  }

  /**
   * Returns the amount of times this object has been moved.
   * \return The number of times this object has been moved.
   */
  SCU_ALWAYS_INLINE auto get_move_count() const {
    return _move_count;
  }

  /**
   * Returns the amount of times this object has been copy assigned.
   * \return The number of times this object has been copy assigned.
   */
  SCU_ALWAYS_INLINE auto get_copy_assign_count() const {
    return _copy_assign_count;
  }

  /**
   * Returns the amount of times this object has been move assigned.
   * \return The number of times this object has been move assigned.
   */
  SCU_ALWAYS_INLINE auto get_move_assign_count() const {
    return _move_assign_count;
  }

  /**
   * Returns the event log of this object.
   * \return The event log of this object.
   */
  SCU_ALWAYS_INLINE const auto& get_event_log() const {
    return _event_log;
  }

  /**
   * Returns the event log of the value of this object.
   * This is useful for tracking the lifetime of the value itself.
   * \return The event log of the value of this object.
   */
  SCU_ALWAYS_INLINE const auto& get_value_event_log() const {
    return _value_event_log;
  }

  /**
   * Returns the number of objects created.
   * \return The number of objects created.
   */
  static auto get_created_count() {
    return _created_counter.load(std::memory_order_relaxed);
  }

  /**
   * Returns the number of objects destroyed.
   * \return The number of objects destroyed.
   */
  static auto get_destroyed_count() {
    return _destroyed_counter.load(std::memory_order_relaxed);
  }

  /**
   * Returns the number of existing objects.
   * \return The number of existing objects.
   */
  static auto get_existing_count() {
    return _created_counter.load(std::memory_order_relaxed) - _destroyed_counter.load(std::memory_order_relaxed);
  }

  /**
   * Resets the counters for created, destroyed, and existing objects.
   * \warning This will fail if there are still existing objects.
   */
  static bool reset_counters() {
    if (get_existing_count() != 0) {
      return false;
    }
    _created_counter.store(0, std::memory_order_relaxed);
    _destroyed_counter.store(0, std::memory_order_relaxed);
    return true;
  }
};
}  // namespace scorpio_utils::testing

std::ostream& operator<<(std::ostream& str, scorpio_utils::testing::LifetimeHelper::EventType event);
