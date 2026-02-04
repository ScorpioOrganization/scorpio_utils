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

#include <array>
#include <atomic>
#include <exception>
#include <optional>
#include <string>
#include <thread>
#include <utility>
#include "scorpio_utils/decorators.hpp"
#include "scorpio_utils/threading/eager_select.hpp"
#include "scorpio_utils/type_traits.hpp"

namespace scorpio_utils::threading {
class SignalException : std::exception {
  std::string msg;

public:
  explicit SignalException(std::string message)
  : msg(std::move(message)) { }
  SCU_ALWAYS_INLINE const char * what() const noexcept override {
    return msg.c_str();
  }
};

class Signal {
  std::atomic<int> _futex;

public:
  SCU_ALWAYS_INLINE bool SCU_EAGER_SELECT_IS_READY() {
    auto current = _futex.load(std::memory_order_relaxed);
    while (current > 0 && !_futex.compare_exchange_weak(current, current - 1,
      std::memory_order_relaxed, std::memory_order_relaxed)) { }
    SCU_UNLIKELY_THROW_IF(current < 0, SignalException, "Signal is closed");
    return current > 0;
  }

  SCU_ALWAYS_INLINE void SCU_EAGER_SELECT_GET_VALUE() noexcept { }

  SCU_ALWAYS_INLINE Signal(int initial = 0)
  : _futex(initial) { }
  ~Signal() = default;

  SCU_UNCOPYBLE(Signal);
  SCU_UNMOVABLE(Signal);

  void notify(int count);
  SCU_ALWAYS_INLINE auto notify_one() {
    return notify(1);
  }
  void wait();
  void close();
  SCU_ALWAYS_INLINE auto count() const noexcept {
    return _futex.load(std::memory_order_relaxed);
  }
  /**
   * Attempts to take the signal without blocking.
   */
  SCU_ALWAYS_INLINE bool try_take() {
    return SCU_EAGER_SELECT_IS_READY();
  }
};
}  // namespace scorpio_utils::threading
