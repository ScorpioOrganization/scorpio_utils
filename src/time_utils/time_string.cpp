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

#include "scorpio_utils/time_utils/time_string.hpp"

#include <chrono>
#include <cstddef>
#include <ctime>
#include <iomanip>

std::string scorpio_utils::time_utils::time_string(int64_t nanosec, const char* format) {
  std::chrono::nanoseconds timestamp(nanosec);
  std::chrono::time_point<std::chrono::system_clock> now(timestamp);
  auto time = std::chrono::system_clock::to_time_t(now);
  std::tm* tm = std::localtime(&time);
  std::stringstream ss;
  ss << std::put_time(tm, format);
  return ss.str();
}
