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
#include <optional>
#include <vector>
#include "scorpio_utils/testing/lifetime_helper.hpp"
#include "scorpio_utils/optional_utils.hpp"

TEST(OptionalMap, base_case) {
  /// INITIAL PARAMS
  std::optional<int> initial = 2;

  /// TEST CASE
  auto result = scorpio_utils::optional_map([](int x) { return x + 2; }, initial);

  /// CHECK
  ASSERT_TRUE(initial.has_value()) << "Initial optional should have a value";
  EXPECT_EQ(initial.value(), 2) << "Initial value should be 2";
  ASSERT_TRUE(result.has_value()) << "Result optional should have a value";
  EXPECT_EQ(result.value(), 4) << "Result value should be 4, which is 2 + 2";
}

TEST(OptionalMap, nullopt_case) {
  /// INITIAL PARAMS
  std::optional<int> initial;

  /// TEST CASE
  auto result = scorpio_utils::optional_map([](int x) { return x + 2; }, initial);

  /// CHECK
  EXPECT_FALSE(initial.has_value()) << "Initial optional should not have a value";
  EXPECT_FALSE(result.has_value()) << "Result optional should not have a value, since initial is nullopt";
}

TEST(OptionalMap, to_different_type) {
  /// INITIAL PARAMS
  std::optional<int> initial = 2;

  /// TEST CASE
  auto result = scorpio_utils::optional_map([](int x) {
        return std::vector<int>(static_cast<size_t>(x + 2), 0);
  }, initial);

  /// CHECK
  ASSERT_TRUE(initial.has_value()) << "Initial optional should have a value";
  EXPECT_EQ(initial.value(), 2) << "Initial value should be 2";
  ASSERT_TRUE(result.has_value()) << "Result optional should have a value";
  EXPECT_EQ(result.value(),
    std::vector<int>(4, 0)) << "Result value should be a vector of size 4 filled with 0s, which is 2 + 2";
}

TEST(OptionalMapXVal, base_case) {
  /// INITIAL PARAMS
  std::optional<scorpio_utils::testing::LifetimeHelper> initial = { scorpio_utils::testing::LifetimeHelper() };

  /// TEST CASE
  auto result = scorpio_utils::optional_map([](auto&& x) {
        return std::forward<scorpio_utils::testing::LifetimeHelper>(x);
      }, std::move(initial));

  /// CHECK
  ASSERT_TRUE(initial.has_value()) << "Initial optional should have a value";
  EXPECT_EQ(initial.value().get_copy_count(), 0) << "Initial value should be moved";
  EXPECT_EQ(initial.value().get_move_count(), 1) << "Initial value should be moved";
  ASSERT_TRUE(result.has_value()) << "Result optional should have a value";
  for (size_t i = 1; i < result.value().get_event_log().size(); ++i) {
    EXPECT_NE(result.value().get_event_log()[i], scorpio_utils::testing::LifetimeHelper::EventType::COPY) <<
      "No COPY events should be present in the log, since we are moving the value";
    EXPECT_NE(result.value().get_event_log()[i], scorpio_utils::testing::LifetimeHelper::EventType::COPY_ASSIGN) <<
      "No COPY_ASSIGN events should be present in the log, since we are moving the value";
  }
}

TEST(OptionalMapXVal, nullopt_case) {
  /// INITIAL PARAMS
  std::optional<int> initial;

  /// TEST CASE
  auto result = scorpio_utils::optional_map([](int) { return std::vector<int>(); }, std::move(initial));

  /// CHECK
  EXPECT_FALSE(initial.has_value()) << "Initial optional should not have a value";
  EXPECT_FALSE(result.has_value()) << "Result optional should not have a value, since initial is nullopt";
}

