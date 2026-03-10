#pragma once

#include <atomic>
#include <cstdint>
#include "scorpio_utils/time_provider/time_provider.hpp"

namespace scorpio_utils::testing {
class MockTimeProvider : public scorpio_utils::time_provider::TimeProvider {
  std::atomic<int64_t> _time;
  std::atomic<int64_t> _time_offset;

public:
  MockTimeProvider()
  : _time(0) { }

  explicit MockTimeProvider(int64_t initial_time)
  : _time(initial_time) { }

  void advance_time(int64_t nanoseconds) noexcept {
    _time.fetch_add(nanoseconds, std::memory_order_relaxed);
  }

  void set_time(int64_t time) noexcept {
    _time.store(time, std::memory_order_relaxed);
  }

  int64_t get_time() const override {
    return _time.load(std::memory_order_relaxed);
  }

  void set_time_offset(int64_t time) noexcept {
    _time_offset.store(time, std::memory_order_relaxed);
  }

  int64_t get_time_offset() const noexcept {
    return _time_offset.load(std::memory_order_relaxed);
  }
};
}  // namespace scorpio_utils::testing
