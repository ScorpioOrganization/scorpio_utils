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
#include <array>
#include "scorpio_utils/endianness.hpp"

TEST(Endianess, NetworkOrderAndBack) {
  // Setup
  const std::array<uint8_t, 4> original_buffer = { 0x01, 0x02, 0x03, 0x04 };
  std::array<uint8_t, 4> buffer = original_buffer;

  // Action
  scorpio_utils::to_network_byte_order(buffer);
  scorpio_utils::to_host_byte_order(buffer);

  // Verify
  for (size_t i = 0; i < buffer.size(); ++i) {
    EXPECT_EQ(buffer[i], original_buffer[i]) << "Byte mismatch at index " << i;
  }
}
