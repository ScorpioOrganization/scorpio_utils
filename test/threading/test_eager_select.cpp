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
#include <memory>
#include <thread>
#include <vector>
#include "scorpio_utils/threading/eager_select.hpp"
#include "scorpio_utils/threading/signal.hpp"
#include "scorpio_utils/threading/jthread.hpp"
#include "scorpio_utils/time_provider/system_time_provider.hpp"

using scorpio_utils::threading::eager_select;
using scorpio_utils::threading::Signal;
using scorpio_utils::threading::JThread;
using scorpio_utils::threading::EagerSelectTimeout;
using scorpio_utils::time_provider::SystemTimeProvider;
using scorpio_utils::time_provider::TimeProvider;

// Mock TimeProvider for testing
class MockTimeProvider : public TimeProvider {
  std::atomic<int64_t> _current_time{ 0 };

public:
  int64_t get_time() const override {
    return _current_time.load();
  }

  void advance_time(int64_t nanos) {
    _current_time.fetch_add(nanos);
  }

  void set_time(int64_t nanos) {
    _current_time.store(nanos);
  }
};

// Custom signal-like class for testing trait checking
struct CustomSignal {
  std::atomic<bool> ready{ false };
  int value{ 42 };

  bool SCU_EAGER_SELECT_IS_READY() {
    return ready.load();
  }

  int SCU_EAGER_SELECT_GET_VALUE() {
    return value;
  }
};

// Custom signal returning different type
struct CustomSignal2 {
  std::atomic<bool> ready{ false };
  int64_t value{ 100 };

  bool SCU_EAGER_SELECT_IS_READY() {
    return ready.load();
  }

  int64_t SCU_EAGER_SELECT_GET_VALUE() {
    return value;
  }
};

// Custom signal returning void
struct CustomSignalVoid {
  std::atomic<bool> ready{ false };

  bool SCU_EAGER_SELECT_IS_READY() {
    return ready.load();
  }

  void SCU_EAGER_SELECT_GET_VALUE() {
    // Returns void
  }
};

// ============================================================================
// EagerSelectTimeout Tests
// ============================================================================

TEST(EagerSelectTimeout, Construction) {
  auto time_provider = std::make_shared<MockTimeProvider>();
  EagerSelectTimeout<MockTimeProvider> timeout(1000000, time_provider);

  EXPECT_EQ(timeout.get_timeout(), 1000000);
  EXPECT_FALSE(timeout.is_started());
  EXPECT_FALSE(timeout.is_elapsed());
}

TEST(EagerSelectTimeout, StartAndElapsed) {
  auto time_provider = std::make_shared<MockTimeProvider>();
  EagerSelectTimeout<MockTimeProvider> timeout(1000000, time_provider);

  // Start the timeout
  timeout.start();
  EXPECT_TRUE(timeout.is_started());
  EXPECT_FALSE(timeout.is_elapsed());

  // Advance time but not enough
  time_provider->advance_time(500000);
  EXPECT_FALSE(timeout.is_elapsed());

  // Advance time to trigger timeout
  time_provider->advance_time(500000);
  EXPECT_TRUE(timeout.is_elapsed());
}

TEST(EagerSelectTimeout, Reset) {
  auto time_provider = std::make_shared<MockTimeProvider>();
  EagerSelectTimeout<MockTimeProvider> timeout(1000000, time_provider);

  timeout.start();
  EXPECT_TRUE(timeout.is_started());

  timeout.reset();
  EXPECT_FALSE(timeout.is_started());
  EXPECT_FALSE(timeout.is_elapsed());
}

TEST(EagerSelectTimeout, SetTimeout) {
  auto time_provider = std::make_shared<MockTimeProvider>();
  EagerSelectTimeout<MockTimeProvider> timeout(1000000, time_provider);

  timeout.set_timeout(2000000);
  EXPECT_EQ(timeout.get_timeout(), 2000000);
}

TEST(EagerSelectTimeout, TimeoutNotElapsedBeforeStart) {
  auto time_provider = std::make_shared<MockTimeProvider>();
  EagerSelectTimeout<MockTimeProvider> timeout(1000000, time_provider);

  // Advance time without starting
  time_provider->set_time(2000000);
  // Should not be elapsed since timeout was never started
  EXPECT_FALSE(timeout.is_elapsed());

  // Start the timeout at time 2000000
  timeout.start();
  EXPECT_FALSE(timeout.is_elapsed());  // Not elapsed yet

  // Advance time to trigger the timeout
  time_provider->set_time(3000001);  // Now elapsed: 3000001 - 2000000 = 1000001 >= 1000000
  EXPECT_TRUE(timeout.is_elapsed());
}

