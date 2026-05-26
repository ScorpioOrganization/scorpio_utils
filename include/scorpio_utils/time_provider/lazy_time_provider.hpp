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

#pragma once

#include <atomic>
#include <thread>

#include "scorpio_utils/decorators.hpp"
#include "scorpio_utils/misc.hpp"
#include "scorpio_utils/time_provider/time_provider.hpp"

namespace scorpio_utils::time_provider {
/**
 * A time provider that updates the current time in a separate thread at a specified interval.
 * This class is useful for scenarios where frequent time queries are needed, and the overhead
 * of calling system time functions can be avoided by caching the time value.
 *
 * The time is updated every `offset` nanoseconds, which can be configured during construction
 * or using `set_time_offset`.
 * The default offset is 100 milliseconds (100,000,000 nanoseconds).
 */
class alignas (SCU_HARDWARE_DESTRUCTIVE_INTERFERENCE_SIZE) LazyTimeProvider : public TimeProvider {
  std::atomic<int64_t> _time;
  std::atomic<int64_t> _time_offset;
  std::atomic<bool> _running;
  std::thread _thread;

  void thread_worker();

public:
  /**
   * Creates a new LazyTimeProvider instance.
   * \param offset Time offset in nanoseconds. Default is 100 milliseconds.
   */
  explicit LazyTimeProvider(const int64_t offset = 100'000'000);

  ~LazyTimeProvider() override;

  /**
   * Gets the current time offset in nanoseconds.
   * \return Time offset in nanoseconds.
   */
  SCU_ALWAYS_INLINE auto get_time_offset() const noexcept {
    return _time_offset.load(std::memory_order_relaxed);
  }

  /**
   * Sets the time offset in nanoseconds.
   * \param offset Time offset in nanoseconds.
   */
  SCU_ALWAYS_INLINE void set_time_offset(const int64_t offset) noexcept {
    _time_offset.store(offset, std::memory_order_relaxed);
  }

  /**
   * Gets the current time in nanoseconds.
   * \return Current time in nanoseconds.
   */
  SCU_ALWAYS_INLINE int64_t get_time() const override {
    return _time.load(std::memory_order_relaxed);
  }
};
}  // namespace scorpio_utils::time_provider
