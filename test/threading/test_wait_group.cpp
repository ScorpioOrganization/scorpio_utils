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
