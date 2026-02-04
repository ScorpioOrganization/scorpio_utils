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
#include <atomic>
#include <thread>
#define SCU_THREADING_THREAD_POOL_SINGLETON
#include "scorpio_utils/threading/thread_pool.hpp"

TEST(ThreadPool, BasicFunctionality) {
  scorpio_utils::threading::ThreadPool pool(4);
  std::atomic<int> result = 0;

  auto future = pool.async([&result]() {
        result = 42;
  });

  future->await();
  EXPECT_EQ(result.load(), 42);
}

TEST(ThreadPool, MultipleTasks) {
  scorpio_utils::threading::ThreadPool pool(4);
  std::atomic<int> sum = 0;

  auto future1 = pool.async([&sum]() {
        sum += 10;
  });
  auto future2 = pool.async([&sum]() {
        sum += 20;
  });

  future1->await();
  future2->await();

  EXPECT_EQ(sum, 30);
}

TEST(ThreadPool, AsyncReturnValue) {
scorpio_utils::threading::ThreadPool pool(4);

auto future = pool.async([]() -> int {
        return 100;
});

EXPECT_EQ(future->await(), 100);
}

TEST(ThreadPool, ForEachRange) {
  scorpio_utils::threading::ThreadPool pool(4);
  std::atomic<int> sum = 0;

  auto wait_group = pool.for_each_range(1, 11, [&sum](int i) {
        sum += i;
  });

  wait_group->wait();
  EXPECT_EQ(sum, 55);
}

TEST(ThreadPool, ForEachRangeBad) {
  scorpio_utils::threading::ThreadPool pool(4);
  std::atomic<int> sum = 0;

  auto wait_group = pool.for_each_range(11, 11, [&sum](int i) {
        sum += i;
  });

  ASSERT_EQ(wait_group->count(), 0);
  wait_group->wait();
  EXPECT_EQ(sum, 0);
}

TEST(ThreadPool, ForEachContainer) {
  scorpio_utils::threading::ThreadPool pool(4);
  std::vector<int> numbers = { 1, 2, 3, 4 };
  std::atomic<int> sum = 0;

  auto wait_group = pool.for_each(numbers.begin(), numbers.end(), [&sum](int i) {
        sum += i;
  });

  wait_group->wait();
  EXPECT_EQ(sum, 10);
}

TEST(ThreadPool, ThreadCount) {
  scorpio_utils::threading::ThreadPool pool(0);
  EXPECT_EQ(pool.threads_count(), 0);
  EXPECT_EQ(pool.spawned_threads_count(), 0);

  pool.set_threads_count(4);
  EXPECT_EQ(pool.threads_count(), 4);
  EXPECT_EQ(pool.spawned_threads_count(), 4);

  pool.set_threads_count(2, true);
  EXPECT_EQ(pool.threads_count(), 2);
  EXPECT_EQ(pool.spawned_threads_count(), 2);
}

TEST(ThreadPool, TaskCount) {
  // To ensure the task count is accurate since task_count returns the number of tasks in the queue
  scorpio_utils::threading::ThreadPool pool(0);
  EXPECT_EQ(pool.tasks_count(), 0);

  pool.add_task([]() { });
  EXPECT_EQ(pool.tasks_count(), 1);

  pool.add_task([]() { });
  EXPECT_EQ(pool.tasks_count(), 2);
}

TEST(ThreadPool, ThreadSuicide) {
  scorpio_utils::threading::ThreadPool pool(4);
  EXPECT_EQ(pool.threads_count(), 4);
  EXPECT_EQ(pool.spawned_threads_count(), 4);

  pool.set_threads_count(0, true);
  EXPECT_EQ(pool.threads_count(), 0);
  EXPECT_EQ(pool.spawned_threads_count(), 0);
}

TEST(ThreadPool, AddThreads) {
  scorpio_utils::threading::ThreadPool pool(2);
  EXPECT_EQ(pool.threads_count(), 2);
  EXPECT_EQ(pool.spawned_threads_count(), 2);

  pool.set_threads_count(4);
  EXPECT_EQ(pool.threads_count(), 4);
  EXPECT_EQ(pool.spawned_threads_count(), 4);

  pool.set_threads_count(1, true);
  EXPECT_EQ(pool.threads_count(), 1);
  EXPECT_EQ(pool.spawned_threads_count(), 1);
}

TEST(ThreadPool, Work) {
  scorpio_utils::threading::ThreadPool pool(0);
  std::atomic<int> count = 0;

  pool.add_task([&count]() {
      count++;
  });

  pool.add_task([&count]() {
      count++;
  });

  EXPECT_EQ(count.load(), 0);
  EXPECT_EQ(pool.tasks_count(), 2);
  EXPECT_EQ(pool.threads_count(), 0);
  EXPECT_EQ(pool.spawned_threads_count(), 0);

  pool.work();
  EXPECT_EQ(count.load(), 2);
}
