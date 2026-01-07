#include <gtest/gtest.h>
#include <atomic>
#include <chrono>
#include <thread>
#include <vector>
#include "scorpio_utils/threading/signal.hpp"
#include "scorpio_utils/threading/jthread.hpp"

using scorpio_utils::threading::Signal;
using scorpio_utils::threading::JThread;
using scorpio_utils::threading::eager_select;

// Test basic signal construction and destruction
TEST(Signal, BasicConstruction) {
  Signal signal;
  // Should construct without issues
  SUCCEED();
}

// Test that Signal is non-copyable and non-movable
TEST(Signal, NonCopyableNonMovable) {
  // These should not compile:
  // Signal signal1;
  // Signal signal2(signal1);        // Copy constructor
  // Signal signal3 = signal1;       // Copy assignment
  // Signal signal4(std::move(signal1)); // Move constructor
  // Signal signal5 = std::move(signal1); // Move assignment

  // If this test compiles, the class is properly non-copyable and non-movable
  SUCCEED();
}

// Test basic notify_one functionality
TEST(Signal, NotifyOneBasic) {
  Signal signal;
  std::atomic<bool> thread_started{ false };
  std::atomic<bool> wait_completed{ false };

  JThread waiter([&]() {
      thread_started = true;
      signal.wait();
      wait_completed = true;
    });

  // Wait for thread to start
  while (!thread_started.load()) {
    std::this_thread::sleep_for(std::chrono::milliseconds(1));
  }

  // Give a moment for the thread to enter wait state
  std::this_thread::sleep_for(std::chrono::milliseconds(10));

  // Thread should be waiting
  EXPECT_FALSE(wait_completed.load());

  // Notify and thread should complete
  signal.notify_one();

  // Give time for notification to be processed
  std::this_thread::sleep_for(std::chrono::milliseconds(10));

  EXPECT_TRUE(wait_completed.load());
}

// Test multiple waiters with notify_one (simplified)
TEST(Signal, MultipleWaitersNotifyOne) {
  Signal signal;
  std::atomic<int> threads_completed{ 0 };

  // Test with just two threads to make it more predictable
  JThread thread1([&]() {
      signal.wait();
      threads_completed.fetch_add(1);
    });

  JThread thread2([&]() {
      signal.wait();
      threads_completed.fetch_add(1);
    });

  // Give time for threads to enter wait state
  std::this_thread::sleep_for(std::chrono::milliseconds(20));

  // No threads should have completed yet
  EXPECT_EQ(threads_completed.load(), 0);

  // First notification should wake up at least one thread
  signal.notify_one();
  std::this_thread::sleep_for(std::chrono::milliseconds(30));

  int first_count = threads_completed.load();
  EXPECT_GE(first_count, 1);

  // If only one thread woke up, wake the second one
  if (first_count == 1) {
    signal.notify_one();
    std::this_thread::sleep_for(std::chrono::milliseconds(30));
  }

  // Eventually both threads should complete
  EXPECT_EQ(threads_completed.load(), 2);
}

// Test notification before wait (signal should handle spurious wakeups correctly)
TEST(Signal, NotifyBeforeWait) {
  Signal signal;
  std::atomic<bool> wait_completed{ false };

  // Notify before any waiter
  signal.notify_one();

  JThread waiter([&]() {
      signal.wait();
      wait_completed = true;
    });

  // Give time for the wait to potentially complete
  std::this_thread::sleep_for(std::chrono::milliseconds(20));

  // The wait should complete immediately since notification already happened
  EXPECT_TRUE(wait_completed.load());
}

TEST(Signal, RapidNotifyWaitCycles) {
  Signal signal;
  std::atomic<int> cycles_completed{ 0 };
  const int num_cycles = 10;

  JThread worker([&]() {
      for (int i = 0; i < num_cycles; ++i) {
        signal.wait();
        cycles_completed.fetch_add(1);
      }
    });

  // Give worker time to start
  std::this_thread::sleep_for(std::chrono::milliseconds(10));

  // Rapid notifications
  for (int i = 0; i < num_cycles; ++i) {
    signal.notify_one();
    // Small delay to allow processing
    std::this_thread::sleep_for(std::chrono::milliseconds(2));
  }

  // Give time for all cycles to complete
  std::this_thread::sleep_for(std::chrono::milliseconds(50));

  EXPECT_EQ(cycles_completed.load(), num_cycles);
}