TEST(EagerSelectTimeout, MultipleStartsUpdateStartTime) {
  auto time_provider = std::make_shared<MockTimeProvider>();
  EagerSelectTimeout<MockTimeProvider> timeout(1000000, time_provider);

  timeout.start();
  time_provider->advance_time(500000);

  // Start again, resetting the start time
  timeout.start();
  EXPECT_FALSE(timeout.is_elapsed());

  time_provider->advance_time(500000);
  EXPECT_FALSE(timeout.is_elapsed());

  time_provider->advance_time(500000);
  EXPECT_TRUE(timeout.is_elapsed());
}

TEST(EagerSelectTimeout, WithSystemTimeProvider) {
  auto time_provider = std::make_shared<SystemTimeProvider>();
  EagerSelectTimeout<SystemTimeProvider> timeout(100000000, time_provider);  // 100ms

  auto start = std::chrono::steady_clock::now();
  timeout.start();

  // Should not be elapsed immediately
  EXPECT_FALSE(timeout.is_elapsed());

  // Wait for timeout
  std::this_thread::sleep_for(std::chrono::milliseconds(110));

  EXPECT_TRUE(timeout.is_elapsed());
  auto end = std::chrono::steady_clock::now();
  auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(end - start);
  EXPECT_GE(duration.count(), 100);
}

TEST(EagerSelectTimeout, EagerSelectIntegrationWithTimeout) {
  auto time_provider = std::make_shared<MockTimeProvider>();
  EagerSelectTimeout<MockTimeProvider> timeout(1000000, time_provider);
  Signal signal;

  timeout.start();

  std::atomic<bool> thread_started{ false };
  std::atomic<size_t> selected_index{ SIZE_MAX };

  JThread selector([&]() {
      thread_started = true;
      auto result = eager_select(signal, timeout);
      selected_index = result.index();
    });

  while (!thread_started.load()) {
    std::this_thread::sleep_for(std::chrono::microseconds(100));
  }

  std::this_thread::sleep_for(std::chrono::milliseconds(5));

  // Trigger timeout
  time_provider->advance_time(1000000);

  std::this_thread::sleep_for(std::chrono::milliseconds(20));

  EXPECT_EQ(selected_index.load(), 1);  // Timeout is second
}

TEST(EagerSelectTimeout, SignalBeforeTimeout) {
  auto time_provider = std::make_shared<MockTimeProvider>();
  EagerSelectTimeout<MockTimeProvider> timeout(1000000, time_provider);
  Signal signal;

  timeout.start();

  std::atomic<bool> thread_started{ false };
  std::atomic<size_t> selected_index{ SIZE_MAX };

  JThread selector([&]() {
      thread_started = true;
      auto result = eager_select(signal, timeout);
      selected_index = result.index();
    });

  while (!thread_started.load()) {
    std::this_thread::sleep_for(std::chrono::microseconds(100));
  }

  std::this_thread::sleep_for(std::chrono::milliseconds(5));

  // Signal before timeout
  signal.notify_one();

  std::this_thread::sleep_for(std::chrono::milliseconds(20));

  EXPECT_EQ(selected_index.load(), 0);  // Signal is first
}

// ============================================================================
// eager_select Basic Tests
// ============================================================================

TEST(EagerSelect, SingleSignal) {
  Signal signal;

  std::atomic<bool> thread_started{ false };
  std::atomic<bool> result_received{ false };

  JThread selector([&]() {
      thread_started = true;
      // Single signal returns value directly, not in a variant
      eager_select(signal);
      result_received = true;
    });

  while (!thread_started.load()) {
    std::this_thread::sleep_for(std::chrono::microseconds(100));
  }

  std::this_thread::sleep_for(std::chrono::milliseconds(5));
  signal.notify_one();
  std::this_thread::sleep_for(std::chrono::milliseconds(20));

  EXPECT_TRUE(result_received.load());
}

