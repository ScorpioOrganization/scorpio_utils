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
#include "scorpio_utils/testing/lifetime_helper.hpp"
#include "scorpio_utils/threading/thread_pool.hpp"

TEST(AndThen, BasicUsage) {
  std::shared_ptr<scorpio_utils::threading::ThreadPool> pool =
    std::make_shared<scorpio_utils::threading::ThreadPool>(2);

  auto future = pool->async([]() -> int {
        // To ensure that the task takes some time
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
        return 42;
  });

  EXPECT_FALSE(future->is_done());
  EXPECT_FALSE(future->has_done_fn());
  EXPECT_FALSE(future->has_functions());

  auto result = future->and_map([](int value) {
        return value + 8;
  }, pool)->await();

  EXPECT_TRUE(future->is_done());
  EXPECT_FALSE(future->has_done_fn());
  EXPECT_TRUE(future->has_functions());

  EXPECT_EQ(future->get_state(), decltype(future)::element_type::State::DONE);
  EXPECT_EQ(result, 50);
}

TEST(AndThen, BasicUsageVoid) {
  std::shared_ptr<scorpio_utils::threading::ThreadPool> pool =
    std::make_shared<scorpio_utils::threading::ThreadPool>(2);

  auto future = pool->async([]() {
        // To ensure that the task takes some time
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
  });

  std::atomic<bool> map_called = false;
  std::atomic<bool> map_move_called = false;

  future->and_map([&map_called]() {
      std::this_thread::sleep_for(std::chrono::milliseconds(10));
      map_called.store(true, std::memory_order_relaxed);
  }, pool);

  auto future2 = std::move(*future).and_map([&map_move_called]() {
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
        map_move_called.store(true, std::memory_order_relaxed);
  }, pool);

  EXPECT_FALSE(map_called);
  EXPECT_FALSE(map_move_called);

  EXPECT_FALSE(future->is_done());
  EXPECT_TRUE(future->has_done_fn());
  EXPECT_TRUE(future->has_functions());

  future2->await();

  EXPECT_TRUE(future->is_done());
  EXPECT_TRUE(map_called);
  EXPECT_TRUE(map_move_called);
  EXPECT_EQ(future->get_state(), decltype(future)::element_type::State::MOVED);

  // Test shall finish correctly without any errors nor getting stuck
}

TEST(AndThen, BasicUsageEvaluated) {
  std::shared_ptr<scorpio_utils::threading::ThreadPool> pool =
    std::make_shared<scorpio_utils::threading::ThreadPool>(2);

  auto future = pool->async([]() -> int {
        return 42;
  });

  EXPECT_EQ(future->await(), 42);

  auto result = future->and_map([](int value) {
        return value + 8;
  }, pool)->await();

  EXPECT_EQ(result, 50);
}

TEST(AndThen, MoveUsage) {
  std::shared_ptr<scorpio_utils::threading::ThreadPool> pool =
    std::make_shared<scorpio_utils::threading::ThreadPool>(2);

  auto future = pool->async([]() -> int {
        // To ensure that the task takes some time
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
        return 42;
  });

  auto moved_future = std::move(*future).and_map([](int value) {
        return value + 8;
  }, pool);

  EXPECT_EQ(moved_future->await(), 50);
  EXPECT_ANY_THROW(future->and_map([](auto) { }, pool));
  EXPECT_ANY_THROW(std::move(*future).and_map([](auto) { }, pool));
  EXPECT_ANY_THROW(std::move(*future).and_map([](auto) { }, pool));
}

TEST(AndThen, MoveUsageEvaluated) {
  std::shared_ptr<scorpio_utils::threading::ThreadPool> pool =
    std::make_shared<scorpio_utils::threading::ThreadPool>(2);

  auto future = pool->async([]() -> int {
        return 42;
  });

  EXPECT_EQ(future->await(), 42);

  auto moved_future = std::move(*future).and_map([](int value) {
        return value + 8;
  }, pool);

  EXPECT_EQ(moved_future->await(), 50);
  EXPECT_ANY_THROW(future->and_map([](auto) { }, pool));
  EXPECT_ANY_THROW(std::move(*future).and_map([](auto) { }, pool));
  EXPECT_ANY_THROW(std::move(*future).and_map([](auto) { }, pool));
}

TEST(AndThen, MoveLifetime) {
  std::shared_ptr<scorpio_utils::threading::ThreadPool> pool =
    std::make_shared<scorpio_utils::threading::ThreadPool>(2);

  auto future = pool->async([]() -> scorpio_utils::testing::LifetimeHelper {
        return scorpio_utils::testing::LifetimeHelper();
  });

  auto moved_future = std::move(*future).and_map([](
        scorpio_utils::testing::LifetimeHelper&& value) -> scorpio_utils::testing::LifetimeHelper {
        return std::move(value);
  }, pool);

  for (size_t i = 1; i < moved_future->await().get_value_event_log().size(); ++i) {
    EXPECT_NE(moved_future->await().get_value_event_log()[i], scorpio_utils::testing::LifetimeHelper::EventType::COPY);
    EXPECT_NE(
      moved_future->await().get_value_event_log()[i], scorpio_utils::testing::LifetimeHelper::EventType::COPY_ASSIGN);
  }
  EXPECT_ANY_THROW(future->and_map([](auto) { }, pool));
  EXPECT_ANY_THROW(std::move(*future).and_map([](auto) { }, pool));
  EXPECT_ANY_THROW(std::move(*future).and_map([](auto) { }, pool));
}
