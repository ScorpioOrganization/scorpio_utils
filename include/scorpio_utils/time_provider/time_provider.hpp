#pragma once

#include <cstdint>

namespace scorpio_utils::time_provider {
/**
 * An abstract base class for time providers.
 * This class defines the interface for retrieving the current time in nanoseconds.
 */
class TimeProvider {
public:
  virtual ~TimeProvider() = default;

  /**
   * Gets the current time in nanoseconds.
   * \return Current time in nanoseconds.
   */
  virtual int64_t get_time() = 0;
};
}  // namespace scorpio_utils::time_provider
