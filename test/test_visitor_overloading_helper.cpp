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
#include <variant>
#include "scorpio_utils/misc.hpp"

TEST(VisitorOverloadingHelperTest, BasicUsage) {
  auto helper = scorpio_utils::VisitorOverloadingHelper{
    [](int& i) { i += 1; },
    [](double& d) { d += 0.5; },
    [](std::string& str) { str += " Hello world"; }
  };

  std::variant<int, double, std::string> v = 42;
  std::visit(helper, v);
  ASSERT_TRUE(std::holds_alternative<int>(v));
  EXPECT_EQ(std::get<int>(v), 43);
  v = 41.7;
  std::visit(helper, v);
  ASSERT_TRUE(std::holds_alternative<double>(v));
  EXPECT_EQ(std::get<double>(v), 42.2);
  v = std::string("Hello");
  std::visit(helper, v);
  ASSERT_TRUE(std::holds_alternative<std::string>(v));
  EXPECT_EQ(std::get<std::string>(v), "Hello Hello world");
}
