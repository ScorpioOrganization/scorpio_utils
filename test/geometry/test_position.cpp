#include <gtest/gtest.h>
#include "scorpio_utils/geometry/position.hpp"

namespace scorpio_utils::geometry {

TEST(Position_Test, EqualityAndInequalityOperators) {
  Position<double> pos1 { { 1.0, 2.0 }, 90.0 };
  Position<double> pos2 { { 1.0, 2.0 }, 90.0 };
  Position<double> pos3 { { 2.0, 3.0 }, 90.0 };
  Position<double> pos4 { { 1.0, 2.0 }, 45.0 };

  ASSERT_TRUE(pos1 == pos2) << "Expected positions to be equal.";
  ASSERT_FALSE(pos1 == pos3) << "Expected positions with different points to be unequal.";
  ASSERT_FALSE(pos1 == pos4) << "Expected positions with different rotation to be unequal.";
  ASSERT_TRUE(pos1 != pos3) << "Expected inequality for different points.";
  ASSERT_TRUE(pos1 != pos4) << "Expected inequality for different rotation.";
  ASSERT_FALSE(pos1 != pos2) << "Expected positions to not be unequal.";
}

TEST(Position_Test, AdditionOperator) {
  Position<double> pos { { 1.0, 2.0 }, 90.0 };
  Point<double> offset { 3.0, 4.0 };
  Position<double> result = pos + offset;

  ASSERT_EQ(result.point.x, 4.0) << "Expected x coordinate after addition to be 4.0.";
  ASSERT_EQ(result.point.y, 6.0) << "Expected y coordinate after addition to be 6.0.";
  ASSERT_EQ(result.rotation, 90.0) << "Rotation should remain unchanged.";
}

TEST(Position_Test, AdditionAssignmentOperator) {
  Position<double> pos { { 1.0, 2.0 }, 90.0 };
  Point<double> offset { 3.0, 4.0 };
  pos += offset;

  ASSERT_EQ(pos.point.x, 4.0) << "Expected x coordinate after += to be 4.0.";
  ASSERT_EQ(pos.point.y, 6.0) << "Expected y coordinate after += to be 6.0.";
  ASSERT_EQ(pos.rotation, 90.0) << "Rotation should remain unchanged.";
}

TEST(Position_Test, SubtractionOperator) {
  Position<double> pos { { 5.0, 7.0 }, 45.0 };
  Point<double> offset { 2.0, 3.0 };
  Position<double> result = pos - offset;

  ASSERT_EQ(result.point.x, 3.0) << "Expected x coordinate after subtraction to be 3.0.";
  ASSERT_EQ(result.point.y, 4.0) << "Expected y coordinate after subtraction to be 4.0.";
  ASSERT_EQ(result.rotation, 45.0) << "Rotation should remain unchanged.";
}

TEST(Position_Test, SubtractionAssignmentOperator) {
  Position<double> pos { { 5.0, 7.0 }, 45.0 };
  Point<double> offset { 2.0, 3.0 };
  pos -= offset;

  ASSERT_EQ(pos.point.x, 3.0) << "Expected x coordinate after -= to be 3.0.";
  ASSERT_EQ(pos.point.y, 4.0) << "Expected y coordinate after -= to be 4.0.";
  ASSERT_EQ(pos.rotation, 45.0) << "Rotation should remain unchanged.";
}

}  // namespace scorpio_utils::geometry
