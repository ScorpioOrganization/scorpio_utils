#include <gtest/gtest.h>

#include <cstdint>
#include <limits>

#include "scorpio_utils/compare.hpp"

using scorpio_utils::cmp_equal;
using scorpio_utils::cmp_less;
using scorpio_utils::cmp_greater;
using scorpio_utils::cmp_greater_equal;
using scorpio_utils::cmp_less_equal;

// ============================================================================
// Test suite for cmp_equal
// ============================================================================

TEST(CmpEqualTest, BothSignedEqual) {
  EXPECT_TRUE(cmp_equal(5, 5));
  EXPECT_TRUE(cmp_equal(-5, -5));
  EXPECT_TRUE(cmp_equal(0, 0));
  EXPECT_TRUE(cmp_equal(INT32_MAX, INT32_MAX));
  EXPECT_TRUE(cmp_equal(INT32_MIN, INT32_MIN));
}

TEST(CmpEqualTest, BothSignedNotEqual) {
  EXPECT_FALSE(cmp_equal(5, 6));
  EXPECT_FALSE(cmp_equal(-5, -6));
  EXPECT_FALSE(cmp_equal(-1, 1));
  EXPECT_FALSE(cmp_equal(0, 1));
}

TEST(CmpEqualTest, BothUnsignedEqual) {
  EXPECT_TRUE(cmp_equal(5u, 5u));
  EXPECT_TRUE(cmp_equal(0u, 0u));
  EXPECT_TRUE(cmp_equal(UINT32_MAX, UINT32_MAX));
}

TEST(CmpEqualTest, BothUnsignedNotEqual) {
  EXPECT_FALSE(cmp_equal(5u, 6u));
  EXPECT_FALSE(cmp_equal(0u, 1u));
  EXPECT_FALSE(cmp_equal(100u, 200u));
}

TEST(CmpEqualTest, SignedAndUnsignedEqual) {
  EXPECT_TRUE(cmp_equal(5, 5u));
  EXPECT_TRUE(cmp_equal(0, 0u));
  EXPECT_TRUE(cmp_equal(100, 100u));
}

TEST(CmpEqualTest, SignedAndUnsignedNotEqual) {
  EXPECT_FALSE(cmp_equal(5, 6u));
  EXPECT_FALSE(cmp_equal(-1, 1u));
  EXPECT_FALSE(cmp_equal(-5, 5u));
}

TEST(CmpEqualTest, SignedNegativeAndUnsigned) {
  EXPECT_FALSE(cmp_equal(-1, 0u));
  EXPECT_FALSE(cmp_equal(-1, 1u));
  EXPECT_FALSE(cmp_equal(-100, 100u));
  EXPECT_FALSE(cmp_equal(INT32_MIN, 0u));
}

TEST(CmpEqualTest, UnsignedAndSignedEqual) {
  EXPECT_TRUE(cmp_equal(5u, 5));
  EXPECT_TRUE(cmp_equal(0u, 0));
  EXPECT_TRUE(cmp_equal(100u, 100));
}

TEST(CmpEqualTest, UnsignedAndSignedNegative) {
  EXPECT_FALSE(cmp_equal(0u, -1));
  EXPECT_FALSE(cmp_equal(1u, -1));
  EXPECT_FALSE(cmp_equal(100u, -100));
}

TEST(CmpEqualTest, EdgeCases) {
  EXPECT_TRUE(cmp_equal(static_cast<int64_t>(UINT32_MAX), static_cast<uint64_t>(UINT32_MAX)));
  EXPECT_FALSE(cmp_equal(static_cast<int32_t>(-1), UINT32_MAX));
  EXPECT_TRUE(cmp_equal(static_cast<uint64_t>(INT64_MAX), INT64_MAX));
  // Overflow test: 256 would overflow uint8_t (max 255)
  EXPECT_FALSE((cmp_equal<uint8_t, int64_t>(255, 256)));
  EXPECT_TRUE((cmp_equal<uint8_t, int64_t>(255, 255)));
  EXPECT_FALSE((cmp_equal<uint8_t, int64_t>(255, 510)));
  // Overflow test: INT16_MAX+1 would overflow int16_t
  EXPECT_FALSE((cmp_equal<int16_t, int64_t>(INT16_MAX, static_cast<int64_t>(INT16_MAX) + 1)));
}

