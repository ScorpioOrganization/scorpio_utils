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

#include <stddef.h>
#include <algorithm>
#include <cstdint>

namespace scorpio_utils {
__attribute__((noinline))
static bool is_big_endian_() {
  union {
    uint32_t i;
    char c[4];
  } test;
  test.i = 0x01020304;
  return test.c[0] == 1;
}

inline const bool is_big_endian = is_big_endian_();

/**
 * Converts a buffer of bytes to network byte order (big-endian).
 * If the system is already big-endian, it does nothing.
 *
 * \param buffer The buffer to convert.
 */
template<size_t T>
inline void to_network_byte_order(std::array<uint8_t, T>& buffer) {
  if (!is_big_endian) {
    std::reverse(buffer.begin(), buffer.end());
  }
}

/**
 * Converts a buffer of bytes to host byte order.
 * If the system is already big-endian, it does nothing.
 *
 * \param buffer The buffer to convert.
 */
template<size_t T>
inline void to_host_byte_order(std::array<uint8_t, T>& buffer) {
  to_network_byte_order(buffer);
}

inline void to_network_byte_order(std::vector<uint8_t>& buffer) {
  if (!is_big_endian) {
    std::reverse(buffer.begin(), buffer.end());
  }
}

inline void to_host_byte_order(std::vector<uint8_t>& buffer) {
  to_network_byte_order(buffer);
}
}  // namespace scorpio_utils
