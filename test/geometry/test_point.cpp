#include <gtest/gtest.h>
#include "scorpio_utils/geometry/point.hpp"

namespace scorpio_utils::geometry {

TEST(Point_Test, AdditionOperator) {
  Point<double> a{ 1.0, 2.0 };
  Point<double> b{ 3.0, 4.0 };
  Point<double> result = a + b;
  ASSERT_DOUBLE_EQ(result.x, 4.0) << "Invalid x addition.";
  ASSERT_DOUBLE_EQ(result.y, 6.0) << "Invalid y addition.";

  Point<double> c{ -1.0, 2.0 };
  Point<double> d{ 3.0, -4.0 };
  Point<double> result2 = c + d;
  ASSERT_DOUBLE_EQ(result2.x, 2.0) << "Invalid x addition.";
  ASSERT_DOUBLE_EQ(result2.y, -2.0) << "Invalid y addition.";
}

TEST(Point_Test, SubtractionOperator) {
  Point<double> a{ 1.0, 2.0 };
  Point<double> b{ 3.0, 4.0 };
  Point<double> result = a - b;
  ASSERT_DOUBLE_EQ(result.x, -2.0) << "Invalid x subtraction.";
  ASSERT_DOUBLE_EQ(result.y, -2.0) << "Invalid y subtraction.";

  Point<double> c{ -1.0, 2.0 };
  Point<double> d{ 3.0, -4.0 };
  Point<double> result2 = c - d;
  ASSERT_DOUBLE_EQ(result2.x, -4.0) << "Invalid x subtraction.";
  ASSERT_DOUBLE_EQ(result2.y, 6.0) << "Invalid y subtraction.";
}

TEST(Point_Test, EqualityOperator) {
  Point<int> a{ 1, 2 };
  Point<int> b{ 1, 2 };
  EXPECT_TRUE(a == b) << "Expected values to be equal.";
}

TEST(Point_Test, InequalityOperator) {
  Point<int> a{ 1, 2 };
  Point<int> b{ 2, 1 };
  EXPECT_TRUE(a != b) << "Values are supposed to be unequal.";
}

TEST(Point_Test, EqualWithThreshold) {
  Point<double> a{ 1.432, 5.211 };
  Point<double> b{ 1.450, 5.210 };

  ASSERT_TRUE(a.equal_with_threshold(b, 0.02)) << "Expected equality with threshold.";

  Point<double> c{ 1.0, 1.0 };
  Point<double> d{ 1.1, 1.1 };
  EXPECT_FALSE(c.equal_with_threshold(d, 0.01)) << "Expected unequality with threshold.";
}

TEST(Point_Test, ScalarMultiplicationOperator) {
  Point<double> a{ 2.0, 3.0 };
  Point<double> result = a * 2.0;
  EXPECT_DOUBLE_EQ(result.x, 4.0) << "Invalid multiplication in x.";
  EXPECT_DOUBLE_EQ(result.y, 6.0) << "Invalid multiplication in y.";
}

TEST(Point_Test, ScalarMultiplicationAssignmentOperator) {
  Point<double> a{ 2.0, 3.0 };
  a *= 2.0;
  EXPECT_DOUBLE_EQ(a.x, 4.0);
  EXPECT_DOUBLE_EQ(a.y, 6.0);
}

TEST(Point_Test, ScalarDivisionOperator) {
  Point<double> a{ 4.0, 6.0 };
  Point<double> result = a / 2.0;
  EXPECT_DOUBLE_EQ(result.x, 2.0);
  EXPECT_DOUBLE_EQ(result.y, 3.0);
}

TEST(Point_Test, ScalarDivisionAssignmentOperator) {
  Point<double> a{ 4.0, 6.0 };
  a /= 2.0;
  EXPECT_DOUBLE_EQ(a.x, 2.0);
  EXPECT_DOUBLE_EQ(a.y, 3.0);
}

}  // namespace scorpio_utils::geometry