TEST(EagerSelect, TwoSignalsFirstReady) {
  Signal signal1;
  Signal signal2;

  std::atomic<bool> thread_started{ false };
  std::atomic<size_t> selected_index{ SIZE_MAX };

  JThread selector([&]() {
      thread_started = true;
      auto result = eager_select(signal1, signal2);
      selected_index = result.index();
    });

  while (!thread_started.load()) {
    std::this_thread::sleep_for(std::chrono::microseconds(100));
  }

  std::this_thread::sleep_for(std::chrono::milliseconds(5));
  signal1.notify_one();
  std::this_thread::sleep_for(std::chrono::milliseconds(20));

  EXPECT_EQ(selected_index.load(), 0);
}

TEST(EagerSelect, TwoSignalsSecondReady) {
  Signal signal1;
  Signal signal2;

  std::atomic<bool> thread_started{ false };
  std::atomic<size_t> selected_index{ SIZE_MAX };

  JThread selector([&]() {
      thread_started = true;
      auto result = eager_select(signal1, signal2);
      selected_index = result.index();
    });

  while (!thread_started.load()) {
    std::this_thread::sleep_for(std::chrono::microseconds(100));
  }

  std::this_thread::sleep_for(std::chrono::milliseconds(5));
  signal2.notify_one();
  std::this_thread::sleep_for(std::chrono::milliseconds(20));

  EXPECT_EQ(selected_index.load(), 1);
}

TEST(EagerSelect, ThreeSignalsMiddleReady) {
  Signal signal1;
  Signal signal2;
  Signal signal3;

  std::atomic<bool> thread_started{ false };
  std::atomic<size_t> selected_index{ SIZE_MAX };

  JThread selector([&]() {
      thread_started = true;
      auto result = eager_select(signal1, signal2, signal3);
      selected_index = result.index();
    });

  while (!thread_started.load()) {
    std::this_thread::sleep_for(std::chrono::microseconds(100));
  }

  std::this_thread::sleep_for(std::chrono::milliseconds(5));
  signal2.notify_one();
  std::this_thread::sleep_for(std::chrono::milliseconds(20));

  EXPECT_EQ(selected_index.load(), 1);
}

TEST(EagerSelect, PreNotifiedSignal) {
  Signal signal1;
  Signal signal2;
  Signal signal3;

  // Pre-notify signal2
  signal2.notify_one();

  auto result = eager_select(signal1, signal2, signal3);
  EXPECT_EQ(result.index(), 1);
}

TEST(EagerSelect, MultipleSignalsNotifiedFirstWins) {
  Signal signal1;
  Signal signal2;
  Signal signal3;

  // Notify all signals
  signal1.notify_one();
  signal2.notify_one();
  signal3.notify_one();

  auto result = eager_select(signal1, signal2, signal3);
  // First signal should win due to order of checking
  EXPECT_EQ(result.index(), 0);
}

TEST(EagerSelect, ImmediateReturn) {
  Signal signal1;
  Signal signal2;

  signal1.notify_one();

  auto start = std::chrono::steady_clock::now();
  auto result = eager_select(signal1, signal2);
  auto end = std::chrono::steady_clock::now();

  EXPECT_EQ(result.index(), 0);
  auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(end - start);
  EXPECT_LT(duration.count(), 10);  // Should return almost immediately
}

// ============================================================================
// eager_select with Pointers
// ============================================================================

TEST(EagerSelect, WithPointers) {
  Signal signal1;
  Signal signal2;

  std::atomic<bool> thread_started{ false };
  std::atomic<size_t> selected_index{ SIZE_MAX };

  JThread selector([&]() {
      thread_started = true;
      // Pointers must be dereferenced or use reference
      auto result = eager_select(*(&signal1), *(&signal2));
      selected_index = result.index();
    });

  while (!thread_started.load()) {
    std::this_thread::sleep_for(std::chrono::microseconds(100));
  }

  std::this_thread::sleep_for(std::chrono::milliseconds(5));
  signal2.notify_one();
  std::this_thread::sleep_for(std::chrono::milliseconds(20));

  EXPECT_EQ(selected_index.load(), 1);
}

