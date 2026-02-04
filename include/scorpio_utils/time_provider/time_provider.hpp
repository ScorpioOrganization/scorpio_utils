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
