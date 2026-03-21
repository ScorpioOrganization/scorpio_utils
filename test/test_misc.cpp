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

#include <memory>

class FailedAssertionException : public std::runtime_error {
public:
  FailedAssertionException()
  : std::runtime_error("ASSERTION FAILED") { }
};

#define SCU_ASSERT_TERMINATE do { \
  throw FailedAssertionException(); \
} while (0)

#include "scorpio_utils/defer.hpp"
#include "scorpio_utils/misc.hpp"

TEST(DynamicAs, unique_ptr_success) {
  struct Base { virtual ~Base() = default; };
  struct Derived : Base { ~Derived() override = default; };

  std::unique_ptr<Base> base_ptr = std::make_unique<Derived>();
  auto derived_ptr = scorpio_utils::dynamic_as<Derived>(std::move(base_ptr));
  EXPECT_NE(derived_ptr, nullptr) << "dynamic_as should successfully cast unique_ptr<Base> to unique_ptr<Derived>";
}

TEST(DynamicAs, unique_ptr_failed) {
  struct Base { virtual ~Base() = default; };
  struct Derived { };

  std::unique_ptr<Base> base_ptr = std::make_unique<Base>();
  EXPECT_THROW(scorpio_utils::dynamic_as<Derived>(std::move(base_ptr)),
    FailedAssertionException) <<
    "dynamic_as should throw an exception when casting unique_ptr<Base> to unique_ptr<Derived>";
}

TEST(DynamicAs, shared_ptr_success) {
  struct Base { virtual ~Base() = default; };
  struct Derived : Base { ~Derived() override = default; };

  std::shared_ptr<Base> base_ptr = std::make_shared<Derived>();
  auto derived_ptr = scorpio_utils::dynamic_as<Derived>(std::move(base_ptr));
  EXPECT_NE(derived_ptr, nullptr) << "dynamic_as should successfully cast shared_ptr<Base> to shared_ptr<Derived>";
}

TEST(DynamicAs, shared_ptr_failed) {
  struct Base { virtual ~Base() = default; };
  struct Derived { };

  std::shared_ptr<Base> base_ptr = std::make_shared<Base>();
  EXPECT_THROW(scorpio_utils::dynamic_as<Derived>(std::move(base_ptr)),
    FailedAssertionException) <<
    "dynamic_as should throw an exception when casting shared_ptr<Base> to shared_ptr<Derived>";
}

TEST(DynamicAs, raw_ptr_success) {
  struct Base { virtual ~Base() = default; };
  struct Derived : Base { ~Derived() override = default; };

  Base* const base_ptr = new Derived;
  SCU_DEFER([base_ptr] { delete base_ptr; });
  auto derived_ptr = scorpio_utils::dynamic_as<Derived>(std::move(base_ptr));
  EXPECT_NE(derived_ptr, nullptr) << "dynamic_as should successfully cast shared_ptr<Base> to shared_ptr<Derived>";
}

TEST(DynamicAs, raw_ptr_failed) {
  struct Base { virtual ~Base() = default; };
  struct Derived { };

  Base* const base_ptr = new Base;
  SCU_DEFER([base_ptr] { delete base_ptr; });
  EXPECT_THROW(scorpio_utils::dynamic_as<Derived>(std::move(base_ptr)),
    FailedAssertionException) <<
    "dynamic_as should throw an exception when casting shared_ptr<Base> to shared_ptr<Derived>";
}