// ============================================================================
// Test suite for cmp_less
// ============================================================================

TEST(CmpLessTest, BothSignedLess) {
  EXPECT_TRUE(cmp_less(5, 6));
  EXPECT_TRUE(cmp_less(-6, -5));
  EXPECT_TRUE(cmp_less(-1, 0));
  EXPECT_TRUE(cmp_less(0, 1));
  EXPECT_TRUE(cmp_less(INT32_MIN, INT32_MAX));
}

TEST(CmpLessTest, BothSignedNotLess) {
  EXPECT_FALSE(cmp_less(6, 5));
  EXPECT_FALSE(cmp_less(5, 5));
  EXPECT_FALSE(cmp_less(0, -1));
  EXPECT_FALSE(cmp_less(1, 0));
}

TEST(CmpLessTest, BothUnsignedLess) {
  EXPECT_TRUE(cmp_less(5u, 6u));
  EXPECT_TRUE(cmp_less(0u, 1u));
  EXPECT_TRUE(cmp_less(100u, 200u));
}

TEST(CmpLessTest, BothUnsignedNotLess) {
  EXPECT_FALSE(cmp_less(6u, 5u));
  EXPECT_FALSE(cmp_less(5u, 5u));
  EXPECT_FALSE(cmp_less(200u, 100u));
}

TEST(CmpLessTest, SignedAndUnsignedLess) {
  EXPECT_TRUE(cmp_less(5, 6u));
  EXPECT_TRUE(cmp_less(0, 1u));
  EXPECT_TRUE(cmp_less(-1, 0u));
  EXPECT_TRUE(cmp_less(-100, 100u));
}

TEST(CmpLessTest, SignedAndUnsignedNotLess) {
  EXPECT_FALSE(cmp_less(6, 5u));
  EXPECT_FALSE(cmp_less(5, 5u));
  EXPECT_FALSE(cmp_less(100, 50u));
}

TEST(CmpLessTest, SignedNegativeAlwaysLessThanUnsigned) {
  EXPECT_TRUE(cmp_less(-1, 0u));
  EXPECT_TRUE(cmp_less(-1, 1u));
  EXPECT_TRUE(cmp_less(-100, 1u));
  EXPECT_TRUE(cmp_less(INT32_MIN, 0u));
}

TEST(CmpLessTest, UnsignedAndSignedLess) {
  EXPECT_TRUE(cmp_less(5u, 6));
  EXPECT_TRUE(cmp_less(0u, 1));
  EXPECT_TRUE(cmp_less(100u, 200));
}

TEST(CmpLessTest, UnsignedAndSignedNegative) {
  EXPECT_FALSE(cmp_less(0u, -1));
  EXPECT_FALSE(cmp_less(1u, -1));
  EXPECT_FALSE(cmp_less(100u, -100));
}

TEST(CmpLessTest, EdgeCases) {
  EXPECT_FALSE(cmp_less(static_cast<uint64_t>(INT64_MAX), INT64_MAX));
  EXPECT_TRUE(cmp_less(static_cast<int32_t>(-1), UINT32_MAX));
  EXPECT_TRUE(cmp_less(INT64_MIN, static_cast<uint64_t>(0)));
  // Overflow test: 256 would overflow uint8_t (max 255)
  EXPECT_TRUE((cmp_less<uint8_t, int64_t>(255, 256)));
  EXPECT_FALSE((cmp_less<uint8_t, int64_t>(255, 255)));
  EXPECT_FALSE((cmp_less<uint8_t, int64_t>(255, 100)));
  // Overflow test: INT16_MAX+1 would overflow int16_t
  EXPECT_TRUE((cmp_less<int16_t, int64_t>(INT16_MAX, static_cast<int64_t>(INT16_MAX) + 1)));
  // Overflow test: negative value below int8_t min
  EXPECT_FALSE((cmp_less<int8_t, int64_t>(INT8_MIN, static_cast<int64_t>(INT8_MIN) - 1)));
}