TEST(EagerSelect, MixedPointersAndReferences) {
  Signal signal1;
  Signal signal2;
  Signal signal3;

  std::atomic<bool> thread_started{ false };
  std::atomic<size_t> selected_index{ SIZE_MAX };

  JThread selector([&]() {
      thread_started = true;
      auto result = eager_select(signal1, *(&signal2), signal3);
      selected_index = result.index();
    });

  while (!thread_started.load()) {
    std::this_thread::sleep_for(std::chrono::microseconds(100));
  }

  std::this_thread::sleep_for(std::chrono::milliseconds(5));
  signal2.notify_one();
  std::this_thread::sleep_for(std::chrono::milliseconds(20));

  EXPECT_EQ(selected_index.load(), 1);
}

// ============================================================================
// eager_select with Custom Signals
// ============================================================================

TEST(EagerSelect, CustomSignalType) {
  CustomSignal custom1;
  CustomSignal2 custom2;

  std::atomic<bool> thread_started{ false };
  std::atomic<size_t> selected_index{ SIZE_MAX };

  JThread selector([&]() {
      thread_started = true;
      auto result = eager_select(custom1, custom2);
      selected_index = result.index();
    });

  while (!thread_started.load()) {
    std::this_thread::sleep_for(std::chrono::microseconds(100));
  }

  std::this_thread::sleep_for(std::chrono::milliseconds(5));
  custom1.value = 100;
  custom1.ready = true;
  std::this_thread::sleep_for(std::chrono::milliseconds(20));

  EXPECT_EQ(selected_index.load(), 0);
}

TEST(EagerSelect, CustomSignalVoidReturn) {
  CustomSignalVoid custom1;
  CustomSignalVoid custom2;

  std::atomic<bool> thread_started{ false };
  std::atomic<size_t> selected_index{ SIZE_MAX };

  JThread selector([&]() {
      thread_started = true;
      auto result = eager_select(custom1, custom2);
      selected_index = result.index();
    });

  while (!thread_started.load()) {
    std::this_thread::sleep_for(std::chrono::microseconds(100));
  }

  std::this_thread::sleep_for(std::chrono::milliseconds(5));
  custom2.ready = true;
  std::this_thread::sleep_for(std::chrono::milliseconds(20));

  EXPECT_EQ(selected_index.load(), 1);
}

TEST(EagerSelect, MixedCustomAndBuiltinSignals) {
  Signal signal;
  CustomSignal custom;

  std::atomic<bool> thread_started{ false };
  std::atomic<size_t> selected_index{ SIZE_MAX };

  JThread selector([&]() {
      thread_started = true;
      auto result = eager_select(signal, custom);
      selected_index = result.index();
    });

  while (!thread_started.load()) {
    std::this_thread::sleep_for(std::chrono::microseconds(100));
  }

  std::this_thread::sleep_for(std::chrono::milliseconds(5));
  custom.ready = true;
  std::this_thread::sleep_for(std::chrono::milliseconds(20));

  EXPECT_EQ(selected_index.load(), 1);
}

// ============================================================================
// eager_select Sleep Parameter Tests
// ============================================================================

TEST(EagerSelect, CustomSleepDuration) {
  Signal signal1;
  Signal signal2;

  signal1.notify_one();

  // Test with different sleep durations
  auto start = std::chrono::steady_clock::now();
  auto result1 = eager_select<1000>(signal1, signal2);  // 1 microsecond
  auto end = std::chrono::steady_clock::now();

  EXPECT_EQ(result1.index(), 0);

  // Should return quickly since signal is pre-notified
  auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(end - start);
  EXPECT_LT(duration.count(), 10);
}

TEST(EagerSelect, ZeroSleepDuration) {
  Signal signal1;
  Signal signal2;

  signal1.notify_one();

  auto start = std::chrono::steady_clock::now();
  auto result = eager_select<0>(signal1, signal2);  // No sleep
  auto end = std::chrono::steady_clock::now();

  EXPECT_EQ(result.index(), 0);

  // Should return very quickly
  auto duration = std::chrono::duration_cast<std::chrono::microseconds>(end - start);
  EXPECT_LT(duration.count(), 1000);  // Less than 1ms
}

// ============================================================================
// eager_select with Iterator Ranges Tests
// ============================================================================

