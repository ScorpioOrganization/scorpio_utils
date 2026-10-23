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
#include <cstdint>
#include "time_provider.hpp"

namespace scorpio_utils::time_provider {
/**
 * A time provider that reads the monotonic clock (`steady_clock`).
 * Unlike `SystemTimeProvider` it never jumps when the system time is set (e.g. by NTP at boot), so it is the one to
 * measure durations and timeouts with. Its epoch is arbitrary (usually the boot), it is not a wall-clock time.
 */
class SteadyTimeProvider final : public TimeProvider {
public:
  /**
   * Gets the current monotonic time in nanoseconds.
   * \return Current time in nanoseconds.
   */
  int64_t get_time() const override {
    return std::chrono::duration_cast<std::chrono::nanoseconds>(
      std::chrono::steady_clock::now().time_since_epoch()).count();
  }
};
}  // namespace scorpio_utils::time_provider