// ============================================================================
// Test suite for cmp_greater
// ============================================================================

TEST(CmpGreaterTest, BothSignedGreater) {
  EXPECT_TRUE(cmp_greater(6, 5));
  EXPECT_TRUE(cmp_greater(-5, -6));
  EXPECT_TRUE(cmp_greater(0, -1));
  EXPECT_TRUE(cmp_greater(1, 0));
  EXPECT_TRUE(cmp_greater(INT32_MAX, INT32_MIN));
}

TEST(CmpGreaterTest, BothSignedNotGreater) {
  EXPECT_FALSE(cmp_greater(5, 6));
  EXPECT_FALSE(cmp_greater(5, 5));
  EXPECT_FALSE(cmp_greater(-1, 0));
  EXPECT_FALSE(cmp_greater(0, 1));
}

TEST(CmpGreaterTest, BothUnsignedGreater) {
  EXPECT_TRUE(cmp_greater(6u, 5u));
  EXPECT_TRUE(cmp_greater(1u, 0u));
  EXPECT_TRUE(cmp_greater(200u, 100u));
}

TEST(CmpGreaterTest, BothUnsignedNotGreater) {
  EXPECT_FALSE(cmp_greater(5u, 6u));
  EXPECT_FALSE(cmp_greater(5u, 5u));
  EXPECT_FALSE(cmp_greater(100u, 200u));
}

TEST(CmpGreaterTest, SignedAndUnsignedGreater) {
  EXPECT_TRUE(cmp_greater(6, 5u));
  EXPECT_TRUE(cmp_greater(1, 0u));
  EXPECT_TRUE(cmp_greater(100, 50u));
}

TEST(CmpGreaterTest, SignedAndUnsignedNotGreater) {
  EXPECT_FALSE(cmp_greater(5, 6u));
  EXPECT_FALSE(cmp_greater(5, 5u));
  EXPECT_FALSE(cmp_greater(-1, 0u));
  EXPECT_FALSE(cmp_greater(-100, 100u));
}

TEST(CmpGreaterTest, SignedNegativeNeverGreaterThanUnsigned) {
  EXPECT_FALSE(cmp_greater(-1, 0u));
  EXPECT_FALSE(cmp_greater(-1, 1u));
  EXPECT_FALSE(cmp_greater(-100, 1u));
  EXPECT_FALSE(cmp_greater(INT32_MIN, 0u));
}

TEST(CmpGreaterTest, UnsignedAndSignedGreater) {
  EXPECT_TRUE(cmp_greater(6u, 5));
  EXPECT_TRUE(cmp_greater(1u, 0));
  EXPECT_TRUE(cmp_greater(200u, 100));
}

TEST(CmpGreaterTest, UnsignedAlwaysGreaterThanNegativeSigned) {
  EXPECT_TRUE(cmp_greater(0u, -1));
  EXPECT_TRUE(cmp_greater(1u, -1));
  EXPECT_TRUE(cmp_greater(100u, -100));
}

