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
#include "scorpio_utils/threading/thread_pool.hpp"
#include "scorpio_utils/threading/wait_group.hpp"

TEST(WaitGroup, baseUsage) {
  scorpio_utils::threading::ThreadPool pool(4);
  scorpio_utils::threading::WaitGroup wg;

  auto future1 = pool.async([]() { std::this_thread::sleep_for(std::chrono::microseconds(100)); });
  future1->await();
  auto future2 = pool.async([]() { std::this_thread::sleep_for(std::chrono::microseconds(200)); });
  auto future3 = pool.async([]() { std::this_thread::sleep_for(std::chrono::microseconds(300)); });
  wg.add(future1);
  EXPECT_EQ(wg.count(), 0);
  wg << future2 << future3;
  EXPECT_EQ(wg.count(), 2);
  wg.wait();
  EXPECT_TRUE(future3->is_completed());
  EXPECT_TRUE(future2->is_completed());
  EXPECT_TRUE(future1->is_completed());
  auto future4 = pool.async([]() { });
  EXPECT_EQ(wg.count(), 0);
  wg.close();
  EXPECT_ANY_THROW(wg.add(future4));
  EXPECT_EQ(wg.count(), 0);
}

TEST(WaitGroup, copyConstructor) {
  scorpio_utils::threading::ThreadPool pool(4);
  scorpio_utils::threading::WaitGroup wg1;

  auto future1 = pool.async([]() { std::this_thread::sleep_for(std::chrono::milliseconds(100)); });
  wg1.add(future1);
  EXPECT_EQ(wg1.count(), 1);

  scorpio_utils::threading::WaitGroup wg2(wg1);
  EXPECT_EQ(wg1.count(), 1);
  EXPECT_EQ(wg2.count(), 1);

  wg2.wait();
  EXPECT_TRUE(future1->is_completed());
}

TEST(WaitGroup, moveConstructor) {
  scorpio_utils::threading::ThreadPool pool(4);
  scorpio_utils::threading::WaitGroup wg1;

  auto future1 = pool.async([]() { std::this_thread::sleep_for(std::chrono::milliseconds(100)); });
  wg1.add(future1);
  EXPECT_EQ(wg1.count(), 1);

  std::thread t([wg1]() {
      wg1.wait();
    });

  scorpio_utils::threading::WaitGroup wg2(std::move(wg1));
  EXPECT_EQ(wg1.count(), 0);
  EXPECT_EQ(wg2.count(), 1);

  wg2.wait();
  EXPECT_TRUE(future1->is_completed());
  t.join();  // Thread shall complete
}

TEST(WaitGroup, andMap) {
  scorpio_utils::threading::ThreadPool pool(4);
  std::shared_ptr<scorpio_utils::threading::ThreadPool> pool_ptr(&pool, [](auto) { });
  scorpio_utils::threading::WaitGroup wg;

  auto future1 = pool.async([]() { std::this_thread::sleep_for(std::chrono::microseconds(100)); });
  future1->await();
  auto future2 = pool.async([]() { std::this_thread::sleep_for(std::chrono::microseconds(200)); });
  auto future3 = pool.async([]() { std::this_thread::sleep_for(std::chrono::microseconds(300)); });
  wg.add(future1);
  EXPECT_EQ(wg.count(), 0);
  wg << future2 << future3;
  EXPECT_EQ(wg.count(), 2);
  wg.close();
  wg.and_map([]() { }, pool_ptr)->await();
  wg.wait();
  std::move(wg).and_map([]() { }, pool_ptr)->await();
  EXPECT_TRUE(future3->is_completed());
  EXPECT_TRUE(future2->is_completed());
  EXPECT_TRUE(future1->is_completed());
  auto future4 = pool.async([]() { });
  EXPECT_EQ(wg.count(), 0);
  EXPECT_ANY_THROW(wg.add(future4));
  EXPECT_EQ(wg.count(), 0);
}
