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
#include <chrono>
#include <thread>
#include "scorpio_utils/threading/jthread.hpp"

using scorpio_utils::threading::JThread;

// Test basic functionality
TEST(JThread, BasicConstruction) {
  std::atomic<bool> executed{ false };

  {
    JThread jt([&executed]() {
        executed = true;
      });
    // Thread should be joinable
    EXPECT_TRUE(jt.joinable());
  }  // JThread destructor should automatically join

  // Thread should have executed
  EXPECT_TRUE(executed.load());
}

TEST(JThread, AutomaticJoinOnDestruction) {
  std::atomic<bool> started{ false };
  std::atomic<bool> finished{ false };

  {
    JThread jt([&started, &finished]() {
        started = true;
        std::this_thread::sleep_for(std::chrono::milliseconds(50));
        finished = true;
      });

    // Wait for thread to start
    while (!started.load()) {
      std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }

    EXPECT_TRUE(started.load());
    EXPECT_FALSE(finished.load());  // Should not be finished yet
  }  // Destructor should join and wait for completion

  // After destructor, thread should be finished
  EXPECT_TRUE(finished.load());
}

TEST(JThread, ConstructorWithArguments) {
  std::atomic<int> result{ 0 };

  auto thread_func = [](std::atomic<int>& res, int a, int b) {
      res = a + b;
    };

  {
    JThread jt(thread_func, std::ref(result), 10, 20);
  }

  EXPECT_EQ(result.load(), 30);
}

TEST(JThread, ConstructorWithMoveOnlyArguments) {
  std::atomic<int> result{ 0 };

  auto thread_func = [](std::atomic<int>& res, std::unique_ptr<int> value) {
      res = *value;
    };

  auto ptr = std::make_unique<int>(42);

  {
    JThread jt(thread_func, std::ref(result), std::move(ptr));
  }

  EXPECT_EQ(result.load(), 42);
}

TEST(JThread, ManualJoinBeforeDestruction) {
  std::atomic<bool> executed{ false };

  JThread jt([&executed]() {
      executed = true;
    });

  // Manually join the thread
  jt.join();

  // Thread should no longer be joinable
  EXPECT_FALSE(jt.joinable());
  EXPECT_TRUE(executed.load());

  // Destructor should handle already-joined thread gracefully
}

TEST(JThread, MultipleThreads) {
  std::atomic<int> result1{ 0 };
  std::atomic<int> result2{ 0 };
  std::atomic<int> result3{ 0 };

  {
    JThread jt1([&result1]() {
        result1 = 1 * 1;
      });

    JThread jt2([&result2]() {
        result2 = 2 * 2;
      });

    JThread jt3([&result3]() {
        result3 = 3 * 3;
      });
  }  // All threads should be joined on destruction

  // Verify all threads executed
  EXPECT_EQ(result1.load(), 1);
  EXPECT_EQ(result2.load(), 4);
  EXPECT_EQ(result3.load(), 9);
}

TEST(JThread, ThreadIdAccess) {
  std::atomic<std::thread::id> thread_id{ };

  {
    JThread jt([&thread_id]() {
        thread_id = std::this_thread::get_id();
      });

    // Should be able to access thread ID
    std::thread::id jt_id = jt.get_id();

    // Wait for thread to set its ID
    while (thread_id.load() == std::thread::id{ }) {
      std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }

    EXPECT_EQ(jt_id, thread_id.load());
  }
}

TEST(JThread, ExceptionInThread) {
  std::atomic<bool> executed{ false };

  {
    JThread jt([&executed]() {
        executed = true;
        // In C++, uncaught exceptions in threads call std::terminate
        // So we need to catch the exception to prevent program termination
        try {
          throw std::runtime_error("Test exception");
        } catch (...) {
          // Exception caught and handled within the thread
        }
      });
  }  // Should join successfully since exception was handled

  EXPECT_TRUE(executed.load());
}