TEST(CmpGreaterTest, EdgeCases) {
  EXPECT_FALSE(cmp_greater(static_cast<uint64_t>(INT64_MAX), INT64_MAX));
  EXPECT_FALSE(cmp_greater(static_cast<int32_t>(-1), UINT32_MAX));
  EXPECT_FALSE(cmp_greater(INT64_MIN, static_cast<uint64_t>(0)));
  // Overflow test: 256 would overflow uint8_t (max 255)
  EXPECT_FALSE((cmp_greater<uint8_t, int64_t>(255, 256)));
  EXPECT_FALSE((cmp_greater<uint8_t, int64_t>(255, 255)));
  EXPECT_TRUE((cmp_greater<uint8_t, int64_t>(255, 100)));
  // Overflow test: INT16_MAX+1 would overflow int16_t
  EXPECT_FALSE((cmp_greater<int16_t, int64_t>(INT16_MAX, static_cast<int64_t>(INT16_MAX) + 1)));
  // Overflow test: negative value below int8_t min
  EXPECT_TRUE((cmp_greater<int8_t, int64_t>(INT8_MIN, static_cast<int64_t>(INT8_MIN) - 1)));
}

// ============================================================================
// Test suite for cmp_less_equal
// ============================================================================

TEST(CmpLessEqualTest, BothSignedLessEqual) {
  EXPECT_TRUE(cmp_less_equal(5, 6));
  EXPECT_TRUE(cmp_less_equal(5, 5));
  EXPECT_TRUE(cmp_less_equal(-6, -5));
  EXPECT_TRUE(cmp_less_equal(-1, 0));
  EXPECT_TRUE(cmp_less_equal(0, 0));
}

TEST(CmpLessEqualTest, BothSignedNotLessEqual) {
  EXPECT_FALSE(cmp_less_equal(6, 5));
  EXPECT_FALSE(cmp_less_equal(0, -1));
  EXPECT_FALSE(cmp_less_equal(1, 0));
}

TEST(CmpLessEqualTest, BothUnsignedLessEqual) {
  EXPECT_TRUE(cmp_less_equal(5u, 6u));
  EXPECT_TRUE(cmp_less_equal(5u, 5u));
  EXPECT_TRUE(cmp_less_equal(0u, 1u));
  EXPECT_TRUE(cmp_less_equal(0u, 0u));
}

TEST(CmpLessEqualTest, BothUnsignedNotLessEqual) {
  EXPECT_FALSE(cmp_less_equal(6u, 5u));
  EXPECT_FALSE(cmp_less_equal(200u, 100u));
}

TEST(CmpLessEqualTest, SignedAndUnsignedLessEqual) {
  EXPECT_TRUE(cmp_less_equal(5, 6u));
  EXPECT_TRUE(cmp_less_equal(5, 5u));
  EXPECT_TRUE(cmp_less_equal(0, 1u));
  EXPECT_TRUE(cmp_less_equal(-1, 0u));
}

TEST(CmpLessEqualTest, SignedAndUnsignedNotLessEqual) {
  EXPECT_FALSE(cmp_less_equal(6, 5u));
  EXPECT_FALSE(cmp_less_equal(100, 50u));
}

TEST(CmpLessEqualTest, SignedNegativeAlwaysLessEqualUnsigned) {
  EXPECT_TRUE(cmp_less_equal(-1, 0u));
  EXPECT_TRUE(cmp_less_equal(-1, 1u));
  EXPECT_TRUE(cmp_less_equal(-100, 1u));
  EXPECT_TRUE(cmp_less_equal(INT32_MIN, 0u));
}

TEST(CmpLessEqualTest, UnsignedAndSignedLessEqual) {
  EXPECT_TRUE(cmp_less_equal(5u, 6));
  EXPECT_TRUE(cmp_less_equal(5u, 5));
  EXPECT_TRUE(cmp_less_equal(0u, 1));
}

TEST(CmpLessEqualTest, UnsignedAndSignedNegative) {
  EXPECT_FALSE(cmp_less_equal(0u, -1));
  EXPECT_FALSE(cmp_less_equal(1u, -1));
  EXPECT_FALSE(cmp_less_equal(100u, -100));
}

