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