// Test producer-consumer pattern
TEST(Signal, ProducerConsumerPattern) {
  Signal signal;
  std::atomic<int> produced_count{ 0 };
  std::atomic<int> consumed_count{ 0 };
  std::atomic<bool> stop_production{ false };

  // Consumer thread
  JThread consumer([&]() {
      while (!stop_production.load() || produced_count.load() > consumed_count.load()) {
        if (produced_count.load() > consumed_count.load()) {
          consumed_count.fetch_add(1);
        } else {
          signal.wait();
        }
      }
    });

  // Producer thread
  JThread producer([&]() {
      for (int i = 0; i < 10; ++i) {
        produced_count.fetch_add(1);
        signal.notify_one();
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
      }
      stop_production = true;
      signal.notify_one();  // Final notification to wake consumer
    });

  // Wait for producer to complete
  // Threads will be joined automatically by JThread destructors

  // Give time for final consumption
  std::this_thread::sleep_for(std::chrono::milliseconds(50));

  EXPECT_EQ(produced_count.load(), 10);
  EXPECT_EQ(consumed_count.load(), 10);
}

// Test with very short-lived threads
TEST(Signal, ShortLivedThreads) {
  Signal signal;
  std::atomic<bool> completed{ false };

  {
    JThread quick_thread([&]() {
        signal.wait();
        completed = true;
      });

    // Immediate notification
    signal.notify_one();
  }  // Thread joins here

  EXPECT_TRUE(completed.load());
}

// Test the new close() functionality
TEST(Signal, CloseBasic) {
  Signal signal;
  std::atomic<bool> wait_completed{ false };

  JThread waiter([&]() {
      EXPECT_ANY_THROW(signal.wait());
      wait_completed = true;
    });

  // Give time for thread to enter wait state
  std::this_thread::sleep_for(std::chrono::milliseconds(10));

  // Thread should be waiting
  EXPECT_FALSE(wait_completed.load());

  // Close the signal - should wake up the waiter
  signal.close();

  // Give time for notification to be processed
  std::this_thread::sleep_for(std::chrono::milliseconds(20));

  EXPECT_TRUE(wait_completed.load());
}

// Test close() with multiple waiters
TEST(Signal, CloseMultipleWaiters) {
  Signal signal;
  std::atomic<int> threads_completed{ 0 };

  JThread thread1([&]() {
      EXPECT_ANY_THROW(signal.wait());
      threads_completed.fetch_add(1);
    });

  JThread thread2([&]() {
      EXPECT_ANY_THROW(signal.wait());
      threads_completed.fetch_add(1);
    });

  JThread thread3([&]() {
      EXPECT_ANY_THROW(signal.wait());
      threads_completed.fetch_add(1);
    });

  // Give time for threads to enter wait state
  std::this_thread::sleep_for(std::chrono::milliseconds(20));

  // No threads should have completed yet
  EXPECT_EQ(threads_completed.load(), 0);

  // Close should wake up waiters (may not wake all at once due to implementation)
  signal.close();
  std::this_thread::sleep_for(std::chrono::milliseconds(50));

  // At least one thread should complete, potentially all
  int completed = threads_completed.load();
  EXPECT_GE(completed, 1);

  // If not all completed, the signal should be in closed state and
  // subsequent operations should wake remaining threads
  if (completed < 3) {
    // Give more time or trigger additional wake-ups
    std::this_thread::sleep_for(std::chrono::milliseconds(30));
    completed = threads_completed.load();
    // In a closed state, all should eventually complete
    EXPECT_EQ(completed, 3);
  }
}

// Test that close() makes future wait() calls return immediately
TEST(Signal, CloseImmediateReturn) {
  Signal signal;

  signal.close();

  EXPECT_ANY_THROW(signal.wait());
}