TEST(CmpLessEqualTest, EdgeCases) {
  EXPECT_TRUE(cmp_less_equal(static_cast<uint64_t>(INT64_MAX), INT64_MAX));
  EXPECT_TRUE(cmp_less_equal(static_cast<int32_t>(-1), UINT32_MAX));
  EXPECT_TRUE(cmp_less_equal(INT64_MIN, static_cast<uint64_t>(0)));
  // Overflow test: 256 would overflow uint8_t (max 255)
  EXPECT_TRUE((cmp_less_equal<uint8_t, int64_t>(255, 256)));
  EXPECT_TRUE((cmp_less_equal<uint8_t, int64_t>(255, 255)));
  EXPECT_FALSE((cmp_less_equal<uint8_t, int64_t>(255, 100)));
  // Overflow test: INT16_MAX+1 would overflow int16_t
  EXPECT_TRUE((cmp_less_equal<int16_t, int64_t>(INT16_MAX, static_cast<int64_t>(INT16_MAX) + 1)));
  // Overflow test: negative value below int8_t min
  EXPECT_FALSE((cmp_less_equal<int8_t, int64_t>(INT8_MIN, static_cast<int64_t>(INT8_MIN) - 1)));
}

// ============================================================================
// Test suite for cmp_greater_equal
// ============================================================================

TEST(CmpGreaterEqualTest, BothSignedGreaterEqual) {
  EXPECT_TRUE(cmp_greater_equal(6, 5));
  EXPECT_TRUE(cmp_greater_equal(5, 5));
  EXPECT_TRUE(cmp_greater_equal(-5, -6));
  EXPECT_TRUE(cmp_greater_equal(0, -1));
  EXPECT_TRUE(cmp_greater_equal(0, 0));
}

TEST(CmpGreaterEqualTest, BothSignedNotGreaterEqual) {
  EXPECT_FALSE(cmp_greater_equal(5, 6));
  EXPECT_FALSE(cmp_greater_equal(-1, 0));
  EXPECT_FALSE(cmp_greater_equal(0, 1));
}

TEST(CmpGreaterEqualTest, BothUnsignedGreaterEqual) {
  EXPECT_TRUE(cmp_greater_equal(6u, 5u));
  EXPECT_TRUE(cmp_greater_equal(5u, 5u));
  EXPECT_TRUE(cmp_greater_equal(1u, 0u));
  EXPECT_TRUE(cmp_greater_equal(0u, 0u));
}

TEST(CmpGreaterEqualTest, BothUnsignedNotGreaterEqual) {
  EXPECT_FALSE(cmp_greater_equal(5u, 6u));
  EXPECT_FALSE(cmp_greater_equal(100u, 200u));
}

TEST(CmpGreaterEqualTest, SignedAndUnsignedGreaterEqual) {
  EXPECT_TRUE(cmp_greater_equal(6, 5u));
  EXPECT_TRUE(cmp_greater_equal(5, 5u));
  EXPECT_TRUE(cmp_greater_equal(1, 0u));
  EXPECT_TRUE(cmp_greater_equal(100, 50u));
}

TEST(CmpGreaterEqualTest, SignedAndUnsignedNotGreaterEqual) {
  EXPECT_FALSE(cmp_greater_equal(5, 6u));
  EXPECT_FALSE(cmp_greater_equal(-1, 0u));
  EXPECT_FALSE(cmp_greater_equal(-100, 100u));
}

TEST(CmpGreaterEqualTest, SignedNegativeNeverGreaterEqualUnsigned) {
  EXPECT_FALSE(cmp_greater_equal(-1, 0u));
  EXPECT_FALSE(cmp_greater_equal(-1, 1u));
  EXPECT_FALSE(cmp_greater_equal(-100, 1u));
  EXPECT_FALSE(cmp_greater_equal(INT32_MIN, 0u));
}

TEST(CmpGreaterEqualTest, UnsignedAndSignedGreaterEqual) {
  EXPECT_TRUE(cmp_greater_equal(6u, 5));
  EXPECT_TRUE(cmp_greater_equal(5u, 5));
  EXPECT_TRUE(cmp_greater_equal(1u, 0));
}

