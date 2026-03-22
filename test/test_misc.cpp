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

#include <stdexcept>
#include <memory>

class FailedAssertionException : public std::runtime_error {
public:
  FailedAssertionException()
  : std::runtime_error("ASSERTION FAILED") { }
};

#define SCU_ASSERT_TERMINATE do { \
  throw FailedAssertionException(); \
} while (0)

#include "scorpio_utils/decorators.hpp"
#include "scorpio_utils/defer.hpp"
#include "scorpio_utils/misc.hpp"

struct Base { virtual ~Base() = default; };
struct Derived : Base { ~Derived() override = default; };
struct Derived2 : Base { ~Derived2() override = default; };

static_assert(
  std::is_same_v<decltype(scorpio_utils::dynamic_as<Derived>(std::declval<std::unique_ptr<Base>>())),
  std::unique_ptr<Derived>>,
  "dynamic_as should preserve cv qualification from the From type");

static_assert(
  std::is_same_v<decltype(scorpio_utils::dynamic_as<Derived>(std::declval<std::unique_ptr<const Base>>())),
  std::unique_ptr<const Derived>>,
  "dynamic_as should preserve cv qualification from the From type");

static_assert(
  std::is_same_v<decltype(scorpio_utils::dynamic_as<Derived>(std::declval<std::unique_ptr<volatile Base>>())),
  std::unique_ptr<volatile Derived>>,
  "dynamic_as should preserve cv qualification from the From type");

static_assert(
  std::is_same_v<decltype(scorpio_utils::dynamic_as<Derived>(std::declval<std::unique_ptr<const volatile Base>>())),
  std::unique_ptr<const volatile Derived>>,
  "dynamic_as should preserve cv qualification from the From type");

static_assert(
  std::is_same_v<decltype(scorpio_utils::dynamic_as<Derived>(std::declval<const std::shared_ptr<Base>>())),
  std::shared_ptr<Derived>>,
  "dynamic_as should preserve cv qualification from the From type");

static_assert(
  std::is_same_v<decltype(scorpio_utils::dynamic_as<Derived>(std::declval<std::shared_ptr<const Base>>())),
  std::shared_ptr<const Derived>>,
  "dynamic_as should preserve cv qualification from the From type");

static_assert(
  std::is_same_v<decltype(scorpio_utils::dynamic_as<Derived>(std::declval<const std::shared_ptr<volatile Base>>())),
  std::shared_ptr<volatile Derived>>,
  "dynamic_as should preserve cv qualification from the From type");

static_assert(
  std::is_same_v<decltype(scorpio_utils::dynamic_as<Derived>(std::declval<std::shared_ptr<const volatile Base>>())),
  std::shared_ptr<const volatile Derived>>,
  "dynamic_as should preserve cv qualification from the From type");

static_assert(
  std::is_same_v<decltype(scorpio_utils::dynamic_as<Derived>(std::declval<Base* const volatile>())),
  Derived*>,
  "dynamic_as should preserve cv qualification from the From type");

static_assert(
  std::is_same_v<decltype(scorpio_utils::dynamic_as<Derived>(std::declval<const Base* volatile>())),
  const Derived*>,
  "dynamic_as should preserve cv qualification from the From type");

static_assert(
  std::is_same_v<decltype(scorpio_utils::dynamic_as<Derived>(std::declval<volatile Base*>())),
  volatile Derived*>,
  "dynamic_as should preserve cv qualification from the From type");

static_assert(
  std::is_same_v<decltype(scorpio_utils::dynamic_as<Derived>(std::declval<const volatile Base*>())),
  const volatile Derived*>,
  "dynamic_as should preserve cv qualification from the From type");


TEST(DynamicAs, unique_ptr_success) {
  std::unique_ptr<Base> base_ptr = std::make_unique<Derived>();
  auto derived_ptr = scorpio_utils::dynamic_as<Derived>(std::move(base_ptr));
  EXPECT_NE(derived_ptr, nullptr) << "dynamic_as should successfully cast unique_ptr<Base> to unique_ptr<Derived>";
}

TEST(DynamicAs, unique_ptr_failed) {
  std::unique_ptr<Base> base_ptr = std::make_unique<Derived2>();
  EXPECT_THROW(scorpio_utils::dynamic_as<Derived>(std::move(base_ptr)),
    FailedAssertionException) <<
    "dynamic_as should throw an exception when casting unique_ptr<Base> to unique_ptr<Derived>";
}

