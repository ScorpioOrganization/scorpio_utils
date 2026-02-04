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

#include <thread>
#include <utility>
#include "scorpio_utils/decorators.hpp"

namespace scorpio_utils::threading {
class JThread : public std::thread {
public:
  template<typename ... Args>
  SCU_ALWAYS_INLINE explicit JThread(Args&&... args)
  : std::thread(std::forward<Args>(args)...) { }
  SCU_ALWAYS_INLINE ~JThread() {
    if (joinable()) {
      join();
    }
  }
};

#define SCU_JTHREAD(...) scorpio_utils::threading::JThread SCU_UNIQUE_NAME(scorpio_jthread)(__VA_ARGS__)

}  // namespace scorpio_utils::threading