TEST(OptionalFlatten, base_case) {
  /// INITIAL PARAMS
  std::optional<std::optional<int>> initial = 2;

  /// TEST CASE
  auto result = scorpio_utils::optional_flatten(initial);

  /// CHECK
  ASSERT_TRUE(initial.has_value()) << "Initial optional should have a value";
  ASSERT_TRUE(initial.value().has_value()) << "Nested optional should have a value";
  EXPECT_EQ(initial.value().value(), 2) << "Nested value should be 2";
  ASSERT_TRUE(result.has_value()) << "Result optional should have a value";
  EXPECT_EQ(result.value(), 2) << "Result value should be 2, which is the flattened value of the nested optional";
}

TEST(OptionalFlatten, flat_case) {
  /// INITIAL PARAMS
  std::optional<int> initial = 2;

  /// TEST CASE
  auto result = scorpio_utils::optional_flatten(initial);

  /// CHECK
  ASSERT_TRUE(initial.has_value()) << "Initial optional should have a value";
  EXPECT_EQ(initial.value(), 2) << "Initial value should be 2";
  ASSERT_TRUE(result.has_value()) << "Result optional should have a value";
  EXPECT_EQ(result.value(), 2) << "Result value should be 2, which is the same as the initial value";
}

TEST(OptionalFlatten, nullopt_case) {
  /// INITIAL PARAMS
  std::optional<std::optional<int>> initial;

  /// TEST CASE
  auto result = scorpio_utils::optional_flatten(initial);

  /// CHECK
  EXPECT_FALSE(initial.has_value()) << "Initial optional should not have a value";
  EXPECT_FALSE(result.has_value()) << "Result optional should not have a value, since initial is nullopt";
}

TEST(OptionalFlatten, deep_case) {
  /// INITIAL PARAMS
  std::optional<std::optional<std::optional<int>>> initial = 2;

  /// TEST CASE
  auto result = scorpio_utils::optional_flatten(initial);

  /// CHECK
  ASSERT_TRUE(initial.has_value()) << "Initial optional should have a value";
  ASSERT_TRUE(initial.value().has_value()) << "Nested optional should have a value";
  ASSERT_TRUE(initial.value().value().has_value()) << "Deep nested optional should have a value";
  EXPECT_EQ(initial.value().value().value(), 2) << "Deep nested value should be 2";
  ASSERT_TRUE(result.has_value()) << "Result optional should have a value";
  EXPECT_EQ(result.value(), 2) << "Result value should be 2, which is the flattened value of the deep nested optional";
}

TEST(OptionalFlatten, deep_nullopt_case) {
  /// INITIAL PARAMS
  std::optional<std::optional<std::optional<int>>> initial;

  /// TEST CASE
  auto result = scorpio_utils::optional_flatten(initial);

  /// CHECK
  EXPECT_FALSE(initial.has_value()) << "Initial optional should not have a value";
  EXPECT_FALSE(result.has_value()) << "Result optional should not have a value, since initial is nullopt";
}

TEST(OptionalFlattenXVal, base_case) {
  /// INITIAL PARAMS
  std::optional<std::optional<scorpio_utils::testing::LifetimeHelper>> initial =
  { { scorpio_utils::testing::LifetimeHelper() } };

  /// TEST CASE
  auto result = scorpio_utils::optional_flatten(std::move(initial));

  /// CHECK
  ASSERT_TRUE(initial.has_value()) << "Initial optional should have a value";
  EXPECT_TRUE(initial.value().has_value()) << "Nested optional should have a value";
  EXPECT_EQ(initial.value().value().get_copy_count(), 0) << "Nested value should not be copied";
  EXPECT_EQ(initial.value().value().get_move_count(), 1) << "Nested value should be moved";
  ASSERT_TRUE(result.has_value()) << "Result optional should have a value";
  for (size_t i = 1; i < result.value().get_event_log().size(); ++i) {
    EXPECT_NE(result.value().get_event_log()[i], scorpio_utils::testing::LifetimeHelper::EventType::COPY) <<
      "No COPY events should be present in the log, since we are moving the value";
    EXPECT_NE(result.value().get_event_log()[i], scorpio_utils::testing::LifetimeHelper::EventType::COPY_ASSIGN) <<
      "No COPY_ASSIGN events should be present in the log, since we are moving the value";
  }
}