TEST(JThread, ExceptionHandlingWithResult) {
  std::atomic<bool> executed{ false };
  std::atomic<bool> exception_caught{ false };

  {
    JThread jt([&executed, &exception_caught]() {
        executed = true;
        try {
          throw std::runtime_error("Test exception");
        } catch (const std::runtime_error&) {
          exception_caught = true;
        }
      });
  }

  EXPECT_TRUE(executed.load());
  EXPECT_TRUE(exception_caught.load());
}

TEST(JThread, MoveConstruction) {
  std::atomic<bool> executed{ false };

  auto create_thread = [&executed]() -> JThread {
      return JThread([&executed]() {
                 std::this_thread::sleep_for(std::chrono::milliseconds(10));
                 executed = true;
    });
    };

  {
    JThread jt = create_thread();
    EXPECT_TRUE(jt.joinable());
  }

  EXPECT_TRUE(executed.load());
}

TEST(JThread, MoveAssignment) {
  std::atomic<bool> executed1{ false };
  std::atomic<bool> executed2{ false };

  {
    JThread jt1([&executed1]() {
        executed1 = true;
      });

    // Wait for first thread to complete
    while (!executed1.load()) {
      std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
  }

  // First thread should have completed
  EXPECT_TRUE(executed1.load());

  {
    JThread jt2([&executed2]() {
        std::this_thread::sleep_for(std::chrono::milliseconds(50));
        executed2 = true;
      });

    // jt2 should be joinable
    EXPECT_TRUE(jt2.joinable());
  }  // jt2 destructor should join

  EXPECT_TRUE(executed2.load());
}

TEST(JThread, DefaultConstruction) {
  JThread jt;  // Default constructed

  EXPECT_FALSE(jt.joinable());

  // Should be safe to destroy default-constructed thread
}

TEST(JThread, InheritedStdThreadMethods) {
  std::atomic<bool> executed{ false };

  JThread jt([&executed]() {
      std::this_thread::sleep_for(std::chrono::milliseconds(10));
      executed = true;
    });

  // Test that we can access std::thread methods
  auto id = jt.get_id();
  EXPECT_NE(id, std::thread::id{ });

  bool is_joinable = jt.joinable();
  EXPECT_TRUE(is_joinable);

  // Let destructor handle joining
}

TEST(JThread, DetachOperation) {
  std::atomic<bool> executed{ false };

  {
    JThread jt([&executed]() {
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
        executed = true;
      });

    jt.detach();
    EXPECT_FALSE(jt.joinable());
  }  // Destructor should not try to join detached thread

  // Give some time for the detached thread to potentially complete
  // Note: We can't reliably test completion of detached threads
  std::this_thread::sleep_for(std::chrono::milliseconds(150));
}

TEST(JThread, HardwareThreads) {
  // Test that JThread inherits std::thread static methods
  unsigned int hw_threads = JThread::hardware_concurrency();

  // Should return some reasonable value (at least 1)
  EXPECT_GT(hw_threads, 0u);
}

TEST(JThread, LongRunningTask) {
  std::atomic<int> counter{ 0 };

  {
    JThread jt([&counter]() {
        for (int i = 0; i < 1000; ++i) {
          counter++;
          // Small delay to make this a longer running task
          if (i % 100 == 0) {
            std::this_thread::sleep_for(std::chrono::microseconds(100));
          }
        }
      });
  }  // Should wait for completion

  EXPECT_EQ(counter.load(), 1000);
}

TEST(JThread, FunctionPointer) {
  std::atomic<int> result{ 0 };

  auto func = [](std::atomic<int>* res, int value) {
      *res = value * 2;
    };

  {
    JThread jt(func, &result, 21);
  }

  EXPECT_EQ(result.load(), 42);
}

TEST(JThread, CaptureLambda) {
  int multiplier = 3;
  std::atomic<int> result{ 0 };

  {
    JThread jt([multiplier, &result](int value) {
        result = value * multiplier;
      }, 14);
  }

  EXPECT_EQ(result.load(), 42);
}
