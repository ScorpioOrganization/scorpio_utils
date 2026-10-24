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
#include <chrono>
#include <thread>
#include "scorpio_utils/time_provider/steady_time_provider.hpp"

TEST(SteadyTimeProvider, FollowsTheMonotonicClock) {
  scorpio_utils::time_provider::SteadyTimeProvider time_provider;
  const auto steady_now = std::chrono::duration_cast<std::chrono::nanoseconds>(
    std::chrono::steady_clock::now().time_since_epoch()).count();
  const auto time = time_provider.get_time();
  EXPECT_GE(time, steady_now);
  EXPECT_LT(time - steady_now, 10'000'000);
}

TEST(SteadyTimeProvider, NeverGoesBack) {
  scorpio_utils::time_provider::SteadyTimeProvider time_provider;
  auto previous = time_provider.get_time();
  for (int i = 0; i < 1000; ++i) {
    const auto now = time_provider.get_time();
    ASSERT_GE(now, previous);
    previous = now;
  }
  std::this_thread::sleep_for(std::chrono::milliseconds(5));
  EXPECT_GE(time_provider.get_time() - previous, 5'000'000);
}
