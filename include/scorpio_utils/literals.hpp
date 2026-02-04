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

#include "scorpio_utils/decorators.hpp"

namespace scorpio_utils::literals {
SCU_ALWAYS_INLINE SCU_CONST_FUNC constexpr unsigned long long operator""_K(unsigned long long value) noexcept {  // NOLINT
  return value * 1000ull;
}
SCU_ALWAYS_INLINE SCU_CONST_FUNC constexpr unsigned long long operator""_KB(unsigned long long value) noexcept {  // NOLINT
  return value * 1024ull;
}
SCU_ALWAYS_INLINE SCU_CONST_FUNC constexpr unsigned long long operator""_M(unsigned long long value) noexcept {  // NOLINT
  return value * 1000ull * 1000ull;
}
SCU_ALWAYS_INLINE SCU_CONST_FUNC constexpr unsigned long long operator""_MB(unsigned long long value) noexcept {  // NOLINT
  return value * 1024ull * 1024ull;
}
SCU_ALWAYS_INLINE SCU_CONST_FUNC constexpr unsigned long long operator""_G(unsigned long long value) noexcept {  // NOLINT
  return value * 1000ull * 1000ull * 1000ull;
}
SCU_ALWAYS_INLINE SCU_CONST_FUNC constexpr unsigned long long operator""_GB(unsigned long long value) noexcept {  // NOLINT
  return value * 1024ull * 1024ull * 1024ull;
}
SCU_ALWAYS_INLINE SCU_CONST_FUNC constexpr unsigned long long operator""_T(unsigned long long value) noexcept {  // NOLINT
  return value * 1000ull * 1000ull * 1000ull * 1000ull;
}
SCU_ALWAYS_INLINE SCU_CONST_FUNC constexpr unsigned long long operator""_TB(unsigned long long value) noexcept {  // NOLINT
  return value * 1024ull * 1024ull * 1024ull * 1024ull;
}
}  // namespace scorpio_utils::literals
