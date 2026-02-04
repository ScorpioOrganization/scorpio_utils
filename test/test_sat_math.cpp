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

#include <cmath>
#include <cstdint>
#include <limits>

#include "scorpio_utils/sat_math.hpp"

using scorpio_utils::sat_add;
using scorpio_utils::sat_sub;
using scorpio_utils::sat_mul;
using scorpio_utils::sat_div;

template<typename T>
void all_cases(std::function<T(int, int)> tested, std::function<T(int, int)> valid) {
  static_assert(std::is_same_v<T, uint8_t>|| std::is_same_v<T, int8_t>, "Only uint8_t and int8_t are supported");
  for (int a = SCU_AS(int, std::numeric_limits<T>::min()); a <= SCU_AS(int, std::numeric_limits<T>::max()); ++a) {
    for (int b = SCU_AS(int, std::numeric_limits<T>::min()); b <= SCU_AS(int, std::numeric_limits<T>::max()); ++b) {
      T expected = valid(a, b);
      EXPECT_EQ(tested(SCU_AS(uint8_t, a), SCU_AS(uint8_t, b)), expected)
        << "Failed for uint8_t a=" << static_cast<int>(a) << ", b=" << static_cast<int>(b);
    }
  }
}

template<typename T>
T clamp_to_type(int value) {
  return SCU_AS(T,
    std::clamp(value, SCU_AS(int, std::numeric_limits<T>::min()), SCU_AS(int, std::numeric_limits<T>::max())));
}

TEST(SatMathAdd, AllCasesSigned) {
  all_cases<int8_t>(
    [](int8_t a, int8_t b) -> int8_t {
      return sat_add(a, b);
    },
    [](int a, int b) -> int8_t {
      return clamp_to_type<int8_t>(a + b);
  });
}

TEST(SatMathSub, AllCasesUnsigned) {
  all_cases<uint8_t>(
    [](uint8_t a, uint8_t b) -> uint8_t {
      return sat_sub(a, b);
    },
    [](int a, int b) -> uint8_t {
      return clamp_to_type<uint8_t>(a - b);
  });
}

TEST(SatMathSub, AllCasesSigned) {
  all_cases<int8_t>(
    [](int8_t a, int8_t b) -> int8_t {
      return sat_sub(a, b);
    },
    [](int a, int b) -> int8_t {
      return clamp_to_type<int8_t>(a - b);
  });
}

TEST(SatMathMul, AllCasesUnsigned) {
  all_cases<uint8_t>(
    [](uint8_t a, uint8_t b) -> uint8_t {
      return sat_mul(a, b);
    },
    [](int a, int b) -> uint8_t {
      return clamp_to_type<uint8_t>(a * b);
  });
}

TEST(SatMathMul, AllCasesSigned) {
  all_cases<int8_t>(
    [](int8_t a, int8_t b) -> int8_t {
      return sat_mul(a, b);
    },
    [](int a, int b) -> int8_t {
      return clamp_to_type<int8_t>(a * b);
  });
}

TEST(SatMathDiv, AllCasesUnsigned) {
  all_cases<uint8_t>(
    [](uint8_t a, uint8_t b) -> uint8_t {
      if (b == 0) {
        return 0;
      }
      return sat_div(a, b);
    },
    [](int a, int b) -> uint8_t {
      if (b == 0) {
        return 0;
      }
      return clamp_to_type<uint8_t>(a / b);
  });
}

TEST(SatMathDiv, AllCasesSigned) {
  all_cases<int8_t>(
    [](int8_t a, int8_t b) -> int8_t {
      if (b == 0) {
        return 0;
      }
      return sat_div(a, b);
    },
    [](int a, int b) -> int8_t {
      if (b == 0) {
        return 0;
      }
      return clamp_to_type<int8_t>(a / b);
  });
}
