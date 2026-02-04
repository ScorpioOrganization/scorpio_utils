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

#include "scorpio_utils/threading/signal.hpp"

extern "C" {
  #include <linux/futex.h>
  #include <sys/syscall.h>
  #include <unistd.h>
}

#include <cstring>

using scorpio_utils::threading::Signal;

void Signal::notify(int count) {
  SCU_ASSUME(count > 0);
  _futex.fetch_add(count, std::memory_order_relaxed);
  SCU_UNLIKELY_THROW_IF(::syscall(SYS_futex, &_futex, FUTEX_WAKE | FUTEX_PRIVATE_FLAG, count, nullptr, nullptr, 0) < 0,
    SignalException, std::string("Futex wake failed: ") + std::strerror(errno));
}

void Signal::close() {
  _futex.store(SCU_AS(int, -1e9), std::memory_order_relaxed);
  SCU_UNLIKELY_THROW_IF(::syscall(
    SYS_futex, &_futex, FUTEX_WAKE | FUTEX_PRIVATE_FLAG, INT32_MAX, nullptr, nullptr, 0) < 0, SignalException,
    std::string("Futex wake failed: ") + std::strerror(errno));
}

void Signal::wait() {
  while (SCU_UNLIKELY(!SCU_EAGER_SELECT_IS_READY())) {
    const auto error = ::syscall(SYS_futex, &_futex, FUTEX_WAIT | FUTEX_PRIVATE_FLAG, 0, nullptr, nullptr, 0);
    SCU_UNLIKELY_THROW_IF(error != 0 && errno != EAGAIN && errno != EINTR, SignalException,
      std::string("Futex wait failed: ") + std::strerror(errno));
  }
}