TEST(EagerSelect, WithVectorOfSignalPointersMultipleSignals) {
  Signal signal1;
  Signal signal2;
  Signal signal3;

  std::vector<Signal*> signals = { &signal1, &signal2, &signal3 };
  auto range = std::make_pair(signals.begin(), signals.end());

  std::atomic<bool> thread_started{ false };
  std::atomic<bool> result_received{ false };

  JThread selector([&]() {
      thread_started = true;
      auto result = eager_select(range, signal1);  // Mix range with individual signal
      result_received = true;
    });

  while (!thread_started.load()) {
    std::this_thread::sleep_for(std::chrono::microseconds(100));
  }

  std::this_thread::sleep_for(std::chrono::milliseconds(5));
  signal2.notify_one();
  std::this_thread::sleep_for(std::chrono::milliseconds(20));

  EXPECT_TRUE(result_received.load());
}

TEST(EagerSelect, WithVectorOfSignalPointersFirstElement) {
  Signal signal1;
  Signal signal2;
  Signal signal3;
  Signal signal4;

  std::vector<Signal*> signals = { &signal1, &signal2, &signal3 };
  auto range = std::make_pair(signals.begin(), signals.end());

  std::atomic<bool> thread_started{ false };
  std::atomic<size_t> selected_index{ SIZE_MAX };

  JThread selector([&]() {
      thread_started = true;
      auto result = eager_select(range, signal4);
      selected_index = result.index();
    });

  while (!thread_started.load()) {
    std::this_thread::sleep_for(std::chrono::microseconds(100));
  }

  std::this_thread::sleep_for(std::chrono::milliseconds(5));
  signal1.notify_one();
  std::this_thread::sleep_for(std::chrono::milliseconds(20));

  EXPECT_EQ(selected_index.load(), 0);
}

TEST(EagerSelect, WithVectorOfSignalPointersLastElement) {
  Signal signal1;
  Signal signal2;
  Signal signal3;
  Signal signal4;

  std::vector<Signal*> signals = { &signal1, &signal2, &signal3 };
  auto range = std::make_pair(signals.begin(), signals.end());

  std::atomic<bool> thread_started{ false };
  std::atomic<size_t> selected_index{ SIZE_MAX };

  JThread selector([&]() {
      thread_started = true;
      auto result = eager_select(range, signal4);
      selected_index = result.index();
    });

  while (!thread_started.load()) {
    std::this_thread::sleep_for(std::chrono::microseconds(100));
  }

  std::this_thread::sleep_for(std::chrono::milliseconds(5));
  signal3.notify_one();
  std::this_thread::sleep_for(std::chrono::milliseconds(20));

  EXPECT_EQ(selected_index.load(), 0);
}

TEST(EagerSelect, WithVectorOfSignalPointersIndividualSignalSelected) {
  Signal signal1;
  Signal signal2;
  Signal signal3;
  Signal signal4;

  std::vector<Signal*> signals = { &signal1, &signal2, &signal3 };
  auto range = std::make_pair(signals.begin(), signals.end());

  std::atomic<bool> thread_started{ false };
  std::atomic<size_t> selected_index{ SIZE_MAX };

  JThread selector([&]() {
      thread_started = true;
      auto result = eager_select(range, signal4);
      selected_index = result.index();
    });

  while (!thread_started.load()) {
    std::this_thread::sleep_for(std::chrono::microseconds(100));
  }

  std::this_thread::sleep_for(std::chrono::milliseconds(5));
  signal4.notify_one();
  std::this_thread::sleep_for(std::chrono::milliseconds(20));

  EXPECT_EQ(selected_index.load(), 1);
}

TEST(EagerSelect, WithArrayOfSignalPointers) {
  Signal signal1;
  Signal signal2;
  Signal signal3;

  Signal* signals[3] = { &signal1, &signal2, &signal3 };
  auto range = std::make_pair(std::begin(signals), std::end(signals));

  std::atomic<bool> thread_started{ false };
  std::atomic<bool> result_received{ false };

  JThread selector([&]() {
      thread_started = true;
      auto result = eager_select(range, signal1);  // Mix range with individual
      result_received = true;
    });

  while (!thread_started.load()) {
    std::this_thread::sleep_for(std::chrono::microseconds(100));
  }

  std::this_thread::sleep_for(std::chrono::milliseconds(5));
  signal2.notify_one();
  std::this_thread::sleep_for(std::chrono::milliseconds(20));

  EXPECT_TRUE(result_received.load());
}