TEST(CmpGreaterEqualTest, UnsignedAlwaysGreaterEqualNegativeSigned) {
  EXPECT_TRUE(cmp_greater_equal(0u, -1));
  EXPECT_TRUE(cmp_greater_equal(1u, -1));
  EXPECT_TRUE(cmp_greater_equal(100u, -100));
}

TEST(CmpGreaterEqualTest, EdgeCases) {
  EXPECT_TRUE(cmp_greater_equal(static_cast<uint64_t>(INT64_MAX), INT64_MAX));
  EXPECT_FALSE(cmp_greater_equal(static_cast<int32_t>(-1), UINT32_MAX));
  EXPECT_FALSE(cmp_greater_equal(INT64_MIN, static_cast<uint64_t>(0)));
  // Overflow test: 256 would overflow uint8_t (max 255)
  EXPECT_FALSE((cmp_greater_equal<uint8_t, int64_t>(255, 256)));
  EXPECT_TRUE((cmp_greater_equal<uint8_t, int64_t>(255, 255)));
  EXPECT_TRUE((cmp_greater_equal<uint8_t, int64_t>(255, 100)));
  // Overflow test: INT16_MAX+1 would overflow int16_t
  EXPECT_FALSE((cmp_greater_equal<int16_t, int64_t>(INT16_MAX, static_cast<int64_t>(INT16_MAX) + 1)));
  // Overflow test: negative value below int8_t min
  EXPECT_TRUE((cmp_greater_equal<int8_t, int64_t>(INT8_MIN, static_cast<int64_t>(INT8_MIN) - 1)));
}

// ============================================================================
// Test suite for all comparison macros
// ============================================================================

TEST(ComparisonMacrosTest, EqFromNeq) {
  struct TestStruct {
    int value;
    bool operator!=(const TestStruct& other) const {
      return value != other.value;
    }
    SCU_EQ_FROM_NEQ(TestStruct)
  };

  TestStruct a{ 5 };
  TestStruct b{ 5 };
  TestStruct c{ 10 };

  EXPECT_TRUE(a == b);
  EXPECT_FALSE(a == c);
}

TEST(ComparisonMacrosTest, EqFromLt) {
  struct TestStruct {
    int value;
    bool operator<(const TestStruct& other) const {
      return value < other.value;
    }
    SCU_EQ_FROM_LT(TestStruct)
  };

  TestStruct a{ 5 };
  TestStruct b{ 5 };
  TestStruct c{ 10 };

  EXPECT_TRUE(a == b);
  EXPECT_FALSE(a == c);
}

TEST(ComparisonMacrosTest, EqFromGt) {
  struct TestStruct {
    int value;
    bool operator>(const TestStruct& other) const {
      return value > other.value;
    }
    SCU_EQ_FROM_GT(TestStruct)
  };

  TestStruct a{ 5 };
  TestStruct b{ 5 };
  TestStruct c{ 10 };

  EXPECT_TRUE(a == b);
  EXPECT_FALSE(a == c);
}

TEST(ComparisonMacrosTest, EqFromLe) {
  struct TestStruct {
    int value;
    bool operator<=(const TestStruct& other) const {
      return value <= other.value;
    }
    SCU_EQ_FROM_LE(TestStruct)
  };

  TestStruct a{ 5 };
  TestStruct b{ 5 };
  TestStruct c{ 10 };

  EXPECT_TRUE(a == b);
  EXPECT_FALSE(a == c);
}

TEST(ComparisonMacrosTest, EqFromGe) {
  struct TestStruct {
    int value;
    bool operator>=(const TestStruct& other) const {
      return value >= other.value;
    }
    SCU_EQ_FROM_GE(TestStruct)
  };

  TestStruct a{ 5 };
  TestStruct b{ 5 };
  TestStruct c{ 10 };

  EXPECT_TRUE(a == b);
  EXPECT_FALSE(a == c);
}

