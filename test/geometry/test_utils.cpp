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
#include "scorpio_utils/geometry/utils.hpp"
#include "scorpio_utils/geometry/discrete_rotation.hpp"

constexpr double THRESHOLD = 1e-6;

namespace scorpio_utils::geometry {

TEST(Utils_Test, RotateLeft) {
  Point<int> p{ 1, 2 };
  Point<int> result = rotate_left(p);
  EXPECT_EQ(result.x, -2) << "Wrong x in left rotation.";
  EXPECT_EQ(result.y, 1) << "Wrong y in left rotation.";
}


TEST(Utils_Test, RotateRight) {
  Point<int> p{ 1, 2 };
  Point<int> result = rotate_right(p);
  EXPECT_EQ(result.x, 2) << "Wrong x in right rotation.";
  EXPECT_EQ(result.y, -1) << "Wrong y in right rotation.";
}


TEST(Utils_Test, Rotate180) {
  Point<int> p{ 1, -2 };
  Point<int> result = rotate_180(p);
  EXPECT_EQ(result.x, -1) << "Invalid x in 180 rotation";
  EXPECT_EQ(result.y, 2) << "Invalid y in 180 rotation";
}

TEST(Utils_Test, RotateWithDoubleAngle) {
  Point<int> p{ 2, 3 };

  auto result = rotate(p, M_PI / 2);
  ASSERT_NEAR(result.x, -3.0, THRESHOLD) << "Invalid x coordinate after 90 degree rotation";
  ASSERT_NEAR(result.y, 2.0, THRESHOLD) << "Invalid y coordinate after 90 degree rotation";

  result = rotate(p, M_PI);
  ASSERT_NEAR(result.x, -2.0, THRESHOLD) << "Invalid x coordinate after 180 degree rotation";
  ASSERT_NEAR(result.y, -3.0, THRESHOLD) << "Invalid y coordinate after 180 degree rotation";

  result = rotate(p, 3 * M_PI / 2);
  ASSERT_NEAR(result.x, 3.0, THRESHOLD) << "Invalid x coordinate after 270 degree rotation";
  ASSERT_NEAR(result.y, -2.0, THRESHOLD) << "Invalid y coordinate after 270 degree rotation";

  result = rotate(p, 0.0);
  ASSERT_NEAR(result.x, 2.0, THRESHOLD) << "Invalid x coordinate after 0 degree rotation";
  ASSERT_NEAR(result.y, 3.0, THRESHOLD) << "Invalid y coordinate after 0 degree rotation";
}

TEST(Utils_Test, RotateWithDiscreteRotation) {
  Point<int> p{ 2, 3 };

  auto result = rotate(p, DiscreteRotation::DEGREES_90);
  ASSERT_EQ(result.x, -3) << "Invalid x coordinate after 90 degree discrete rotation";
  ASSERT_EQ(result.y, 2) << "Invalid y coordinate after 90 degree discrete rotation";

  result = rotate(p, DiscreteRotation::DEGREES_180);
  ASSERT_EQ(result.x, -2) << "Invalid x coordinate after 180 degree discrete rotation";
  ASSERT_EQ(result.y, -3) << "Invalid y coordinate after 180 degree discrete rotation";

  result = rotate(p, DiscreteRotation::DEGREES_270);
  ASSERT_EQ(result.x, 3) << "Invalid x coordinate after 270 degree discrete rotation";
  ASSERT_EQ(result.y, -2) << "Invalid y coordinate after 270 degree discrete rotation";

  result = rotate(p, DiscreteRotation::DEGREES_0);
  ASSERT_EQ(result.x, 2) << "Invalid x coordinate after 0 degree discrete rotation";
  ASSERT_EQ(result.y, 3) << "Invalid y coordinate after 0 degree discrete rotation";
}

TEST(Utils_Test, TranslateRelativePointToPosition) {
  Position<int, DiscreteRotation> position{ Point<int>{ 1, 2 }, DiscreteRotation::DEGREES_90 };
  Point<int> relative_point{ 3, 4 };

  auto result = translate_relative_point_to_position(position, relative_point);
  ASSERT_EQ(result.x, -3) << "Invalid x coordinate after translation and rotation";
  ASSERT_EQ(result.y, 5) << "Invalid y coordinate after translation and rotation";
}

TEST(Utils_Test, Distance) {
  Point<int> p1({ 2, 3 });
  Point<int> p2({ -1, 5 });
  Point<double> p1d({ 2.5, 3.5 });
  Point<double> p2d({ -1.5, 5.5 });
  double dist = distance(p1, p2);
  double expected = std::sqrt(std::pow(3.0, 2) + std::pow(2.0, 2));
  ASSERT_DOUBLE_EQ(dist, expected) << "Incorrect distance calculation between points";

  dist = distance(p1d, p2d);
  expected = std::sqrt(std::pow(4.0, 2) + std::pow(2.0, 2));
  ASSERT_DOUBLE_EQ(dist, expected) << "Incorrect distance calculation between double points";
}

TEST(Utils_Test, Length) {
  Point<int> p1({ 2, 3 });
  Point<double> p1d({ 2.5, 3.5 });

  double len = length(p1);
  double expected = std::sqrt(std::pow(2.0, 2) + std::pow(3.0, 2));
  ASSERT_DOUBLE_EQ(len, expected) << "Incorrect length calculation for point";

  len = length(p1d);
  expected = std::sqrt(std::pow(2.5, 2) + std::pow(3.5, 2));
  ASSERT_DOUBLE_EQ(len, expected) << "Incorrect length calculation for double point";
}

TEST(Utils_Test, GetAngle) {
  Point<int> p1({ 2, 3 });
  Point<int> p2({ -1, 5 });
  Point<double> p1d({ 2.5, 3.5 });
  Point<double> p2d({ -1.5, 5.5 });
  double angle = get_angle(p1, p2);
  double expected = std::atan2(2.0, -3.0);
  ASSERT_DOUBLE_EQ(angle, expected) << "Incorrect angle between points";

  angle = get_angle(p1d, p2d);
  expected = std::atan2(2.0, -4.0);
  ASSERT_DOUBLE_EQ(angle, expected) << "Incorrect angle between double points";
}

TEST(Utils_Test, GetAngleWithHeight) {
  Point<int> p1({ 0, 100 });
  Point<int> p2({ 10, 100 });
  double angle = get_angle(p1, p2, 100, 100);
  ASSERT_NEAR(angle, 0.0, THRESHOLD) << "Expected 0 for no height difference.";

  angle = get_angle(p1, p2, 10, 20);
  ASSERT_NEAR(angle, std::atan2(10, 10), THRESHOLD) << "Expected value around ~0.785398";

  Point<int> p({ 0, 0 });
  angle = get_angle(p, p, 0, 0);
  ASSERT_NEAR(angle, 0.0, THRESHOLD) << "Expected 0 for data with no difference";
}

TEST(Utils_Test, DiscreteRotationFromRadians) {
  ASSERT_EQ(discrete_rotation_from_radians(0.0), DiscreteRotation::DEGREES_0)
      << "Incorrect discrete rotation for 0 radians";
  ASSERT_EQ(discrete_rotation_from_radians(M_PI / 2 - 0.1), DiscreteRotation::DEGREES_90)
      << "Incorrect discrete rotation for π/2 radians";
  ASSERT_EQ(discrete_rotation_from_radians(M_PI), DiscreteRotation::DEGREES_180)
      << "Incorrect discrete rotation for π radians";
  ASSERT_EQ(discrete_rotation_from_radians(3 * M_PI / 2 - 0.1), DiscreteRotation::DEGREES_270)
      << "Incorrect discrete rotation for 3π/2 radians";
  ASSERT_EQ(discrete_rotation_from_radians(2 * M_PI - 0.1), DiscreteRotation::DEGREES_0)
      << "Incorrect discrete rotation for 2π radians";
  ASSERT_EQ(discrete_rotation_from_radians(-M_PI / 2), DiscreteRotation::DEGREES_270)
      << "Incorrect discrete rotation for negative radians";
}

TEST(Utils_Test, DiscreteRotationToRadians) {
  ASSERT_DOUBLE_EQ(discrete_rotation_to_radians(DiscreteRotation::DEGREES_0), 0.0)
      << "Incorrect radians for 0 degrees";
  ASSERT_DOUBLE_EQ(discrete_rotation_to_radians(DiscreteRotation::DEGREES_90), M_PI / 2)
      << "Incorrect radians for 90 degrees";
  ASSERT_DOUBLE_EQ(discrete_rotation_to_radians(DiscreteRotation::DEGREES_180), M_PI)
      << "Incorrect radians for 180 degrees";
  ASSERT_DOUBLE_EQ(discrete_rotation_to_radians(DiscreteRotation::DEGREES_270), 3 * M_PI / 2)
      << "Incorrect radians for 270 degrees";
}

TEST(Utils_Test, MetropolitanDistance) {
  Point<int> p1({ 2, 3 });
  Point<int> p2({ -1, 5 });
  ASSERT_EQ(metropolitan_distance(p1, p2), 5) << "Incorrect metropolitan distance calculation";
  ASSERT_EQ(metropolitan_distance(p2, p1), 5) << "Incorrect metropolitan distance calculation";
  ASSERT_EQ(metropolitan_distance(Point<int>{ 0, 0 }, Point<int>{ 5, 5 }), 10)
      << "Incorrect metropolitan distance calculation";
}

TEST(Utils_Test, YawDifference) {
  ASSERT_DOUBLE_EQ(yaw_difference(0.0, M_PI / 2), M_PI / 2)
      << "Incorrect yaw difference calculation";
  ASSERT_DOUBLE_EQ(yaw_difference(M_PI / 2, 0.0), M_PI / 2)
      << "Incorrect yaw difference calculation";
  ASSERT_DOUBLE_EQ(yaw_difference(0.0, 2 * M_PI), 0.0)
      << "Incorrect yaw difference calculation for full rotation";
  ASSERT_DOUBLE_EQ(yaw_difference(0.0, 3 * M_PI / 2), M_PI / 2)
      << "Incorrect yaw difference calculation";
  ASSERT_DOUBLE_EQ(yaw_difference(0.0, -M_PI / 2), M_PI / 2)
      << "Incorrect yaw difference calculation for negative angles";
}

}  // namespace scorpio_utils::geometry