TEST(EagerSelect, WithEmptyRange) {
  Signal signal1;

  std::vector<Signal*> signals;  // Empty vector
  auto range = std::make_pair(signals.begin(), signals.end());

  std::atomic<bool> thread_started{ false };
  std::atomic<size_t> selected_index{ SIZE_MAX };

  JThread selector([&]() {
      thread_started = true;
      auto result = eager_select(range, signal1);
      selected_index = result.index();
    });

  while (!thread_started.load()) {
    std::this_thread::sleep_for(std::chrono::microseconds(100));
  }

  std::this_thread::sleep_for(std::chrono::milliseconds(5));
  signal1.notify_one();
  std::this_thread::sleep_for(std::chrono::milliseconds(20));

  EXPECT_EQ(selected_index.load(), 1);  // The individual signal
}

// ============================================================================
// eager_select with Dereferenceable Types (Pointers) Tests
// ============================================================================

TEST(EagerSelect, WithRawPointers) {
  Signal signal1;
  Signal signal2;
  Signal signal3;

  Signal* ptr1 = &signal1;
  Signal* ptr2 = &signal2;
  Signal* ptr3 = &signal3;

  std::atomic<bool> thread_started{ false };
  std::atomic<size_t> selected_index{ SIZE_MAX };

  JThread selector([&]() {
      thread_started = true;
      auto result = eager_select(ptr1, ptr2, ptr3);
      selected_index = result.index();
    });

  while (!thread_started.load()) {
    std::this_thread::sleep_for(std::chrono::microseconds(100));
  }

  std::this_thread::sleep_for(std::chrono::milliseconds(5));
  signal2.notify_one();
  std::this_thread::sleep_for(std::chrono::milliseconds(20));

  EXPECT_EQ(selected_index.load(), 1);
}

TEST(EagerSelect, WithMixedPointersAndValues) {
  Signal signal1;
  Signal signal2;
  Signal signal3;

  Signal* ptr2 = &signal2;

  std::atomic<bool> thread_started{ false };
  std::atomic<size_t> selected_index{ SIZE_MAX };

  JThread selector([&]() {
      thread_started = true;
      auto result = eager_select(signal1, ptr2, signal3);
      selected_index = result.index();
    });

  while (!thread_started.load()) {
    std::this_thread::sleep_for(std::chrono::microseconds(100));
  }

  std::this_thread::sleep_for(std::chrono::milliseconds(5));
  signal2.notify_one();
  std::this_thread::sleep_for(std::chrono::milliseconds(20));

  EXPECT_EQ(selected_index.load(), 1);
}

TEST(EagerSelect, WithAllPointers) {
  Signal signal1;
  Signal signal2;

  Signal* ptr1 = &signal1;
  Signal* ptr2 = &signal2;

  std::atomic<bool> thread_started{ false };
  std::atomic<size_t> selected_index{ SIZE_MAX };

  JThread selector([&]() {
      thread_started = true;
      auto result = eager_select(ptr1, ptr2);
      selected_index = result.index();
    });

  while (!thread_started.load()) {
    std::this_thread::sleep_for(std::chrono::microseconds(100));
  }

  std::this_thread::sleep_for(std::chrono::milliseconds(5));
  signal1.notify_one();
  std::this_thread::sleep_for(std::chrono::milliseconds(20));

  EXPECT_EQ(selected_index.load(), 0);
}

TEST(EagerSelect, WithCustomSignalPointers) {
  CustomSignal custom1;
  CustomSignal2 custom2;

  CustomSignal* ptr1 = &custom1;
  CustomSignal2* ptr2 = &custom2;

  std::atomic<bool> thread_started{ false };
  std::atomic<size_t> selected_index{ SIZE_MAX };

  JThread selector([&]() {
      thread_started = true;
      auto result = eager_select(ptr1, ptr2);
      selected_index = result.index();
    });

  while (!thread_started.load()) {
    std::this_thread::sleep_for(std::chrono::microseconds(100));
  }

  std::this_thread::sleep_for(std::chrono::milliseconds(5));
  custom2.ready = true;
  std::this_thread::sleep_for(std::chrono::milliseconds(20));

  EXPECT_EQ(selected_index.load(), 1);
}

// ============================================================================
// eager_select Complex Combinations Tests
// ============================================================================

