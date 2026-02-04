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
#include "scorpio_utils/time_provider/lazy_time_provider.hpp"

TEST(LazyTimeProvider, BasicUsage) {
  const int64_t base_offset = 100'000'000;
  scorpio_utils::time_provider::LazyTimeProvider time_provider(base_offset);

  EXPECT_EQ(time_provider.get_time_offset(), base_offset);
  auto time = time_provider.get_time();
  auto current_time = std::chrono::system_clock::now().time_since_epoch().count();
  EXPECT_LE(current_time - time, time_provider.get_time_offset());
  std::this_thread::sleep_for(std::chrono::nanoseconds(time_provider.get_time_offset() * 2));
  auto next_time = time_provider.get_time();
  EXPECT_GT(next_time, time);
  EXPECT_LT(next_time - time, base_offset * 3);
  time_provider.set_time_offset(base_offset * 2);
  EXPECT_EQ(time_provider.get_time_offset(), base_offset * 2);
  std::this_thread::sleep_for(std::chrono::nanoseconds(time_provider.get_time_offset()));
  time = time_provider.get_time();
  std::this_thread::sleep_for(std::chrono::nanoseconds(time_provider.get_time_offset()));
  next_time = time_provider.get_time();
  EXPECT_GT(next_time, time);
  EXPECT_LT((next_time - time) % time_provider.get_time_offset(), base_offset);
  EXPECT_GT(next_time - time, base_offset);
}
