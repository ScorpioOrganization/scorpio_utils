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

#include <limits>
#include <type_traits>
#include "scorpio_utils/decorators.hpp"

#define ZERO SCU_AS(T, 0)

namespace scorpio_utils {
template<typename T>
SCU_CONST_FUNC constexpr auto sat_add(T a, T b) noexcept {
  if constexpr (std::is_signed_v<T>) {
    if (a <= ZERO && b <= ZERO) {
      if (std::numeric_limits<T>::min() - b >= a) {
        return std::numeric_limits<T>::min();
      }
      return SCU_AS(T, a + b);
    }
    if (a < ZERO || b < ZERO) {
      return SCU_AS(T, a + b);
    }
  }
  if (std::numeric_limits<T>::max() - b <= a) {
    return std::numeric_limits<T>::max();
  }
  return SCU_AS(T, a + b);
}

template<typename T>
SCU_CONST_FUNC constexpr auto sat_sub(T a, T b) noexcept {
  if constexpr (std::is_signed_v<T>) {
    if (a < ZERO && b <= ZERO) {
      return SCU_AS(T, a - b);
    }
    if (a < ZERO) {
      if (std::numeric_limits<T>::min() + b >= a) {
        return std::numeric_limits<T>::min();
      }
      return SCU_AS(T, a - b);
    }
    if (b < ZERO) {
      if (std::numeric_limits<T>::max() + b <= a) {
        return std::numeric_limits<T>::max();
      }
      return SCU_AS(T, a - b);
    }
  } else if (b >= a) {
    return SCU_AS(T, ZERO);
  }
  return SCU_AS(T, a - b);
}

template<typename T>
SCU_CONST_FUNC constexpr auto sat_mul(T a, T b) noexcept {
  if (a == ZERO || b == ZERO) {
    return SCU_AS(T, ZERO);
  }
  if constexpr (std::is_signed_v<T>) {
    if (a < ZERO && b < ZERO) {
      if (std::numeric_limits<T>::max() / b > a) {
        return std::numeric_limits<T>::max();
      }
      return SCU_AS(T, a * b);
    }
    if (a < ZERO) {
      if (std::numeric_limits<T>::min() / b > a) {
        return std::numeric_limits<T>::min();
      }
      return SCU_AS(T, a * b);
    }
    if (b < ZERO) {
      if (std::numeric_limits<T>::min() / a > b) {
        return std::numeric_limits<T>::min();
      }
      return SCU_AS(T, a * b);
    }
  }
  if (std::numeric_limits<T>::max() / b < a) {
    return std::numeric_limits<T>::max();
  }
  return SCU_AS(T, a * b);
}

template<typename T>
SCU_CONST_FUNC constexpr auto sat_div(T a, T b) {
  if constexpr (std::is_signed_v<T>) {
    if (a == std::numeric_limits<T>::min() && b == -1) {
      return std::numeric_limits<T>::max();
    }
  }
  return SCU_AS(T, a / b);
}
}  // namespace scorpio_utils

#undef ZERO