TEST(EagerSelect, RangeWithPointersAndValues) {
  Signal signal1;
  Signal signal2;
  Signal signal3;
  Signal signal4;

  std::vector<Signal*> signals = { &signal1, &signal2 };
  auto range = std::make_pair(signals.begin(), signals.end());

  Signal* ptr3 = &signal3;

  std::atomic<bool> thread_started{ false };
  std::atomic<size_t> selected_index{ SIZE_MAX };

  JThread selector([&]() {
      thread_started = true;
      auto result = eager_select(range, ptr3, signal4);
      selected_index = result.index();
    });

  while (!thread_started.load()) {
    std::this_thread::sleep_for(std::chrono::microseconds(100));
  }

  std::this_thread::sleep_for(std::chrono::milliseconds(5));
  signal3.notify_one();
  std::this_thread::sleep_for(std::chrono::milliseconds(20));

  EXPECT_EQ(selected_index.load(), 1);
}

TEST(EagerSelect, MultipleRangesAndValues) {
  Signal signal1;
  Signal signal2;
  Signal signal3;
  Signal signal4;
  Signal signal5;

  std::vector<Signal*> signals1 = { &signal1, &signal2 };
  auto range1 = std::make_pair(signals1.begin(), signals1.end());

  std::vector<Signal*> signals2 = { &signal3 };
  auto range2 = std::make_pair(signals2.begin(), signals2.end());

  std::atomic<bool> thread_started{ false };
  std::atomic<size_t> selected_index{ SIZE_MAX };

  JThread selector([&]() {
      thread_started = true;
      auto result = eager_select(range1, signal4, range2, signal5);
      selected_index = result.index();
    });

  while (!thread_started.load()) {
    std::this_thread::sleep_for(std::chrono::microseconds(100));
  }

  std::this_thread::sleep_for(std::chrono::milliseconds(5));
  signal4.notify_one();
  std::this_thread::sleep_for(std::chrono::milliseconds(20));

  EXPECT_EQ(selected_index.load(), 1);
}

TEST(EagerSelect, RangePointerAndTimeout) {
  auto time_provider = std::make_shared<MockTimeProvider>();
  EagerSelectTimeout<MockTimeProvider> timeout(1000000, time_provider);

  Signal signal1;
  Signal signal2;
  Signal signal3;

  std::vector<Signal*> signals = { &signal1, &signal2 };
  auto range = std::make_pair(signals.begin(), signals.end());

  Signal* ptr3 = &signal3;

  timeout.start();

  std::atomic<bool> thread_started{ false };
  std::atomic<size_t> selected_index{ SIZE_MAX };

  JThread selector([&]() {
      thread_started = true;
      auto result = eager_select(range, ptr3, timeout);
      selected_index = result.index();
    });

  while (!thread_started.load()) {
    std::this_thread::sleep_for(std::chrono::microseconds(100));
  }

  std::this_thread::sleep_for(std::chrono::milliseconds(5));
  signal2.notify_one();
  std::this_thread::sleep_for(std::chrono::milliseconds(20));

  EXPECT_EQ(selected_index.load(), 0);
}

TEST(EagerSelect, CustomSignalsWithPointersAndRanges) {
  CustomSignal custom1;
  CustomSignal2 custom2;
  Signal signal1;
  Signal signal2;

  std::vector<Signal*> signals = { &signal1, &signal2 };
  auto range = std::make_pair(signals.begin(), signals.end());

  CustomSignal* ptr1 = &custom1;

  std::atomic<bool> thread_started{ false };
  std::atomic<size_t> selected_index{ SIZE_MAX };

  JThread selector([&]() {
      thread_started = true;
      auto result = eager_select(ptr1, range, custom2);
      selected_index = result.index();
    });

  while (!thread_started.load()) {
    std::this_thread::sleep_for(std::chrono::microseconds(100));
  }

  std::this_thread::sleep_for(std::chrono::milliseconds(5));
  signal2.notify_one();
  std::this_thread::sleep_for(std::chrono::milliseconds(20));

  EXPECT_EQ(selected_index.load(), 1);
}

TEST(EagerSelect, ArrayPointerAndValueCombination) {
  Signal signal1;
  Signal signal2;
  Signal signal3;
  Signal signal4;

  Signal* signals[2] = { &signal1, &signal2 };
  auto range = std::make_pair(std::begin(signals), std::end(signals));

  Signal* ptr3 = &signal3;

  std::atomic<bool> thread_started{ false };
  std::atomic<size_t> selected_index{ SIZE_MAX };

  JThread selector([&]() {
      thread_started = true;
      auto result = eager_select(range, ptr3, signal4);
      selected_index = result.index();
    });

  while (!thread_started.load()) {
    std::this_thread::sleep_for(std::chrono::microseconds(100));
  }

  std::this_thread::sleep_for(std::chrono::milliseconds(5));
  signal1.notify_one();
  std::this_thread::sleep_for(std::chrono::milliseconds(20));

  EXPECT_EQ(selected_index.load(), 0);
}

