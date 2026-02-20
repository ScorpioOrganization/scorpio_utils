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
  int64_t get_time() const override {
    return std::chrono::system_clock::now().time_since_epoch().count();
  }
};
}  // namespace scorpio_utils::time_provider