TEST(DynamicAs, unique_ptr_custom_deleter_success) {
  Base* raw_ptr = new Derived;
  SCU_DEFER([raw_ptr] { delete raw_ptr; });
  size_t custom_deleter_called = 0;
  auto custom_deleter = [&custom_deleter_called](Base* p) { custom_deleter_called += SCU_AS(size_t, p != nullptr); };
  std::unique_ptr<Base, decltype(custom_deleter)> base_ptr(raw_ptr, custom_deleter);
  auto derived_ptr = scorpio_utils::dynamic_as<Derived>(std::move(base_ptr));
  EXPECT_NE(derived_ptr,
    nullptr) <<
    "dynamic_as should successfully cast unique_ptr<Base, CustomDeleter> to unique_ptr<Derived, CustomDeleter>";
  static_assert(std::is_same_v<std::decay_t<decltype(custom_deleter)>,
    std::decay_t<decltype(derived_ptr.get_deleter())>>,
    "dynamic_as should preserve the custom deleter type when casting unique_ptr");
  EXPECT_EQ(custom_deleter_called, 0) << "custom deleter should not be called when the cast is successful";
  base_ptr.reset();
  EXPECT_EQ(custom_deleter_called, 0) << "custom deleter should not be called when the cast is successful";
  derived_ptr.reset();
  EXPECT_EQ(custom_deleter_called, 1) << "custom deleter should be called when the unique_ptr is reset";
}

TEST(DynamicAs, unique_ptr_custom_deleter_failed) {
  Base* raw_ptr = new Derived2;
  SCU_DEFER([raw_ptr] { delete raw_ptr; });
  size_t custom_deleter_called = 0;
  auto custom_deleter = [&custom_deleter_called](Base* p) { custom_deleter_called += SCU_AS(size_t, p != nullptr); };
  {
    // Scope is needed here because of shenanigans with inlining
    std::unique_ptr<Base, decltype(custom_deleter)> base_ptr(raw_ptr, custom_deleter);

    EXPECT_THROW(scorpio_utils::dynamic_as<Derived>(std::move(base_ptr)),
      FailedAssertionException) <<
      "dynamic_as should throw an exception when casting unique_ptr<Base, "
      "CustomDeleter> to unique_ptr<Derived, CustomDeleter>";
  }
  EXPECT_EQ(custom_deleter_called, 1) <<
    "custom deleter should be called when the cast fails and the unique_ptr is destroyed";
}

TEST(DynamicAs, shared_ptr_success) {
  std::shared_ptr<Base> base_ptr = std::make_shared<Derived>();
  auto derived_ptr = scorpio_utils::dynamic_as<Derived>(std::move(base_ptr));
  EXPECT_NE(derived_ptr, nullptr) << "dynamic_as should successfully cast shared_ptr<Base> to shared_ptr<Derived>";
}

TEST(DynamicAs, shared_ptr_failed) {
  std::shared_ptr<Base> base_ptr = std::make_shared<Derived2>();
  EXPECT_THROW(scorpio_utils::dynamic_as<Derived>(std::move(base_ptr)),
    FailedAssertionException) <<
    "dynamic_as should throw an exception when casting shared_ptr<Base> to shared_ptr<Derived>";
}

TEST(DynamicAs, raw_ptr_success) {
  Base* const base_ptr = new Derived;
  SCU_DEFER([base_ptr] { delete base_ptr; });
  auto derived_ptr = scorpio_utils::dynamic_as<Derived>(std::move(base_ptr));
  EXPECT_NE(derived_ptr, nullptr) << "dynamic_as should successfully cast Base* to Derived*";
}

TEST(DynamicAs, raw_ptr_failed) {
  Base* const base_ptr = new Derived2;
  SCU_DEFER([base_ptr] { delete base_ptr; });
  EXPECT_THROW(scorpio_utils::dynamic_as<Derived>(std::move(base_ptr)),
    FailedAssertionException) <<
    "dynamic_as should throw an exception when casting Base* to Derived*";
}

TEST(DynamicAs, null_ptr) {
  std::unique_ptr<Base> null_unique_ptr;
  auto result_unique_ptr = scorpio_utils::dynamic_as<Derived>(std::move(null_unique_ptr));
  EXPECT_EQ(result_unique_ptr, nullptr) << "dynamic_as should return nullptr when casting a null unique_ptr";

  std::shared_ptr<Base> null_shared_ptr;
  auto result_shared_ptr = scorpio_utils::dynamic_as<Derived>(null_shared_ptr);
  EXPECT_EQ(result_shared_ptr, nullptr) << "dynamic_as should return nullptr when casting a null shared_ptr";

  Base* null_raw_ptr = nullptr;
  auto result_raw_ptr = scorpio_utils::dynamic_as<Derived>(std::move(null_raw_ptr));
  EXPECT_EQ(result_raw_ptr, nullptr) << "dynamic_as should return nullptr when casting a null raw pointer";
}