TEST(ComparisonMacrosTest, NeqFromEq) {
  struct TestStruct {
    int value;
    bool operator==(const TestStruct& other) const {
      return value == other.value;
    }
    SCU_NEQ_FROM_EQ(TestStruct)
  };

  TestStruct a{ 5 };
  TestStruct b{ 5 };
  TestStruct c{ 10 };

  EXPECT_FALSE(a != b);
  EXPECT_TRUE(a != c);
}

TEST(ComparisonMacrosTest, NeqFromLt) {
  struct TestStruct {
    int value;
    bool operator<(const TestStruct& other) const {
      return value < other.value;
    }
    SCU_NEQ_FROM_LT(TestStruct)
  };

  TestStruct a{ 5 };
  TestStruct b{ 5 };
  TestStruct c{ 10 };

  EXPECT_FALSE(a != b);
  EXPECT_TRUE(a != c);
}

TEST(ComparisonMacrosTest, NeqFromGt) {
  struct TestStruct {
    int value;
    bool operator>(const TestStruct& other) const {
      return value > other.value;
    }
    SCU_NEQ_FROM_GT(TestStruct)
  };

  TestStruct a{ 5 };
  TestStruct b{ 5 };
  TestStruct c{ 10 };

  EXPECT_FALSE(a != b);
  EXPECT_TRUE(a != c);
}

TEST(ComparisonMacrosTest, NeqFromLe) {
  struct TestStruct {
    int value;
    bool operator<=(const TestStruct& other) const {
      return value <= other.value;
    }
    SCU_NEQ_FROM_LE(TestStruct)
  };

  TestStruct a{ 5 };
  TestStruct b{ 5 };
  TestStruct c{ 10 };

  EXPECT_FALSE(a != b);
  EXPECT_TRUE(a != c);
}

TEST(ComparisonMacrosTest, NeqFromGe) {
  struct TestStruct {
    int value;
    bool operator>=(const TestStruct& other) const {
      return value >= other.value;
    }
    SCU_NEQ_FROM_GE(TestStruct)
  };

  TestStruct a{ 5 };
  TestStruct b{ 5 };
  TestStruct c{ 10 };

  EXPECT_FALSE(a != b);
  EXPECT_TRUE(a != c);
}

TEST(ComparisonMacrosTest, LtFromGt) {
  struct TestStruct {
    int value;
    bool operator>(const TestStruct& other) const {
      return value > other.value;
    }
    SCU_LT_FROM_GT(TestStruct)
  };

  TestStruct a{ 5 };
  TestStruct b{ 10 };
  TestStruct c{ 5 };

  EXPECT_TRUE(a < b);
  EXPECT_FALSE(b < a);
  EXPECT_FALSE(a < c);
}

TEST(ComparisonMacrosTest, LtFromGe) {
  struct TestStruct {
    int value;
    bool operator>=(const TestStruct& other) const {
      return value >= other.value;
    }
    SCU_LT_FROM_GE(TestStruct)
  };

  TestStruct a{ 5 };
  TestStruct b{ 10 };
  TestStruct c{ 5 };

  EXPECT_TRUE(a < b);
  EXPECT_FALSE(b < a);
  EXPECT_FALSE(a < c);
}

TEST(ComparisonMacrosTest, LtFromLe) {
  struct TestStruct {
    int value;
    bool operator<=(const TestStruct& other) const {
      return value <= other.value;
    }
    SCU_LT_FROM_LE(TestStruct)
  };

  TestStruct a{ 5 };
  TestStruct b{ 10 };
  TestStruct c{ 5 };

  EXPECT_TRUE(a < b);
  EXPECT_FALSE(b < a);
  EXPECT_FALSE(a < c);
}

TEST(ComparisonMacrosTest, GtFromLt) {
  struct TestStruct {
    int value;
    bool operator<(const TestStruct& other) const {
      return value < other.value;
    }
    SCU_GT_FROM_LT(TestStruct)
  };

  TestStruct a{ 10 };
  TestStruct b{ 5 };
  TestStruct c{ 10 };

  EXPECT_TRUE(a > b);
  EXPECT_FALSE(b > a);
  EXPECT_FALSE(a > c);
}

