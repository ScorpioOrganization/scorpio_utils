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

#include <new>

#include "scorpio_utils/decorators.hpp"

namespace scorpio_utils {
template<typename T>
SCU_ALWAYS_INLINE SCU_PURE T clone(T v) {
  return v;
}

/**
 * A utility structure to help with visitor overloading.
 * This is useful for creating a visitor that can handle multiple types
 * without needing to define a separate visitor for each type.
 *
 * \tparam T The types to be overloaded.
 */
template<class ... T>
struct VisitorOverloadingHelper : T ... { using T::operator() ...; };
template<class ... T>
VisitorOverloadingHelper(T ...)->VisitorOverloadingHelper<T...>;

// Define hardware interference size if not defined by the standard library
#if defined(__cpp_lib_hardware_interference_size) && false  // Disable for now
# define SCU_HARDWARE_DESTRUCTIVE_INTERFERENCE_SIZE (std::hardware_destructive_interference_size)
#else
# define SCU_HARDWARE_DESTRUCTIVE_INTERFERENCE_SIZE 64
#endif

template<typename T, typename Y>
SCU_CONST_FUNC constexpr T least_significant_bytes_to_val(T last_val, Y cur_val) {
  static_assert(sizeof(T) >= sizeof(Y));
  // Mask that takes bytes from size_t that are not included to SeqNumber
  // This excessive casting is actually necessary because apparently ~SCU_AS(uint8_t, 0) will be promoted to int
  constexpr T significant_bytes_mask = SCU_AS(T, ~SCU_AS(T, SCU_AS(Y, ~SCU_AS(Y, 0))));
  constexpr T significant_bytes_increment_step = SCU_AS(T, SCU_AS(T, 1) << SCU_AS(T, (sizeof(Y) * 8)));
  // Static cast and truncating bytes is intentional
  const auto significant_bytes = significant_bytes_mask & last_val;
  if (cur_val < SCU_AS(Y, last_val)) {
    return SCU_AS(T, cur_val) | (significant_bytes + significant_bytes_increment_step);
  } else {
    return SCU_AS(T, cur_val) | significant_bytes;
  }
}
}  // namespace scorpio_utils