TEST(OptionalFlattenXVal, flat_case) {
  /// INITIAL PARAMS
  std::optional<scorpio_utils::testing::LifetimeHelper> initial = { scorpio_utils::testing::LifetimeHelper() };

  /// TEST CASE
  auto result = scorpio_utils::optional_flatten(std::move(initial));

  /// CHECK
  ASSERT_TRUE(initial.has_value()) << "Initial optional should have a value";
  EXPECT_EQ(initial.value().get_copy_count(), 0) << "Initial value should not be copied";
  EXPECT_EQ(initial.value().get_move_count(), 1) << "Initial value should be moved";
  ASSERT_TRUE(result.has_value()) << "Result optional should have a value";
  for (size_t i = 1; i < result.value().get_event_log().size(); ++i) {
    EXPECT_NE(result.value().get_event_log()[i], scorpio_utils::testing::LifetimeHelper::EventType::COPY) <<
      "No COPY events should be present in the log, since we are moving the value";
    EXPECT_NE(result.value().get_event_log()[i], scorpio_utils::testing::LifetimeHelper::EventType::COPY_ASSIGN) <<
      "No COPY_ASSIGN events should be present in the log, since we are moving the value";
  }
}

TEST(OptionalFlattenXVal, nullopt_case) {
  /// INITIAL PARAMS
  std::optional<std::optional<int>> initial;

  /// TEST CASE
  auto result = scorpio_utils::optional_flatten(std::move(initial));

  /// CHECK
  EXPECT_FALSE(initial.has_value()) << "Initial optional should not have a value";
  EXPECT_FALSE(result.has_value()) << "Result optional should not have a value, since initial is nullopt";
}

TEST(OptionalFlattenXVal, deep_case) {
  /// INITIAL PARAMS
  std::optional<std::optional<std::optional<scorpio_utils::testing::LifetimeHelper>>> initial =
  { { { scorpio_utils::testing::LifetimeHelper() } } };

  /// TEST CASE
  auto result = scorpio_utils::optional_flatten(std::move(initial));

  /// CHECK
  ASSERT_TRUE(initial.has_value()) << "Initial optional should have a value";
  EXPECT_TRUE(initial.value().has_value()) << "Nested optional should have a value";
  EXPECT_TRUE(initial.value().value().has_value()) << "Deep nested optional should have a value";
  EXPECT_EQ(initial.value().value().value().get_copy_count(), 0) << "Deep nested value should not be copied";
  EXPECT_EQ(initial.value().value().value().get_move_count(), 1) << "Deep nested value should be moved";
  ASSERT_TRUE(result.has_value()) << "Result optional should have a value";
  for (size_t i = 1; i < result.value().get_event_log().size(); ++i) {
    EXPECT_NE(result.value().get_event_log()[i], scorpio_utils::testing::LifetimeHelper::EventType::COPY) <<
      "No COPY events should be present in the log, since we are moving the value";
    EXPECT_NE(result.value().get_event_log()[i], scorpio_utils::testing::LifetimeHelper::EventType::COPY_ASSIGN) <<
      "No COPY_ASSIGN events should be present in the log, since we are moving the value";
  }
}

TEST(OptionalFlattenXVal, deep_nullopt_case) {
  /// INITIAL PARAMS
  std::optional<std::optional<std::optional<int>>> initial;

  /// TEST CASE
  auto result = scorpio_utils::optional_flatten(std::move(initial));

  /// CHECK
  EXPECT_FALSE(initial.has_value()) << "Initial optional should not have a value";
  EXPECT_FALSE(result.has_value()) << "Result optional should not have a value, since initial is nullopt";
}