TEST(ComparisonMacrosTest, GtFromLe) {
  struct TestStruct {
    int value;
    bool operator<=(const TestStruct& other) const {
      return value <= other.value;
    }
    SCU_GT_FROM_LE(TestStruct)
  };

  TestStruct a{ 10 };
  TestStruct b{ 5 };
  TestStruct c{ 10 };

  EXPECT_TRUE(a > b);
  EXPECT_FALSE(b > a);
  EXPECT_FALSE(a > c);
}

TEST(ComparisonMacrosTest, GtFromGe) {
  struct TestStruct {
    int value;
    bool operator>=(const TestStruct& other) const {
      return value >= other.value;
    }
    SCU_GT_FROM_GE(TestStruct)
  };

  TestStruct a{ 10 };
  TestStruct b{ 5 };
  TestStruct c{ 10 };

  EXPECT_TRUE(a > b);
  EXPECT_FALSE(b > a);
  EXPECT_FALSE(a > c);
}

TEST(ComparisonMacrosTest, LeFromGt) {
  struct TestStruct {
    int value;
    bool operator>(const TestStruct& other) const {
      return value > other.value;
    }
    SCU_LE_FROM_GT(TestStruct)
  };

  TestStruct a{ 5 };
  TestStruct b{ 10 };
  TestStruct c{ 5 };

  EXPECT_TRUE(a <= b);
  EXPECT_TRUE(a <= c);
  EXPECT_FALSE(b <= a);
}

TEST(ComparisonMacrosTest, LeFromGe) {
  struct TestStruct {
    int value;
    bool operator>=(const TestStruct& other) const {
      return value >= other.value;
    }
    SCU_LE_FROM_GE(TestStruct)
  };

  TestStruct a{ 5 };
  TestStruct b{ 10 };
  TestStruct c{ 5 };

  EXPECT_TRUE(a <= b);
  EXPECT_TRUE(a <= c);
  EXPECT_FALSE(b <= a);
}

TEST(ComparisonMacrosTest, LeFromLt) {
  struct TestStruct {
    int value;
    bool operator<(const TestStruct& other) const {
      return value < other.value;
    }
    SCU_LE_FROM_LT(TestStruct)
  };

  TestStruct a{ 5 };
  TestStruct b{ 10 };
  TestStruct c{ 5 };

  EXPECT_TRUE(a <= b);
  EXPECT_TRUE(a <= c);
  EXPECT_FALSE(b <= a);
}

TEST(ComparisonMacrosTest, GeFromLt) {
  struct TestStruct {
    int value;
    bool operator<(const TestStruct& other) const {
      return value < other.value;
    }
    SCU_GE_FROM_LT(TestStruct)
  };

  TestStruct a{ 10 };
  TestStruct b{ 5 };
  TestStruct c{ 10 };

  EXPECT_TRUE(a >= b);
  EXPECT_TRUE(a >= c);
  EXPECT_FALSE(b >= a);
}

TEST(ComparisonMacrosTest, GeFromLe) {
  struct TestStruct {
    int value;
    bool operator<=(const TestStruct& other) const {
      return value <= other.value;
    }
    SCU_GE_FROM_LE(TestStruct)
  };

  TestStruct a{ 10 };
  TestStruct b{ 5 };
  TestStruct c{ 10 };

  EXPECT_TRUE(a >= b);
  EXPECT_TRUE(a >= c);
  EXPECT_FALSE(b >= a);
}

TEST(ComparisonMacrosTest, GeFromGt) {
  struct TestStruct {
    int value;
    bool operator>(const TestStruct& other) const {
      return value > other.value;
    }
    SCU_GE_FROM_GT(TestStruct)
  };

  TestStruct a{ 10 };
  TestStruct b{ 5 };
  TestStruct c{ 10 };

  EXPECT_TRUE(a >= b);
  EXPECT_TRUE(a >= c);
  EXPECT_FALSE(b >= a);
}
