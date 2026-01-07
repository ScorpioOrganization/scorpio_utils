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
}  // namespace scorpio_utils
