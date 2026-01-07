#pragma once

#include <chrono>
#include <cstddef>
#include "time_provider.hpp"

namespace scorpio_utils::time_provider {
/**
 * A time provider that retrieves the current system time.
 * This class uses the system clock to get the current time in nanoseconds.
 */
class SystemTimeProvider final : public TimeProvider {
public:
  /**
   * Gets the current time in nanoseconds.
   * \return Current time in nanoseconds.
   */
  int64_t get_time() override {
    return std::chrono::system_clock::now().time_since_epoch().count();
  }
};
}  // namespace scorpio_utils::time_provider