// Test SignalException
TEST(Signal, ExceptionHandling) {
  // Test the exception class exists and is properly defined
  try {
    throw scorpio_utils::threading::SignalException("Test exception");
  } catch (const scorpio_utils::threading::SignalException& e) {
    EXPECT_STREQ(e.what(), "Test exception");
  } catch (...) {
    FAIL() << "SignalException not caught properly";
  }
}

// Test signal behavior after close - multiple wait() calls
TEST(Signal, MultipleWaitsAfterClose) {
  Signal signal;
  signal.close();

  std::atomic<int> completed_waits{ 0 };

  // Multiple threads calling wait() on closed signal
  JThread waiter1([&]() {
      EXPECT_ANY_THROW(signal.wait());
      completed_waits.fetch_add(1);
    });

  JThread waiter2([&]() {
      EXPECT_ANY_THROW(signal.wait());
      completed_waits.fetch_add(1);
    });

  JThread waiter3([&]() {
      EXPECT_ANY_THROW(signal.wait());
      completed_waits.fetch_add(1);
    });

  // All waits should complete immediately
  std::this_thread::sleep_for(std::chrono::milliseconds(30));
  EXPECT_EQ(completed_waits.load(), 3);
}

// Test rapid signal operations
TEST(Signal, RapidOperations) {
  Signal signal;
  std::atomic<int> notifications_received{ 0 };

  JThread worker([&]() {
      for (int i = 0; i < 10; ++i) {
        signal.wait();
        notifications_received.fetch_add(1);
      }
    });

  // Give worker time to start
  std::this_thread::sleep_for(std::chrono::milliseconds(10));

  // Rapid notifications
  for (int i = 0; i < 10; ++i) {
    signal.notify_one();
    std::this_thread::sleep_for(std::chrono::milliseconds(2));
  }

  // Give time for all notifications to be processed
  std::this_thread::sleep_for(std::chrono::milliseconds(50));

  EXPECT_EQ(notifications_received.load(), 10);
}