// ============================================================================
// Stress and Edge Case Tests
// ============================================================================

TEST(EagerSelect, RapidMultipleSelections) {
  Signal signal1;
  Signal signal2;

  std::atomic<int> selections{ 0 };
  std::atomic<bool> stop{ false };

  JThread selector([&]() {
      while (!stop.load()) {
        auto result = eager_select(signal1, signal2);
        selections.fetch_add(1);
        std::this_thread::sleep_for(std::chrono::microseconds(500));
      }
    });

  // Rapidly notify signals
  for (int i = 0; i < 20; ++i) {
    signal1.notify_one();
    std::this_thread::sleep_for(std::chrono::milliseconds(1));
  }

  stop = true;
  signal1.notify_one();  // Unblock selector

  std::this_thread::sleep_for(std::chrono::milliseconds(20));

  EXPECT_GT(selections.load(), 15);
}

TEST(EagerSelect, FourSignals) {
  Signal signal1, signal2, signal3, signal4;

  signal3.notify_one();

  auto result = eager_select(signal1, signal2, signal3, signal4);
  EXPECT_EQ(result.index(), 2);
}

TEST(EagerSelect, FiveSignals) {
  Signal signal1, signal2, signal3, signal4, signal5;

  signal5.notify_one();

  auto result = eager_select(signal1, signal2, signal3, signal4, signal5);
  EXPECT_EQ(result.index(), 4);
}

// ============================================================================
// Integration Tests
// ============================================================================

TEST(EagerSelect, ProducerConsumerPattern) {
  Signal producer1_ready;
  Signal producer2_ready;
  std::atomic<int> consumed_from_producer1{ 0 };
  std::atomic<int> consumed_from_producer2{ 0 };
  std::atomic<bool> stop{ false };

  // Producer 1
  JThread producer1([&]() {
      for (int i = 0; i < 10; ++i) {
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
        producer1_ready.notify_one();
      }
    });

  // Producer 2
  JThread producer2([&]() {
      for (int i = 0; i < 10; ++i) {
        std::this_thread::sleep_for(std::chrono::milliseconds(7));
        producer2_ready.notify_one();
      }
    });

  // Consumer
  JThread consumer([&]() {
      while (!stop.load()) {
        auto result = eager_select(producer1_ready, producer2_ready);
        if (result.index() == 0) {
          consumed_from_producer1.fetch_add(1);
        } else {
          consumed_from_producer2.fetch_add(1);
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
      }
    });

  std::this_thread::sleep_for(std::chrono::milliseconds(150));
  stop = true;
  producer1_ready.notify_one();

  std::this_thread::sleep_for(std::chrono::milliseconds(20));

  // Check that we consumed messages from both producers
  int total = consumed_from_producer1.load() + consumed_from_producer2.load();
  EXPECT_GE(total, 20);  // Should have consumed at least 20 (all produced)
  EXPECT_GT(consumed_from_producer1.load(), 0);
  EXPECT_GT(consumed_from_producer2.load(), 0);
}

TEST(EagerSelect, ComplexScenarioWithTimeoutAndSignals) {
  auto time_provider = std::make_shared<SystemTimeProvider>();
  EagerSelectTimeout<SystemTimeProvider> timeout(50000000, time_provider);  // 50ms
  Signal signal1;
  Signal signal2;

  timeout.start();

  std::atomic<size_t> selected_index{ SIZE_MAX };
  std::atomic<bool> thread_started{ false };

  JThread selector([&]() {
      thread_started = true;
      auto result = eager_select(signal1, signal2, timeout);
      selected_index = result.index();
    });

  while (!thread_started.load()) {
    std::this_thread::sleep_for(std::chrono::microseconds(100));
  }

  // Wait for timeout to occur
  std::this_thread::sleep_for(std::chrono::milliseconds(60));

  EXPECT_EQ(selected_index.load(), 2);  // Timeout should trigger
}
