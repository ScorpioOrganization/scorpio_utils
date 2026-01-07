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