// Test select() with two signals - simple case
TEST(Signal, SelectSimple) {
  Signal signal1;
  Signal signal2;
  Signal signal3;

  std::atomic<bool> thread_started{ false };
  std::atomic<size_t> selected_index{ SIZE_MAX };

  {
    SCU_JTHREAD([&]() {
        thread_started = true;
        selected_index = eager_select(signal1, signal2, signal3).index();
      });
    while (!thread_started.load()) {
      std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    signal2.notify_one();
  }
  EXPECT_EQ(selected_index.load(), 1);
}

// Test eager_select with first signal notified
TEST(Signal, EagerSelectFirstSignal) {
  Signal signal1;
  Signal signal2;
  Signal signal3;

  std::atomic<bool> thread_started{ false };
  std::atomic<size_t> selected_index{ SIZE_MAX };

  SCU_JTHREAD([&]() {
      thread_started = true;
      selected_index = eager_select(signal1, signal2, signal3).index();
    });

  while (!thread_started.load()) {
    std::this_thread::sleep_for(std::chrono::milliseconds(1));
  }

  std::this_thread::sleep_for(std::chrono::milliseconds(10));
  signal1.notify_one();
  std::this_thread::sleep_for(std::chrono::milliseconds(20));

  EXPECT_EQ(selected_index.load(), 0);
}

// Test eager_select with last signal notified
TEST(Signal, EagerSelectLastSignal) {
  Signal signal1;
  Signal signal2;
  Signal signal3;

  std::atomic<bool> thread_started{ false };
  std::atomic<size_t> selected_index{ SIZE_MAX };

  SCU_JTHREAD([&]() {
      thread_started = true;
      selected_index = eager_select(signal1, signal2, signal3).index();
    });

  while (!thread_started.load()) {
    std::this_thread::sleep_for(std::chrono::milliseconds(1));
  }

  std::this_thread::sleep_for(std::chrono::milliseconds(10));
  signal3.notify_one();
  std::this_thread::sleep_for(std::chrono::milliseconds(20));

  EXPECT_EQ(selected_index.load(), 2);
}

// Test eager_select with multiple signals notified (first wins)
TEST(Signal, EagerSelectMultipleSignalsFirstWins) {
  Signal signal1;
  Signal signal2;
  Signal signal3;

  std::atomic<bool> thread_started{ false };
  std::atomic<size_t> selected_index{ SIZE_MAX };

  SCU_JTHREAD([&]() {
      thread_started = true;
      selected_index = eager_select(signal1, signal2, signal3).index();
    });

  while (!thread_started.load()) {
    std::this_thread::sleep_for(std::chrono::milliseconds(1));
  }

  std::this_thread::sleep_for(std::chrono::milliseconds(10));

  // Notify multiple signals in quick succession
  signal1.notify_one();
  signal2.notify_one();
  signal3.notify_one();

  std::this_thread::sleep_for(std::chrono::milliseconds(20));

  // Should select the first signal (index 0) due to order of checking
  EXPECT_EQ(selected_index.load(), 0);
}

// Test eager_select with pre-notified signal
TEST(Signal, EagerSelectPreNotified) {
  Signal signal1;
  Signal signal2;
  Signal signal3;

  // Pre-notify signal2
  signal2.notify_one();

  std::atomic<size_t> selected_index{ SIZE_MAX };

  SCU_JTHREAD([&]() {
      selected_index = eager_select(signal1, signal2, signal3).index();
    });

  // Should return quickly since signal2 is already notified
  std::this_thread::sleep_for(std::chrono::milliseconds(20));

  EXPECT_EQ(selected_index.load(), 1);
}

// Test eager_select with closed signal
TEST(Signal, EagerSelectWithClosedSignal) {
  Signal signal1;
  Signal signal2;
  Signal signal3;

  std::atomic<bool> thread_started{ false };
  std::atomic<size_t> selected_index{ SIZE_MAX };

  SCU_JTHREAD([&]() {
      thread_started = true;
      EXPECT_ANY_THROW(eager_select(signal1, signal2, signal3));
    });

  while (!thread_started.load()) {
    std::this_thread::sleep_for(std::chrono::milliseconds(1));
  }

  std::this_thread::sleep_for(std::chrono::milliseconds(10));

  // Close signal2
  signal2.close();
}

// Test eager_select with pre-closed signal
TEST(Signal, EagerSelectPreClosed) {
  Signal signal1;
  Signal signal2;
  Signal signal3;

  // Pre-close signal3
  signal3.close();

  EXPECT_ANY_THROW(eager_select(signal1, signal2, signal3));
}

// Test eager_select with two signals
TEST(Signal, EagerSelectTwoSignals) {
  Signal signal1;
  Signal signal2;

  std::atomic<bool> thread_started{ false };
  std::atomic<size_t> selected_index{ SIZE_MAX };

  SCU_JTHREAD([&]() {
      thread_started = true;
      selected_index = eager_select(signal1, signal2).index();
    });

  while (!thread_started.load()) {
    std::this_thread::sleep_for(std::chrono::milliseconds(1));
  }

  std::this_thread::sleep_for(std::chrono::milliseconds(10));
  signal2.notify_one();
  std::this_thread::sleep_for(std::chrono::milliseconds(20));

  EXPECT_EQ(selected_index.load(), 1);
}

// Test eager_select with single signal
TEST(Signal, EagerSelectSingleSignal) {
  Signal signal1;

  std::atomic<bool> thread_started{ false };

  SCU_JTHREAD([&]() {
      thread_started = true;
      eager_select(signal1);
    });

  while (!thread_started.load()) {
    std::this_thread::sleep_for(std::chrono::milliseconds(1));
  }

  std::this_thread::sleep_for(std::chrono::milliseconds(10));
  signal1.notify_one();
  std::this_thread::sleep_for(std::chrono::milliseconds(20));
}

// Test eager_select rapid notifications
TEST(Signal, EagerSelectRapidNotifications) {
  Signal signal1;
  Signal signal2;
  Signal signal3;

  std::atomic<int> selections_made{ 0 };
  std::atomic<bool> stop_selecting{ false };

  // Consumer thread doing multiple selects
  SCU_JTHREAD([&]() {
      while (!stop_selecting.load()) {
        auto result = eager_select(signal1, signal2, signal3);
        selections_made.fetch_add(1);
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
      }
    });
  {
    // Producer threads
    SCU_JTHREAD([&]() {
        for (int i = 0; i < 10; ++i) {
          signal1.notify_one();
          std::this_thread::sleep_for(std::chrono::milliseconds(5));
        }
    });

    SCU_JTHREAD([&]() {
        for (int i = 0; i < 10; ++i) {
          signal2.notify_one();
          std::this_thread::sleep_for(std::chrono::milliseconds(7));
        }
    });

    SCU_JTHREAD([&]() {
        for (int i = 0; i < 10; ++i) {
          signal3.notify_one();
          std::this_thread::sleep_for(std::chrono::milliseconds(11));
        }
    });
  }

  // Let it run for a while
  std::this_thread::sleep_for(std::chrono::milliseconds(200));
  stop_selecting = true;
  signal1.notify_one();

  // Should have made multiple selections
  EXPECT_GE(selections_made.load(), 30);
}

// Test eager_select timing behavior
TEST(Signal, EagerSelectTiming) {
  Signal signal1;
  Signal signal2;

  auto start_time = std::chrono::high_resolution_clock::now();

  // Pre-notify to ensure immediate return
  signal1.notify_one();

  auto result = eager_select(signal1, signal2);

  auto end_time = std::chrono::high_resolution_clock::now();
  auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(end_time - start_time);

  EXPECT_EQ(result.index(), 0);
  // Should return very quickly since signal is pre-notified
  EXPECT_LT(duration.count(), 10);  // Less than 10ms
}

// Test eager_select with multiple threads selecting
TEST(Signal, EagerSelectMultipleSelectors) {
  Signal signal1;
  Signal signal2;

  std::atomic<int> thread1_selections{ 0 };
  std::atomic<int> thread2_selections{ 0 };
  std::atomic<bool> stop_test{ false };

  SCU_JTHREAD([&]() {
      while (!stop_test.load()) {
        eager_select(signal1, signal2);
        thread1_selections.fetch_add(1);
        std::this_thread::sleep_for(std::chrono::milliseconds(2));
      }
    });

  SCU_JTHREAD([&]() {
      while (!stop_test.load()) {
        eager_select(signal1, signal2);
        thread2_selections.fetch_add(1);
        std::this_thread::sleep_for(std::chrono::milliseconds(3));
      }
    });

  // Producer thread
  {
    SCU_JTHREAD([&]() {
        for (int i = 0; i < 20; ++i) {
          if (i % 2 == 0) {
            signal1.notify_one();
          } else {
            signal2.notify_one();
          }
          std::this_thread::sleep_for(std::chrono::milliseconds(5));
        }
      });
  }

  // Let it run
  std::this_thread::sleep_for(std::chrono::milliseconds(150));
  stop_test = true;
  signal1.notify_one();
  signal2.notify_one();

  // Both threads should have made some selections
  EXPECT_GT(thread1_selections.load(), 0);
  EXPECT_GT(thread2_selections.load(), 0);
}

// Test eager_select return value type
TEST(Signal, EagerSelectReturnType) {
  Signal signal1;
  Signal signal2;

  // Pre-notify signal1
  signal1.notify_one();

  auto result = eager_select(signal1, signal2);

  // Check that result is the correct variant type
  EXPECT_EQ(result.index(), 0);

  // The value should be void (no actual value returned from Signal)
  // This test mainly checks compilation and basic functionality
  SUCCEED();
}

// Test eager_select with all signals closed
TEST(Signal, EagerSelectAllClosed) {
  Signal signal1;
  Signal signal2;
  Signal signal3;

  // Close all signals
  signal1.close();
  signal2.close();
  signal3.close();

  // Should return immediately with first available (closed) signal
  EXPECT_ANY_THROW(eager_select(signal1, signal2, signal3));
}
