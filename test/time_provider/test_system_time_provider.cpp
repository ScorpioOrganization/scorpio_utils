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

#include <gtest/gtest.h>
#include <thread>
#include "scorpio_utils/time_provider/system_time_provider.hpp"

TEST(SystemTimeProvider, BasicUsage) {
  scorpio_utils::time_provider::SystemTimeProvider time_provider;

  auto time = time_provider.get_time();
  auto current_time = std::chrono::system_clock::now().time_since_epoch().count();

  EXPECT_LE(current_time - time, 10000);
  std::this_thread::sleep_for(std::chrono::milliseconds(10));

  time = time_provider.get_time();
  auto next_time = time_provider.get_time();
  EXPECT_GT(next_time, time);
  EXPECT_LT(next_time - time, 20000);
}
