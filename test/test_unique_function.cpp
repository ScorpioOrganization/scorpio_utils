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
#define SCU_ASSERT_TERMINATE throw std::runtime_error("Assertion failed");
#include "scorpio_utils/unique_function.hpp"

TEST(UniqueFunction, baseUsage) {
  int x = 0;
  scorpio_utils::UniqueFunction<int()> f([x]() mutable {
      return x += 2;
    });

  ASSERT_TRUE(f);
  ASSERT_TRUE(f.has_value());
  EXPECT_EQ(f.call(), 2);
  EXPECT_EQ(f(), 4);
}

TEST(UniqueFunction, move) {
  int x = 0;
  scorpio_utils::UniqueFunction<int()> f([x]() mutable {
      return x += 2;
    });

  ASSERT_TRUE(f);
  ASSERT_TRUE(f.has_value());
  EXPECT_EQ(f(), 2);
  auto g = std::move(f);
  ASSERT_TRUE(g);
  ASSERT_TRUE(g.has_value());
  EXPECT_EQ(g(), 4);
  EXPECT_FALSE(f);
  EXPECT_FALSE(f.has_value());
  EXPECT_ANY_THROW(f());

  f = std::move(g);
  ASSERT_TRUE(f);
  ASSERT_TRUE(f.has_value());
  EXPECT_EQ(f.call(), 6);
  EXPECT_FALSE(g);
  EXPECT_FALSE(g.has_value());
  EXPECT_ANY_THROW(g());
  EXPECT_ANY_THROW(g.call());
}

TEST(UniqueFunction, unutilized) {
  scorpio_utils::UniqueFunction<int()> f;
  EXPECT_FALSE(f);
  EXPECT_FALSE(f.has_value());
  EXPECT_ANY_THROW(f());
  EXPECT_ANY_THROW(f.call());
}
