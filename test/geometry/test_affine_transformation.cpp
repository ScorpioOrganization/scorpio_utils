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
#include "scorpio_utils/geometry/affine_transformation.hpp"
#include "scorpio_utils/geometry/point.hpp"
#include "scorpio_utils/geometry/utils.hpp"

// All tests were made based on this simulation in GeoGebra: https://www.geogebra.org/graphing/g3mkvegy. Some rotations
// and displacement vectors were taken randomly and some are specific edge cases.

namespace scorpio_utils::geometry {

constexpr double EPSILON = 1e-3;

double degrees_to_radians(double degrees) { return std::remainder(degrees * M_PI / 180.0, 2 * M_PI); }
double rotation_of(const AffineTransformationMatrix& transform) { return std::atan2(transform.c, transform.a); }

class CoordinateSystemTestPointsInQuadrantI : public ::testing::Test {
protected:
  static constexpr Point<double> first_point_first_xy_plane{ 2.0, 1.0 };
  static constexpr Point<double> second_point_first_xy_plane{ 1.0, 3.0 };
  static constexpr Point<double> arbitrary_point_first_xy_plane{ 4, 6 };
};

// How the tests are constructed:
TEST_F(CoordinateSystemTestPointsInQuadrantI, IdenticalOriginTest0Rotation) {
  // These values are the chosen rotation and displacement vector.
  // The rotation inside brackets is -alpha, where alpha is the angle between the two coordinate systems.
  double rotation = degrees_to_radians(-0.0);
  // The displacement vector is the (-1) * vector from the origin of the first coordinate system to the origin of the
  // second coordinate system.
  Point<double> displacement_vector{ -0.0, -0.0 };
  displacement_vector = rotate(displacement_vector, rotation);
  // We did all above tweaks so that we went from transforming just axis to transforming points between coordinate
  // systems. These values are read from the GeoGebra simulation for the second coordinate system.
  const Point<double> first_point_second_xy_plane{ 2.0, 1.0 };
  const Point<double> second_point_second_xy_plane{ 1.0, 3.0 };
  const Point<double> arbitrary_point_second_xy_plane{ 4, 6 };

  // Our affine_transform_from_points_only_translation_and_rotation function should return the same rotation and
  // displacement vector as we defined above.
  auto result = affine_transform_from_points_only_translation_and_rotation(
      first_point_first_xy_plane, second_point_first_xy_plane, first_point_second_xy_plane,
      second_point_second_xy_plane);

  EXPECT_NEAR(rotation_of(result), rotation, EPSILON)
      << "Expected rotation to be " << rotation << " for points: (" << first_point_first_xy_plane.x << ", "
      << first_point_first_xy_plane.y << ") and (" << second_point_first_xy_plane.x << ", "
      << second_point_first_xy_plane.y << ") to (" << first_point_second_xy_plane.x << ", "
      << first_point_second_xy_plane.y << ") and (" << second_point_second_xy_plane.x << ", "
      << second_point_second_xy_plane.y << ").";
  EXPECT_NEAR(result.tx, displacement_vector.x, EPSILON)
      << "Expected displacement vector x to be " << displacement_vector.x << " for points: ("
      << first_point_first_xy_plane.x << ", " << first_point_first_xy_plane.y << ") and ("
      << second_point_first_xy_plane.x << ", " << second_point_first_xy_plane.y << ") to ("
      << first_point_second_xy_plane.x << ", " << first_point_second_xy_plane.y << ") and ("
      << second_point_second_xy_plane.x << ", " << second_point_second_xy_plane.y << ").";
  EXPECT_NEAR(result.ty, displacement_vector.y, EPSILON)
      << "Expected displacement vector y to be " << displacement_vector.y << " for points: ("
      << first_point_first_xy_plane.x << ", " << first_point_first_xy_plane.y << ") and ("
      << second_point_first_xy_plane.x << ", " << second_point_first_xy_plane.y << ") to ("
      << first_point_second_xy_plane.x << ", " << first_point_second_xy_plane.y << ") and ("
      << second_point_second_xy_plane.x << ", " << second_point_second_xy_plane.y << ").";
  EXPECT_NEAR(first_point_second_xy_plane.x, transform_point(result, first_point_first_xy_plane).x, EPSILON)
      << "Expected first point in second coordinate system to be (" << first_point_second_xy_plane.x << ", "
      << first_point_second_xy_plane.y << ") but got (" << transform_point(result, first_point_first_xy_plane).x << ", "
      << transform_point(result, first_point_first_xy_plane).y << ").";
  EXPECT_NEAR(first_point_second_xy_plane.y, transform_point(result, first_point_first_xy_plane).y, EPSILON)
      << "Expected first point in second coordinate system to be (" << first_point_second_xy_plane.x << ", "
      << first_point_second_xy_plane.y << ") but got (" << transform_point(result, first_point_first_xy_plane).x << ", "
      << transform_point(result, first_point_first_xy_plane).y << ").";
  EXPECT_NEAR(second_point_second_xy_plane.x, transform_point(result, second_point_first_xy_plane).x, EPSILON)
      << "Expected second point in second coordinate system to be (" << second_point_second_xy_plane.x << ", "
      << second_point_second_xy_plane.y << ") but got (" << transform_point(result, second_point_first_xy_plane).x
      << ", " << transform_point(result, second_point_first_xy_plane).y << ").";
  EXPECT_NEAR(second_point_second_xy_plane.y, transform_point(result, second_point_first_xy_plane).y, EPSILON)
      << "Expected second point in second coordinate system to be (" << second_point_second_xy_plane.x << ", "
      << second_point_second_xy_plane.y << ") but got (" << transform_point(result, second_point_first_xy_plane).x
      << ", " << transform_point(result, second_point_first_xy_plane).y << ").";
  EXPECT_NEAR(arbitrary_point_second_xy_plane.x, transform_point(result, arbitrary_point_first_xy_plane).x, EPSILON)
      << "Expected arbitrary point in second coordinate system to be (" << arbitrary_point_second_xy_plane.x << ", "
      << arbitrary_point_second_xy_plane.y << ") but got (" << transform_point(result, arbitrary_point_first_xy_plane).x
      << ", " << transform_point(result, arbitrary_point_first_xy_plane).y << ").";
  EXPECT_NEAR(arbitrary_point_second_xy_plane.y, transform_point(result, arbitrary_point_first_xy_plane).y, EPSILON)
      << "Expected arbitrary point in second coordinate system to be (" << arbitrary_point_second_xy_plane.x << ", "
      << arbitrary_point_second_xy_plane.y << ") but got (" << transform_point(result, arbitrary_point_first_xy_plane).x
      << ", " << transform_point(result, arbitrary_point_first_xy_plane).y << ").";
}

TEST_F(CoordinateSystemTestPointsInQuadrantI, IdenticalOriginTestminus180Rotation) {
  double rotation = degrees_to_radians(-180.0);
  Point<double> displacement_vector{ -0.0, -0.0 };
  displacement_vector = rotate(displacement_vector, rotation);

  const Point<double> first_point_second_xy_plane{ -2.0, -1.0 };
  const Point<double> second_point_second_xy_plane{ -1.0, -3.0 };
  const Point<double> arbitrary_point_second_xy_plane{ -4.0, -6.0 };

  auto result = affine_transform_from_points_only_translation_and_rotation(
      first_point_first_xy_plane, second_point_first_xy_plane, first_point_second_xy_plane,
      second_point_second_xy_plane);

  EXPECT_NEAR(rotation_of(result), rotation, EPSILON)
      << "Expected rotation to be " << rotation << " for points: (" << first_point_first_xy_plane.x << ", "
      << first_point_first_xy_plane.y << ") and (" << second_point_first_xy_plane.x << ", "
      << second_point_first_xy_plane.y << ") to (" << first_point_second_xy_plane.x << ", "
      << first_point_second_xy_plane.y << ") and (" << second_point_second_xy_plane.x << ", "
      << second_point_second_xy_plane.y << ").";
  EXPECT_NEAR(result.tx, displacement_vector.x, EPSILON)
      << "Expected displacement vector x to be " << displacement_vector.x << " for points: ("
      << first_point_first_xy_plane.x << ", " << first_point_first_xy_plane.y << ") and ("
      << second_point_first_xy_plane.x << ", " << second_point_first_xy_plane.y << ") to ("
      << first_point_second_xy_plane.x << ", " << first_point_second_xy_plane.y << ") and ("
      << second_point_second_xy_plane.x << ", " << second_point_second_xy_plane.y << ").";
  EXPECT_NEAR(result.ty, displacement_vector.y, EPSILON)
      << "Expected displacement vector y to be " << displacement_vector.y << " for points: ("
      << first_point_first_xy_plane.x << ", " << first_point_first_xy_plane.y << ") and ("
      << second_point_first_xy_plane.x << ", " << second_point_first_xy_plane.y << ") to ("
      << first_point_second_xy_plane.x << ", " << first_point_second_xy_plane.y << ") and ("
      << second_point_second_xy_plane.x << ", " << second_point_second_xy_plane.y << ").";
  EXPECT_NEAR(first_point_second_xy_plane.x, transform_point(result, first_point_first_xy_plane).x, EPSILON)
      << "Expected first point in second coordinate system to be (" << first_point_second_xy_plane.x << ", "
      << first_point_second_xy_plane.y << ") but got (" << transform_point(result, first_point_first_xy_plane).x << ", "
      << transform_point(result, first_point_first_xy_plane).y << ").";
  EXPECT_NEAR(first_point_second_xy_plane.y, transform_point(result, first_point_first_xy_plane).y, EPSILON)
      << "Expected first point in second coordinate system to be (" << first_point_second_xy_plane.x << ", "
      << first_point_second_xy_plane.y << ") but got (" << transform_point(result, first_point_first_xy_plane).x << ", "
      << transform_point(result, first_point_first_xy_plane).y << ").";
  EXPECT_NEAR(second_point_second_xy_plane.x, transform_point(result, second_point_first_xy_plane).x, EPSILON)
      << "Expected second point in second coordinate system to be (" << second_point_second_xy_plane.x << ", "
      << second_point_second_xy_plane.y << ") but got (" << transform_point(result, second_point_first_xy_plane).x
      << ", " << transform_point(result, second_point_first_xy_plane).y << ").";
  EXPECT_NEAR(second_point_second_xy_plane.y, transform_point(result, second_point_first_xy_plane).y, EPSILON)
      << "Expected second point in second coordinate system to be (" << second_point_second_xy_plane.x << ", "
      << second_point_second_xy_plane.y << ") but got (" << transform_point(result, second_point_first_xy_plane).x
      << ", " << transform_point(result, second_point_first_xy_plane).y << ").";
  EXPECT_NEAR(arbitrary_point_second_xy_plane.x, transform_point(result, arbitrary_point_first_xy_plane).x, EPSILON)
      << "Expected arbitrary point in second coordinate system to be (" << arbitrary_point_second_xy_plane.x << ", "
      << arbitrary_point_second_xy_plane.y << ") but got (" << transform_point(result, arbitrary_point_first_xy_plane).x
      << ", " << transform_point(result, arbitrary_point_first_xy_plane).y << ").";
  EXPECT_NEAR(arbitrary_point_second_xy_plane.y, transform_point(result, arbitrary_point_first_xy_plane).y, EPSILON)
      << "Expected arbitrary point in second coordinate system to be (" << arbitrary_point_second_xy_plane.x << ", "
      << arbitrary_point_second_xy_plane.y << ") but got (" << transform_point(result, arbitrary_point_first_xy_plane).x
      << ", " << transform_point(result, arbitrary_point_first_xy_plane).y << ").";
}

TEST_F(CoordinateSystemTestPointsInQuadrantI, IdenticalOriginTestminus104Rotation) {
  double rotation = degrees_to_radians(-104.0);

  Point<double> displacement_vector{ -0.0, -0.0 };
  displacement_vector = rotate(displacement_vector, rotation);

  const Point<double> first_point_second_xy_plane{ 0.4864519350767, -2.1825133481517 };
  const Point<double> second_point_second_xy_plane{ 2.6689652832283, -1.696061413075 };
  const Point<double> arbitrary_point_second_xy_plane{ 4.8540867752573, -5.332714278702 };

  auto result = affine_transform_from_points_only_translation_and_rotation(
      first_point_first_xy_plane, second_point_first_xy_plane, first_point_second_xy_plane,
      second_point_second_xy_plane);
  EXPECT_NEAR(rotation_of(result), rotation, EPSILON)
      << "Expected rotation to be " << rotation << " for points: (" << first_point_first_xy_plane.x << ", "
      << first_point_first_xy_plane.y << ") and (" << second_point_first_xy_plane.x << ", "
      << second_point_first_xy_plane.y << ") to (" << first_point_second_xy_plane.x << ", "
      << first_point_second_xy_plane.y << ") and (" << second_point_second_xy_plane.x << ", "
      << second_point_second_xy_plane.y << ").";
  EXPECT_NEAR(result.tx, displacement_vector.x, EPSILON)
      << "Expected displacement vector x to be " << displacement_vector.x << " for points: ("
      << first_point_first_xy_plane.x << ", " << first_point_first_xy_plane.y << ") and ("
      << second_point_first_xy_plane.x << ", " << second_point_first_xy_plane.y << ") to ("
      << first_point_second_xy_plane.x << ", " << first_point_second_xy_plane.y << ") and ("
      << second_point_second_xy_plane.x << ", " << second_point_second_xy_plane.y << ").";
  EXPECT_NEAR(result.ty, displacement_vector.y, EPSILON)
      << "Expected displacement vector y to be " << displacement_vector.y << " for points: ("
      << first_point_first_xy_plane.x << ", " << first_point_first_xy_plane.y << ") and ("
      << second_point_first_xy_plane.x << ", " << second_point_first_xy_plane.y << ") to ("
      << first_point_second_xy_plane.x << ", " << first_point_second_xy_plane.y << ") and ("
      << second_point_second_xy_plane.x << ", " << second_point_second_xy_plane.y << ").";
  EXPECT_NEAR(first_point_second_xy_plane.x, transform_point(result, first_point_first_xy_plane).x, EPSILON)
      << "Expected first point in second coordinate system to be (" << first_point_second_xy_plane.x << ", "
      << first_point_second_xy_plane.y << ") but got (" << transform_point(result, first_point_first_xy_plane).x << ", "
      << transform_point(result, first_point_first_xy_plane).y << ").";
  EXPECT_NEAR(first_point_second_xy_plane.y, transform_point(result, first_point_first_xy_plane).y, EPSILON)
      << "Expected first point in second coordinate system to be (" << first_point_second_xy_plane.x << ", "
      << first_point_second_xy_plane.y << ") but got (" << transform_point(result, first_point_first_xy_plane).x << ", "
      << transform_point(result, first_point_first_xy_plane).y << ").";
  EXPECT_NEAR(second_point_second_xy_plane.x, transform_point(result, second_point_first_xy_plane).x, EPSILON)
      << "Expected second point in second coordinate system to be (" << second_point_second_xy_plane.x << ", "
      << second_point_second_xy_plane.y << ") but got (" << transform_point(result, second_point_first_xy_plane).x
      << ", " << transform_point(result, second_point_first_xy_plane).y << ").";
  EXPECT_NEAR(second_point_second_xy_plane.y, transform_point(result, second_point_first_xy_plane).y, EPSILON)
      << "Expected second point in second coordinate system to be (" << second_point_second_xy_plane.x << ", "
      << second_point_second_xy_plane.y << ") but got (" << transform_point(result, second_point_first_xy_plane).x
      << ", " << transform_point(result, second_point_first_xy_plane).y << ").";
  EXPECT_NEAR(arbitrary_point_second_xy_plane.x, transform_point(result, arbitrary_point_first_xy_plane).x, EPSILON)
      << "Expected arbitrary point in second coordinate system to be (" << arbitrary_point_second_xy_plane.x << ", "
      << arbitrary_point_second_xy_plane.y << ") but got (" << transform_point(result, arbitrary_point_first_xy_plane).x
      << ", " << transform_point(result, arbitrary_point_first_xy_plane).y << ").";
  EXPECT_NEAR(arbitrary_point_second_xy_plane.y, transform_point(result, arbitrary_point_first_xy_plane).y, EPSILON)
      << "Expected arbitrary point in second coordinate system to be (" << arbitrary_point_second_xy_plane.x << ", "
      << arbitrary_point_second_xy_plane.y << ") but got (" << transform_point(result, arbitrary_point_first_xy_plane).x
      << ", " << transform_point(result, arbitrary_point_first_xy_plane).y << ").";
}

TEST_F(CoordinateSystemTestPointsInQuadrantI, NonIdenticalOriginTestminus64Rotation) {
  double rotation = degrees_to_radians(-64.0);
  Point<double> displacement_vector{ -0.5, -1.3 };
  displacement_vector = rotate(displacement_vector, rotation);

  const Point<double> first_point_second_xy_plane{ 0.3879185062939, -1.4797024134855 };
  const Point<double> second_point_second_xy_plane{ 1.7471354521031, 0.2958339263918 };
  const Point<double> arbitrary_point_second_xy_plane{ 5.7586310313679, -1.0854347721384 };

  auto result = affine_transform_from_points_only_translation_and_rotation(
      first_point_first_xy_plane, second_point_first_xy_plane, first_point_second_xy_plane,
      second_point_second_xy_plane);
  EXPECT_NEAR(rotation_of(result), rotation, EPSILON)
      << "Expected rotation to be " << rotation << " for points: (" << first_point_first_xy_plane.x << ", "
      << first_point_first_xy_plane.y << ") and (" << second_point_first_xy_plane.x << ", "
      << second_point_first_xy_plane.y << ") to (" << first_point_second_xy_plane.x << ", "
      << first_point_second_xy_plane.y << ") and (" << second_point_second_xy_plane.x << ", "
      << second_point_second_xy_plane.y << ").";
  EXPECT_NEAR(result.tx, displacement_vector.x, EPSILON)
      << "Expected displacement vector x to be " << displacement_vector.x << " for points: ("
      << first_point_first_xy_plane.x << ", " << first_point_first_xy_plane.y << ") and ("
      << second_point_first_xy_plane.x << ", " << second_point_first_xy_plane.y << ") to ("
      << first_point_second_xy_plane.x << ", " << first_point_second_xy_plane.y << ") and ("
      << second_point_second_xy_plane.x << ", " << second_point_second_xy_plane.y << ").";
  EXPECT_NEAR(result.ty, displacement_vector.y, EPSILON)
      << "Expected displacement vector y to be " << displacement_vector.y << " for points: ("
      << first_point_first_xy_plane.x << ", " << first_point_first_xy_plane.y << ") and ("
      << second_point_first_xy_plane.x << ", " << second_point_first_xy_plane.y << ") to ("
      << first_point_second_xy_plane.x << ", " << first_point_second_xy_plane.y << ") and ("
      << second_point_second_xy_plane.x << ", " << second_point_second_xy_plane.y << ").";
  EXPECT_NEAR(first_point_second_xy_plane.x, transform_point(result, first_point_first_xy_plane).x, EPSILON)
      << "Expected first point in second coordinate system to be (" << first_point_second_xy_plane.x << ", "
      << first_point_second_xy_plane.y << ") but got (" << transform_point(result, first_point_first_xy_plane).x << ", "
      << transform_point(result, first_point_first_xy_plane).y << ").";
  EXPECT_NEAR(first_point_second_xy_plane.y, transform_point(result, first_point_first_xy_plane).y, EPSILON)
      << "Expected first point in second coordinate system to be (" << first_point_second_xy_plane.x << ", "
      << first_point_second_xy_plane.y << ") but got (" << transform_point(result, first_point_first_xy_plane).x << ", "
      << transform_point(result, first_point_first_xy_plane).y << ").";
  EXPECT_NEAR(second_point_second_xy_plane.x, transform_point(result, second_point_first_xy_plane).x, EPSILON)
      << "Expected second point in second coordinate system to be (" << second_point_second_xy_plane.x << ", "
      << second_point_second_xy_plane.y << ") but got (" << transform_point(result, second_point_first_xy_plane).x
      << ", " << transform_point(result, second_point_first_xy_plane).y << ").";
  EXPECT_NEAR(second_point_second_xy_plane.y, transform_point(result, second_point_first_xy_plane).y, EPSILON)
      << "Expected second point in second coordinate system to be (" << second_point_second_xy_plane.x << ", "
      << second_point_second_xy_plane.y << ") but got (" << transform_point(result, second_point_first_xy_plane).x
      << ", " << transform_point(result, second_point_first_xy_plane).y << ").";
  EXPECT_NEAR(arbitrary_point_second_xy_plane.x, transform_point(result, arbitrary_point_first_xy_plane).x, EPSILON)
      << "Expected arbitrary point in second coordinate system to be (" << arbitrary_point_second_xy_plane.x << ", "
      << arbitrary_point_second_xy_plane.y << ") but got (" << transform_point(result, arbitrary_point_first_xy_plane).x
      << ", " << transform_point(result, arbitrary_point_first_xy_plane).y << ").";
  EXPECT_NEAR(arbitrary_point_second_xy_plane.y, transform_point(result, arbitrary_point_first_xy_plane).y, EPSILON)
      << "Expected arbitrary point in second coordinate system to be (" << arbitrary_point_second_xy_plane.x << ", "
      << arbitrary_point_second_xy_plane.y << ") but got (" << transform_point(result, arbitrary_point_first_xy_plane).x
      << ", " << transform_point(result, arbitrary_point_first_xy_plane).y << ").";
}

TEST_F(CoordinateSystemTestPointsInQuadrantI, NonIdenticalOriginTestminus88Rotation) {
  double rotation = degrees_to_radians(-88.0);
  Point<double> displacement_vector{ 1, 2.7 };
  displacement_vector = rotate(displacement_vector, rotation);

  const Point<double> first_point_second_xy_plane{ 3.8024445500782, -2.869044343258 };
  const Point<double> second_point_second_xy_plane{ 5.7663267074138, -1.7998545228339 };
  const Point<double> arbitrary_point_second_xy_plane{ 8.8691976785786, -4.6933285137837 };

  auto result = affine_transform_from_points_only_translation_and_rotation(
      first_point_first_xy_plane, second_point_first_xy_plane, first_point_second_xy_plane,
      second_point_second_xy_plane);
  EXPECT_NEAR(rotation_of(result), rotation, EPSILON)
      << "Expected rotation to be " << rotation << " for points: (" << first_point_first_xy_plane.x << ", "
      << first_point_first_xy_plane.y << ") and (" << second_point_first_xy_plane.x << ", "
      << second_point_first_xy_plane.y << ") to (" << first_point_second_xy_plane.x << ", "
      << first_point_second_xy_plane.y << ") and (" << second_point_second_xy_plane.x << ", "
      << second_point_second_xy_plane.y << ").";
  EXPECT_NEAR(result.tx, displacement_vector.x, EPSILON)
      << "Expected displacement vector x to be " << displacement_vector.x << " for points: ("
      << first_point_first_xy_plane.x << ", " << first_point_first_xy_plane.y << ") and ("
      << second_point_first_xy_plane.x << ", " << second_point_first_xy_plane.y << ") to ("
      << first_point_second_xy_plane.x << ", " << first_point_second_xy_plane.y << ") and ("
      << second_point_second_xy_plane.x << ", " << second_point_second_xy_plane.y << ").";
  EXPECT_NEAR(result.ty, displacement_vector.y, EPSILON)
      << "Expected displacement vector y to be " << displacement_vector.y << " for points: ("
      << first_point_first_xy_plane.x << ", " << first_point_first_xy_plane.y << ") and ("
      << second_point_first_xy_plane.x << ", " << second_point_first_xy_plane.y << ") to ("
      << first_point_second_xy_plane.x << ", " << first_point_second_xy_plane.y << ") and ("
      << second_point_second_xy_plane.x << ", " << second_point_second_xy_plane.y << ").";
  EXPECT_NEAR(first_point_second_xy_plane.x, transform_point(result, first_point_first_xy_plane).x, EPSILON)
      << "Expected first point in second coordinate system to be (" << first_point_second_xy_plane.x << ", "
      << first_point_second_xy_plane.y << ") but got (" << transform_point(result, first_point_first_xy_plane).x << ", "
      << transform_point(result, first_point_first_xy_plane).y << ").";
  EXPECT_NEAR(first_point_second_xy_plane.y, transform_point(result, first_point_first_xy_plane).y, EPSILON)
      << "Expected first point in second coordinate system to be (" << first_point_second_xy_plane.x << ", "
      << first_point_second_xy_plane.y << ") but got (" << transform_point(result, first_point_first_xy_plane).x << ", "
      << transform_point(result, first_point_first_xy_plane).y << ").";
  EXPECT_NEAR(second_point_second_xy_plane.x, transform_point(result, second_point_first_xy_plane).x, EPSILON)
      << "Expected second point in second coordinate system to be (" << second_point_second_xy_plane.x << ", "
      << second_point_second_xy_plane.y << ") but got (" << transform_point(result, second_point_first_xy_plane).x
      << ", " << transform_point(result, second_point_first_xy_plane).y << ").";
  EXPECT_NEAR(second_point_second_xy_plane.y, transform_point(result, second_point_first_xy_plane).y, EPSILON)
      << "Expected second point in second coordinate system to be (" << second_point_second_xy_plane.x << ", "
      << second_point_second_xy_plane.y << ") but got (" << transform_point(result, second_point_first_xy_plane).x
      << ", " << transform_point(result, second_point_first_xy_plane).y << ").";
  EXPECT_NEAR(arbitrary_point_second_xy_plane.x, transform_point(result, arbitrary_point_first_xy_plane).x, EPSILON)
      << "Expected arbitrary point in second coordinate system to be (" << arbitrary_point_second_xy_plane.x << ", "
      << arbitrary_point_second_xy_plane.y << ") but got (" << transform_point(result, arbitrary_point_first_xy_plane).x
      << ", " << transform_point(result, arbitrary_point_first_xy_plane).y << ").";
  EXPECT_NEAR(arbitrary_point_second_xy_plane.y, transform_point(result, arbitrary_point_first_xy_plane).y, EPSILON)
      << "Expected arbitrary point in second coordinate system to be (" << arbitrary_point_second_xy_plane.x << ", "
      << arbitrary_point_second_xy_plane.y << ") but got (" << transform_point(result, arbitrary_point_first_xy_plane).x
      << ", " << transform_point(result, arbitrary_point_first_xy_plane).y << ").";
}

TEST_F(CoordinateSystemTestPointsInQuadrantI, NonIdenticalOriginTestminus114Rotation) {
  double rotation = degrees_to_radians(-114.0);
  Point<double> displacement_vector{ 3.2, 3.0 };
  displacement_vector = rotate(displacement_vector, rotation);

  const Point<double> first_point_second_xy_plane{ 1.5391512865762, -6.3773829520447 };
  const Point<double> second_point_second_xy_plane{ 3.7729788449372, -6.2773107805537 };
  const Point<double> arbitrary_point_second_xy_plane{ 5.2934052886376, -10.2381570827089 };

  auto result = affine_transform_from_points_only_translation_and_rotation(
      first_point_first_xy_plane, second_point_first_xy_plane, first_point_second_xy_plane,
      second_point_second_xy_plane);
  EXPECT_NEAR(rotation_of(result), rotation, EPSILON)
      << "Expected rotation to be " << rotation << " for points: (" << first_point_first_xy_plane.x << ", "
      << first_point_first_xy_plane.y << ") and (" << second_point_first_xy_plane.x << ", "
      << second_point_first_xy_plane.y << ") to (" << first_point_second_xy_plane.x << ", "
      << first_point_second_xy_plane.y << ") and (" << second_point_second_xy_plane.x << ", "
      << second_point_second_xy_plane.y << ").";
  EXPECT_NEAR(result.tx, displacement_vector.x, EPSILON)
      << "Expected displacement vector x to be " << displacement_vector.x << " for points: ("
      << first_point_first_xy_plane.x << ", " << first_point_first_xy_plane.y << ") and ("
      << second_point_first_xy_plane.x << ", " << second_point_first_xy_plane.y << ") to ("
      << first_point_second_xy_plane.x << ", " << first_point_second_xy_plane.y << ") and ("
      << second_point_second_xy_plane.x << ", " << second_point_second_xy_plane.y << ").";
  EXPECT_NEAR(result.ty, displacement_vector.y, EPSILON)
      << "Expected displacement vector y to be " << displacement_vector.y << " for points: ("
      << first_point_first_xy_plane.x << ", " << first_point_first_xy_plane.y << ") and ("
      << second_point_first_xy_plane.x << ", " << second_point_first_xy_plane.y << ") to ("
      << first_point_second_xy_plane.x << ", " << first_point_second_xy_plane.y << ") and ("
      << second_point_second_xy_plane.x << ", " << second_point_second_xy_plane.y << ").";
  EXPECT_NEAR(first_point_second_xy_plane.x, transform_point(result, first_point_first_xy_plane).x, EPSILON)
      << "Expected first point in second coordinate system to be (" << first_point_second_xy_plane.x << ", "
      << first_point_second_xy_plane.y << ") but got (" << transform_point(result, first_point_first_xy_plane).x << ", "
      << transform_point(result, first_point_first_xy_plane).y << ").";
  EXPECT_NEAR(first_point_second_xy_plane.y, transform_point(result, first_point_first_xy_plane).y, EPSILON)
      << "Expected first point in second coordinate system to be (" << first_point_second_xy_plane.x << ", "
      << first_point_second_xy_plane.y << ") but got (" << transform_point(result, first_point_first_xy_plane).x << ", "
      << transform_point(result, first_point_first_xy_plane).y << ").";
  EXPECT_NEAR(second_point_second_xy_plane.x, transform_point(result, second_point_first_xy_plane).x, EPSILON)
      << "Expected second point in second coordinate system to be (" << second_point_second_xy_plane.x << ", "
      << second_point_second_xy_plane.y << ") but got (" << transform_point(result, second_point_first_xy_plane).x
      << ", " << transform_point(result, second_point_first_xy_plane).y << ").";
  EXPECT_NEAR(second_point_second_xy_plane.y, transform_point(result, second_point_first_xy_plane).y, EPSILON)
      << "Expected second point in second coordinate system to be (" << second_point_second_xy_plane.x << ", "
      << second_point_second_xy_plane.y << ") but got (" << transform_point(result, second_point_first_xy_plane).x
      << ", " << transform_point(result, second_point_first_xy_plane).y << ").";
  EXPECT_NEAR(arbitrary_point_second_xy_plane.x, transform_point(result, arbitrary_point_first_xy_plane).x, EPSILON)
      << "Expected arbitrary point in second coordinate system to be (" << arbitrary_point_second_xy_plane.x << ", "
      << arbitrary_point_second_xy_plane.y << ") but got (" << transform_point(result, arbitrary_point_first_xy_plane).x
      << ", " << transform_point(result, arbitrary_point_first_xy_plane).y << ").";
  EXPECT_NEAR(arbitrary_point_second_xy_plane.y, transform_point(result, arbitrary_point_first_xy_plane).y, EPSILON)
      << "Expected arbitrary point in second coordinate system to be (" << arbitrary_point_second_xy_plane.x << ", "
      << arbitrary_point_second_xy_plane.y << ") but got (" << transform_point(result, arbitrary_point_first_xy_plane).x
      << ", " << transform_point(result, arbitrary_point_first_xy_plane).y << ").";
}

TEST_F(CoordinateSystemTestPointsInQuadrantI, NonIdenticalOriginTestminus194Rotation) {
  double rotation = degrees_to_radians(-194.0);
  Point<double> displacement_vector{ 3.2, -1.5 };
  displacement_vector = rotate(displacement_vector, rotation);

  const Point<double> first_point_second_xy_plane{ -4.9245768288353, 1.7431417202563 };
  const Point<double> second_point_second_xy_plane{ -4.4381248937587, -0.4393716278954 };
  const Point<double> arbitrary_point_second_xy_plane{ -8.0747777593857, -2.6244931199244 };

  auto result = affine_transform_from_points_only_translation_and_rotation(
      first_point_first_xy_plane, second_point_first_xy_plane, first_point_second_xy_plane,
      second_point_second_xy_plane);
  EXPECT_NEAR(rotation_of(result), rotation, EPSILON)
      << "Expected rotation to be " << rotation << " for points: (" << first_point_first_xy_plane.x << ", "
      << first_point_first_xy_plane.y << ") and (" << second_point_first_xy_plane.x << ", "
      << second_point_first_xy_plane.y << ") to (" << first_point_second_xy_plane.x << ", "
      << first_point_second_xy_plane.y << ") and (" << second_point_second_xy_plane.x << ", "
      << second_point_second_xy_plane.y << ").";
  EXPECT_NEAR(result.tx, displacement_vector.x, EPSILON)
      << "Expected displacement vector x to be " << displacement_vector.x << " for points: ("
      << first_point_first_xy_plane.x << ", " << first_point_first_xy_plane.y << ") and ("
      << second_point_first_xy_plane.x << ", " << second_point_first_xy_plane.y << ") to ("
      << first_point_second_xy_plane.x << ", " << first_point_second_xy_plane.y << ") and ("
      << second_point_second_xy_plane.x << ", " << second_point_second_xy_plane.y << ").";
  EXPECT_NEAR(result.ty, displacement_vector.y, EPSILON)
      << "Expected displacement vector y to be " << displacement_vector.y << " for points: ("
      << first_point_first_xy_plane.x << ", " << first_point_first_xy_plane.y << ") and ("
      << second_point_first_xy_plane.x << ", " << second_point_first_xy_plane.y << ") to ("
      << first_point_second_xy_plane.x << ", " << first_point_second_xy_plane.y << ") and ("
      << second_point_second_xy_plane.x << ", " << second_point_second_xy_plane.y << ").";
  EXPECT_NEAR(first_point_second_xy_plane.x, transform_point(result, first_point_first_xy_plane).x, EPSILON)
      << "Expected first point in second coordinate system to be (" << first_point_second_xy_plane.x << ", "
      << first_point_second_xy_plane.y << ") but got (" << transform_point(result, first_point_first_xy_plane).x << ", "
      << transform_point(result, first_point_first_xy_plane).y << ").";
  EXPECT_NEAR(first_point_second_xy_plane.y, transform_point(result, first_point_first_xy_plane).y, EPSILON)
      << "Expected first point in second coordinate system to be (" << first_point_second_xy_plane.x << ", "
      << first_point_second_xy_plane.y << ") but got (" << transform_point(result, first_point_first_xy_plane).x << ", "
      << transform_point(result, first_point_first_xy_plane).y << ").";
  EXPECT_NEAR(second_point_second_xy_plane.x, transform_point(result, second_point_first_xy_plane).x, EPSILON)
      << "Expected second point in second coordinate system to be (" << second_point_second_xy_plane.x << ", "
      << second_point_second_xy_plane.y << ") but got (" << transform_point(result, second_point_first_xy_plane).x
      << ", " << transform_point(result, second_point_first_xy_plane).y << ").";
  EXPECT_NEAR(second_point_second_xy_plane.y, transform_point(result, second_point_first_xy_plane).y, EPSILON)
      << "Expected second point in second coordinate system to be (" << second_point_second_xy_plane.x << ", "
      << second_point_second_xy_plane.y << ") but got (" << transform_point(result, second_point_first_xy_plane).x
      << ", " << transform_point(result, second_point_first_xy_plane).y << ").";
  EXPECT_NEAR(arbitrary_point_second_xy_plane.x, transform_point(result, arbitrary_point_first_xy_plane).x, EPSILON)
      << "Expected arbitrary point in second coordinate system to be (" << arbitrary_point_second_xy_plane.x << ", "
      << arbitrary_point_second_xy_plane.y << ") but got (" << transform_point(result, arbitrary_point_first_xy_plane).x
      << ", " << transform_point(result, arbitrary_point_first_xy_plane).y << ").";
  EXPECT_NEAR(arbitrary_point_second_xy_plane.y, transform_point(result, arbitrary_point_first_xy_plane).y, EPSILON)
      << "Expected arbitrary point in second coordinate system to be (" << arbitrary_point_second_xy_plane.x << ", "
      << arbitrary_point_second_xy_plane.y << ") but got (" << transform_point(result, arbitrary_point_first_xy_plane).x
      << ", " << transform_point(result, arbitrary_point_first_xy_plane).y << ").";
}

TEST_F(CoordinateSystemTestPointsInQuadrantI, NonIdenticalOriginTestminus98Rotation) {
  double rotation = degrees_to_radians(-98.0);
  Point<double> displacement_vector{ -1.7, 1.5 };
  displacement_vector = rotate(displacement_vector, rotation);

  const Point<double> first_point_second_xy_plane{ 2.4339182415659, -0.6450131730226 };
  const Point<double> second_point_second_xy_plane{ 4.5536274800091, 0.0669086937988 };
  const Point<double> arbitrary_point_second_xy_plane{ 7.1069123833536, -3.3214148153061 };

  auto result = affine_transform_from_points_only_translation_and_rotation(
      first_point_first_xy_plane, second_point_first_xy_plane, first_point_second_xy_plane,
      second_point_second_xy_plane);
  EXPECT_NEAR(rotation_of(result), rotation, EPSILON)
      << "Expected rotation to be " << rotation << " for points: (" << first_point_first_xy_plane.x << ", "
      << first_point_first_xy_plane.y << ") and (" << second_point_first_xy_plane.x << ", "
      << second_point_first_xy_plane.y << ") to (" << first_point_second_xy_plane.x << ", "
      << first_point_second_xy_plane.y << ") and (" << second_point_second_xy_plane.x << ", "
      << second_point_second_xy_plane.y << ").";
  EXPECT_NEAR(result.tx, displacement_vector.x, EPSILON)
      << "Expected displacement vector x to be " << displacement_vector.x << " for points: ("
      << first_point_first_xy_plane.x << ", " << first_point_first_xy_plane.y << ") and ("
      << second_point_first_xy_plane.x << ", " << second_point_first_xy_plane.y << ") to ("
      << first_point_second_xy_plane.x << ", " << first_point_second_xy_plane.y << ") and ("
      << second_point_second_xy_plane.x << ", " << second_point_second_xy_plane.y << ").";
  EXPECT_NEAR(result.ty, displacement_vector.y, EPSILON)
      << "Expected displacement vector y to be " << displacement_vector.y << " for points: ("
      << first_point_first_xy_plane.x << ", " << first_point_first_xy_plane.y << ") and ("
      << second_point_first_xy_plane.x << ", " << second_point_first_xy_plane.y << ") to ("
      << first_point_second_xy_plane.x << ", " << first_point_second_xy_plane.y << ") and ("
      << second_point_second_xy_plane.x << ", " << second_point_second_xy_plane.y << ").";
  EXPECT_NEAR(first_point_second_xy_plane.x, transform_point(result, first_point_first_xy_plane).x, EPSILON)
      << "Expected first point in second coordinate system to be (" << first_point_second_xy_plane.x << ", "
      << first_point_second_xy_plane.y << ") but got (" << transform_point(result, first_point_first_xy_plane).x << ", "
      << transform_point(result, first_point_first_xy_plane).y << ").";
  EXPECT_NEAR(first_point_second_xy_plane.y, transform_point(result, first_point_first_xy_plane).y, EPSILON)
      << "Expected first point in second coordinate system to be (" << first_point_second_xy_plane.x << ", "
      << first_point_second_xy_plane.y << ") but got (" << transform_point(result, first_point_first_xy_plane).x << ", "
      << transform_point(result, first_point_first_xy_plane).y << ").";
  EXPECT_NEAR(second_point_second_xy_plane.x, transform_point(result, second_point_first_xy_plane).x, EPSILON)
      << "Expected second point in second coordinate system to be (" << second_point_second_xy_plane.x << ", "
      << second_point_second_xy_plane.y << ") but got (" << transform_point(result, second_point_first_xy_plane).x
      << ", " << transform_point(result, second_point_first_xy_plane).y << ").";
  EXPECT_NEAR(second_point_second_xy_plane.y, transform_point(result, second_point_first_xy_plane).y, EPSILON)
      << "Expected second point in second coordinate system to be (" << second_point_second_xy_plane.x << ", "
      << second_point_second_xy_plane.y << ") but got (" << transform_point(result, second_point_first_xy_plane).x
      << ", " << transform_point(result, second_point_first_xy_plane).y << ").";
  EXPECT_NEAR(arbitrary_point_second_xy_plane.x, transform_point(result, arbitrary_point_first_xy_plane).x, EPSILON)
      << "Expected arbitrary point in second coordinate system to be (" << arbitrary_point_second_xy_plane.x << ", "
      << arbitrary_point_second_xy_plane.y << ") but got (" << transform_point(result, arbitrary_point_first_xy_plane).x
      << ", " << transform_point(result, arbitrary_point_first_xy_plane).y << ").";
  EXPECT_NEAR(arbitrary_point_second_xy_plane.y, transform_point(result, arbitrary_point_first_xy_plane).y, EPSILON)
      << "Expected arbitrary point in second coordinate system to be (" << arbitrary_point_second_xy_plane.x << ", "
      << arbitrary_point_second_xy_plane.y << ") but got (" << transform_point(result, arbitrary_point_first_xy_plane).x
      << ", " << transform_point(result, arbitrary_point_first_xy_plane).y << ").";
}

TEST_F(CoordinateSystemTestPointsInQuadrantI, NonIdenticalOriginTestminus39Rotation) {
  double rotation = degrees_to_radians(-39.0);
  Point<double> displacement_vector{ 0, 3.1 };
  displacement_vector = rotate(displacement_vector, rotation);

  const Point<double> first_point_second_xy_plane{ 4.1345055262183, 1.9276576598739 };
  const Point<double> second_point_second_xy_plane{ 4.616000346861, 4.1112699738377 };
  const Point<double> arbitrary_point_second_xy_plane{ 8.8353994043814, 4.5547466850591 };

  auto result = affine_transform_from_points_only_translation_and_rotation(
      first_point_first_xy_plane, second_point_first_xy_plane, first_point_second_xy_plane,
      second_point_second_xy_plane);
  EXPECT_NEAR(rotation_of(result), rotation, EPSILON)
      << "Expected rotation to be " << rotation << " for points: (" << first_point_first_xy_plane.x << ", "
      << first_point_first_xy_plane.y << ") and (" << second_point_first_xy_plane.x << ", "
      << second_point_first_xy_plane.y << ") to (" << first_point_second_xy_plane.x << ", "
      << first_point_second_xy_plane.y << ") and (" << second_point_second_xy_plane.x << ", "
      << second_point_second_xy_plane.y << ").";
  EXPECT_NEAR(result.tx, displacement_vector.x, EPSILON)
      << "Expected displacement vector x to be " << displacement_vector.x << " for points: ("
      << first_point_first_xy_plane.x << ", " << first_point_first_xy_plane.y << ") and ("
      << second_point_first_xy_plane.x << ", " << second_point_first_xy_plane.y << ") to ("
      << first_point_second_xy_plane.x << ", " << first_point_second_xy_plane.y << ") and ("
      << second_point_second_xy_plane.x << ", " << second_point_second_xy_plane.y << ").";
  EXPECT_NEAR(result.ty, displacement_vector.y, EPSILON)
      << "Expected displacement vector y to be " << displacement_vector.y << " for points: ("
      << first_point_first_xy_plane.x << ", " << first_point_first_xy_plane.y << ") and ("
      << second_point_first_xy_plane.x << ", " << second_point_first_xy_plane.y << ") to ("
      << first_point_second_xy_plane.x << ", " << first_point_second_xy_plane.y << ") and ("
      << second_point_second_xy_plane.x << ", " << second_point_second_xy_plane.y << ").";
  EXPECT_NEAR(first_point_second_xy_plane.x, transform_point(result, first_point_first_xy_plane).x, EPSILON)
      << "Expected first point in second coordinate system to be (" << first_point_second_xy_plane.x << ", "
      << first_point_second_xy_plane.y << ") but got (" << transform_point(result, first_point_first_xy_plane).x << ", "
      << transform_point(result, first_point_first_xy_plane).y << ").";
  EXPECT_NEAR(first_point_second_xy_plane.y, transform_point(result, first_point_first_xy_plane).y, EPSILON)
      << "Expected first point in second coordinate system to be (" << first_point_second_xy_plane.x << ", "
      << first_point_second_xy_plane.y << ") but got (" << transform_point(result, first_point_first_xy_plane).x << ", "
      << transform_point(result, first_point_first_xy_plane).y << ").";
  EXPECT_NEAR(second_point_second_xy_plane.x, transform_point(result, second_point_first_xy_plane).x, EPSILON)
      << "Expected second point in second coordinate system to be (" << second_point_second_xy_plane.x << ", "
      << second_point_second_xy_plane.y << ") but got (" << transform_point(result, second_point_first_xy_plane).x
      << ", " << transform_point(result, second_point_first_xy_plane).y << ").";
  EXPECT_NEAR(second_point_second_xy_plane.y, transform_point(result, second_point_first_xy_plane).y, EPSILON)
      << "Expected second point in second coordinate system to be (" << second_point_second_xy_plane.x << ", "
      << second_point_second_xy_plane.y << ") but got (" << transform_point(result, second_point_first_xy_plane).x
      << ", " << transform_point(result, second_point_first_xy_plane).y << ").";
  EXPECT_NEAR(arbitrary_point_second_xy_plane.x, transform_point(result, arbitrary_point_first_xy_plane).x, EPSILON)
      << "Expected arbitrary point in second coordinate system to be (" << arbitrary_point_second_xy_plane.x << ", "
      << arbitrary_point_second_xy_plane.y << ") but got (" << transform_point(result, arbitrary_point_first_xy_plane).x
      << ", " << transform_point(result, arbitrary_point_first_xy_plane).y << ").";
  EXPECT_NEAR(arbitrary_point_second_xy_plane.y, transform_point(result, arbitrary_point_first_xy_plane).y, EPSILON)
      << "Expected arbitrary point in second coordinate system to be (" << arbitrary_point_second_xy_plane.x << ", "
      << arbitrary_point_second_xy_plane.y << ") but got (" << transform_point(result, arbitrary_point_first_xy_plane).x
      << ", " << transform_point(result, arbitrary_point_first_xy_plane).y << ").";
}

class CoordinateSystemTestPointsInQuadrantII : public ::testing::Test {
protected:
  static constexpr Point<double> first_point_first_xy_plane{ -2.659771305149, 2.3996855522746 };
  static constexpr Point<double> second_point_first_xy_plane{ -1.5170400705159, 2.1854234457809 };
  static constexpr Point<double> arbitrary_point_first_xy_plane{ 4.3394575069784, -1.7605703488113 };
};

TEST_F(CoordinateSystemTestPointsInQuadrantII, NonIdenticalOriginTestminus39Rotation) {
  double rotation = degrees_to_radians(-39.0);
  Point<double> displacement_vector{ 0, 3.1 };
  displacement_vector = rotate(displacement_vector, rotation);

  const Point<double> first_point_second_xy_plane{ 1.394033734213, 5.947906734093 };
  const Point<double> second_point_second_xy_plane{ 2.147263185593, 5.062249735894 };
  const Point<double> arbitrary_point_second_xy_plane{ 4.2153222683322, -1.6899767511588 };

  auto result = affine_transform_from_points_only_translation_and_rotation(
      first_point_first_xy_plane, second_point_first_xy_plane, first_point_second_xy_plane,
      second_point_second_xy_plane);
  EXPECT_NEAR(rotation_of(result), rotation, EPSILON)
      << "Expected rotation to be " << rotation << " for points: (" << first_point_first_xy_plane.x << ", "
      << first_point_first_xy_plane.y << ") and (" << second_point_first_xy_plane.x << ", "
      << second_point_first_xy_plane.y << ") to (" << first_point_second_xy_plane.x << ", "
      << first_point_second_xy_plane.y << ") and (" << second_point_second_xy_plane.x << ", "
      << second_point_second_xy_plane.y << ").";
  EXPECT_NEAR(result.tx, displacement_vector.x, EPSILON)
      << "Expected displacement vector x to be " << displacement_vector.x << " for points: ("
      << first_point_first_xy_plane.x << ", " << first_point_first_xy_plane.y << ") and ("
      << second_point_first_xy_plane.x << ", " << second_point_first_xy_plane.y << ") to ("
      << first_point_second_xy_plane.x << ", " << first_point_second_xy_plane.y << ") and ("
      << second_point_second_xy_plane.x << ", " << second_point_second_xy_plane.y << ").";
  EXPECT_NEAR(result.ty, displacement_vector.y, EPSILON)
      << "Expected displacement vector y to be " << displacement_vector.y << " for points: ("
      << first_point_first_xy_plane.x << ", " << first_point_first_xy_plane.y << ") and ("
      << second_point_first_xy_plane.x << ", " << second_point_first_xy_plane.y << ") to ("
      << first_point_second_xy_plane.x << ", " << first_point_second_xy_plane.y << ") and ("
      << second_point_second_xy_plane.x << ", " << second_point_second_xy_plane.y << ").";
  EXPECT_NEAR(first_point_second_xy_plane.x, transform_point(result, first_point_first_xy_plane).x, EPSILON)
      << "Expected first point in second coordinate system to be (" << first_point_second_xy_plane.x << ", "
      << first_point_second_xy_plane.y << ") but got (" << transform_point(result, first_point_first_xy_plane).x << ", "
      << transform_point(result, first_point_first_xy_plane).y << ").";
  EXPECT_NEAR(first_point_second_xy_plane.y, transform_point(result, first_point_first_xy_plane).y, EPSILON)
      << "Expected first point in second coordinate system to be (" << first_point_second_xy_plane.x << ", "
      << first_point_second_xy_plane.y << ") but got (" << transform_point(result, first_point_first_xy_plane).x << ", "
      << transform_point(result, first_point_first_xy_plane).y << ").";
  EXPECT_NEAR(second_point_second_xy_plane.x, transform_point(result, second_point_first_xy_plane).x, EPSILON)
      << "Expected second point in second coordinate system to be (" << second_point_second_xy_plane.x << ", "
      << second_point_second_xy_plane.y << ") but got (" << transform_point(result, second_point_first_xy_plane).x
      << ", " << transform_point(result, second_point_first_xy_plane).y << ").";
  EXPECT_NEAR(second_point_second_xy_plane.y, transform_point(result, second_point_first_xy_plane).y, EPSILON)
      << "Expected second point in second coordinate system to be (" << second_point_second_xy_plane.x << ", "
      << second_point_second_xy_plane.y << ") but got (" << transform_point(result, second_point_first_xy_plane).x
      << ", " << transform_point(result, second_point_first_xy_plane).y << ").";
  EXPECT_NEAR(arbitrary_point_second_xy_plane.x, transform_point(result, arbitrary_point_first_xy_plane).x, EPSILON)
      << "Expected arbitrary point in second coordinate system to be (" << arbitrary_point_second_xy_plane.x << ", "
      << arbitrary_point_second_xy_plane.y << ") but got (" << transform_point(result, arbitrary_point_first_xy_plane).x
      << ", " << transform_point(result, arbitrary_point_first_xy_plane).y << ").";
  EXPECT_NEAR(arbitrary_point_second_xy_plane.y, transform_point(result, arbitrary_point_first_xy_plane).y, EPSILON)
      << "Expected arbitrary point in second coordinate system to be (" << arbitrary_point_second_xy_plane.x << ", "
      << arbitrary_point_second_xy_plane.y << ") but got (" << transform_point(result, arbitrary_point_first_xy_plane).x
      << ", " << transform_point(result, arbitrary_point_first_xy_plane).y << ").";
}

TEST_F(CoordinateSystemTestPointsInQuadrantII, NonIdenticalOriginTestminus82Rotation) {
  double rotation = degrees_to_radians(-82.0);
  Point<double> displacement_vector{ -1.8, -0.3 };
  displacement_vector = rotate(displacement_vector, rotation);

  const Point<double> first_point_second_xy_plane{ 1.4585713547053, 4.7085888667301 };
  const Point<double> second_point_second_xy_plane{ 1.4054318817911, 3.5471590921404 };
  const Point<double> arbitrary_point_second_xy_plane{ -1.687092843821, -2.8015196462772 };

  auto result = affine_transform_from_points_only_translation_and_rotation(
      first_point_first_xy_plane, second_point_first_xy_plane, first_point_second_xy_plane,
      second_point_second_xy_plane);
  EXPECT_NEAR(rotation_of(result), rotation, EPSILON)
      << "Expected rotation to be " << rotation << " for points: (" << first_point_first_xy_plane.x << ", "
      << first_point_first_xy_plane.y << ") and (" << second_point_first_xy_plane.x << ", "
      << second_point_first_xy_plane.y << ") to (" << first_point_second_xy_plane.x << ", "
      << first_point_second_xy_plane.y << ") and (" << second_point_second_xy_plane.x << ", "
      << second_point_second_xy_plane.y << ").";
  EXPECT_NEAR(result.tx, displacement_vector.x, EPSILON)
      << "Expected displacement vector x to be " << displacement_vector.x << " for points: ("
      << first_point_first_xy_plane.x << ", " << first_point_first_xy_plane.y << ") and ("
      << second_point_first_xy_plane.x << ", " << second_point_first_xy_plane.y << ") to ("
      << first_point_second_xy_plane.x << ", " << first_point_second_xy_plane.y << ") and ("
      << second_point_second_xy_plane.x << ", " << second_point_second_xy_plane.y << ").";
  EXPECT_NEAR(result.ty, displacement_vector.y, EPSILON)
      << "Expected displacement vector y to be " << displacement_vector.y << " for points: ("
      << first_point_first_xy_plane.x << ", " << first_point_first_xy_plane.y << ") and ("
      << second_point_first_xy_plane.x << ", " << second_point_first_xy_plane.y << ") to ("
      << first_point_second_xy_plane.x << ", " << first_point_second_xy_plane.y << ") and ("
      << second_point_second_xy_plane.x << ", " << second_point_second_xy_plane.y << ").";
  EXPECT_NEAR(first_point_second_xy_plane.x, transform_point(result, first_point_first_xy_plane).x, EPSILON)
      << "Expected first point in second coordinate system to be (" << first_point_second_xy_plane.x << ", "
      << first_point_second_xy_plane.y << ") but got (" << transform_point(result, first_point_first_xy_plane).x << ", "
      << transform_point(result, first_point_first_xy_plane).y << ").";
  EXPECT_NEAR(first_point_second_xy_plane.y, transform_point(result, first_point_first_xy_plane).y, EPSILON)
      << "Expected first point in second coordinate system to be (" << first_point_second_xy_plane.x << ", "
      << first_point_second_xy_plane.y << ") but got (" << transform_point(result, first_point_first_xy_plane).x << ", "
      << transform_point(result, first_point_first_xy_plane).y << ").";
  EXPECT_NEAR(second_point_second_xy_plane.x, transform_point(result, second_point_first_xy_plane).x, EPSILON)
      << "Expected second point in second coordinate system to be (" << second_point_second_xy_plane.x << ", "
      << second_point_second_xy_plane.y << ") but got (" << transform_point(result, second_point_first_xy_plane).x
      << ", " << transform_point(result, second_point_first_xy_plane).y << ").";
  EXPECT_NEAR(second_point_second_xy_plane.y, transform_point(result, second_point_first_xy_plane).y, EPSILON)
      << "Expected second point in second coordinate system to be (" << second_point_second_xy_plane.x << ", "
      << second_point_second_xy_plane.y << ") but got (" << transform_point(result, second_point_first_xy_plane).x
      << ", " << transform_point(result, second_point_first_xy_plane).y << ").";
  EXPECT_NEAR(arbitrary_point_second_xy_plane.x, transform_point(result, arbitrary_point_first_xy_plane).x, EPSILON)
      << "Expected arbitrary point in second coordinate system to be (" << arbitrary_point_second_xy_plane.x << ", "
      << arbitrary_point_second_xy_plane.y << ") but got (" << transform_point(result, arbitrary_point_first_xy_plane).x
      << ", " << transform_point(result, arbitrary_point_first_xy_plane).y << ").";
  EXPECT_NEAR(arbitrary_point_second_xy_plane.y, transform_point(result, arbitrary_point_first_xy_plane).y, EPSILON)
      << "Expected arbitrary point in second coordinate system to be (" << arbitrary_point_second_xy_plane.x << ", "
      << arbitrary_point_second_xy_plane.y << ") but got (" << transform_point(result, arbitrary_point_first_xy_plane).x
      << ", " << transform_point(result, arbitrary_point_first_xy_plane).y << ").";
}

TEST_F(CoordinateSystemTestPointsInQuadrantII, IdenticalOriginTestminus192Rotation) {
  double rotation = degrees_to_radians(-192.0);
  Point<double> displacement_vector{ -0.0, -0.0 };
  displacement_vector = rotate(displacement_vector, rotation);

  const Point<double> first_point_second_xy_plane{ 2.1027262400277, -2.9002442147151 };
  const Point<double> second_point_second_xy_plane{ 1.0295140214271, -2.4530770661773 };
  const Point<double> arbitrary_point_second_xy_plane{ -3.8785867909123, 2.6243216101205 };

  auto result = affine_transform_from_points_only_translation_and_rotation(
      first_point_first_xy_plane, second_point_first_xy_plane, first_point_second_xy_plane,
      second_point_second_xy_plane);
  EXPECT_NEAR(rotation_of(result), rotation, EPSILON)
      << "Expected rotation to be " << rotation << " for points: (" << first_point_first_xy_plane.x << ", "
      << first_point_first_xy_plane.y << ") and (" << second_point_first_xy_plane.x << ", "
      << second_point_first_xy_plane.y << ") to (" << first_point_second_xy_plane.x << ", "
      << first_point_second_xy_plane.y << ") and (" << second_point_second_xy_plane.x << ", "
      << second_point_second_xy_plane.y << ").";
  EXPECT_NEAR(result.tx, displacement_vector.x, EPSILON)
      << "Expected displacement vector x to be " << displacement_vector.x << " for points: ("
      << first_point_first_xy_plane.x << ", " << first_point_first_xy_plane.y << ") and ("
      << second_point_first_xy_plane.x << ", " << second_point_first_xy_plane.y << ") to ("
      << first_point_second_xy_plane.x << ", " << first_point_second_xy_plane.y << ") and ("
      << second_point_second_xy_plane.x << ", " << second_point_second_xy_plane.y << ").";
  EXPECT_NEAR(result.ty, displacement_vector.y, EPSILON)
      << "Expected displacement vector y to be " << displacement_vector.y << " for points: ("
      << first_point_first_xy_plane.x << ", " << first_point_first_xy_plane.y << ") and ("
      << second_point_first_xy_plane.x << ", " << second_point_first_xy_plane.y << ") to ("
      << first_point_second_xy_plane.x << ", " << first_point_second_xy_plane.y << ") and ("
      << second_point_second_xy_plane.x << ", " << second_point_second_xy_plane.y << ").";
  EXPECT_NEAR(first_point_second_xy_plane.x, transform_point(result, first_point_first_xy_plane).x, EPSILON)
      << "Expected first point in second coordinate system to be (" << first_point_second_xy_plane.x << ", "
      << first_point_second_xy_plane.y << ") but got (" << transform_point(result, first_point_first_xy_plane).x << ", "
      << transform_point(result, first_point_first_xy_plane).y << ").";
  EXPECT_NEAR(first_point_second_xy_plane.y, transform_point(result, first_point_first_xy_plane).y, EPSILON)
      << "Expected first point in second coordinate system to be (" << first_point_second_xy_plane.x << ", "
      << first_point_second_xy_plane.y << ") but got (" << transform_point(result, first_point_first_xy_plane).x << ", "
      << transform_point(result, first_point_first_xy_plane).y << ").";
  EXPECT_NEAR(second_point_second_xy_plane.x, transform_point(result, second_point_first_xy_plane).x, EPSILON)
      << "Expected second point in second coordinate system to be (" << second_point_second_xy_plane.x << ", "
      << second_point_second_xy_plane.y << ") but got (" << transform_point(result, second_point_first_xy_plane).x
      << ", " << transform_point(result, second_point_first_xy_plane).y << ").";
  EXPECT_NEAR(second_point_second_xy_plane.y, transform_point(result, second_point_first_xy_plane).y, EPSILON)
      << "Expected second point in second coordinate system to be (" << second_point_second_xy_plane.x << ", "
      << second_point_second_xy_plane.y << ") but got (" << transform_point(result, second_point_first_xy_plane).x
      << ", " << transform_point(result, second_point_first_xy_plane).y << ").";
  EXPECT_NEAR(arbitrary_point_second_xy_plane.x, transform_point(result, arbitrary_point_first_xy_plane).x, EPSILON)
      << "Expected arbitrary point in second coordinate system to be (" << arbitrary_point_second_xy_plane.x << ", "
      << arbitrary_point_second_xy_plane.y << ") but got (" << transform_point(result, arbitrary_point_first_xy_plane).x
      << ", " << transform_point(result, arbitrary_point_first_xy_plane).y << ").";
  EXPECT_NEAR(arbitrary_point_second_xy_plane.y, transform_point(result, arbitrary_point_first_xy_plane).y, EPSILON)
      << "Expected arbitrary point in second coordinate system to be (" << arbitrary_point_second_xy_plane.x << ", "
      << arbitrary_point_second_xy_plane.y << ") but got (" << transform_point(result, arbitrary_point_first_xy_plane).x
      << ", " << transform_point(result, arbitrary_point_first_xy_plane).y << ").";
}

TEST_F(CoordinateSystemTestPointsInQuadrantII, NonIdenticalOriginTestminus234Rotation) {
  double rotation = degrees_to_radians(-234.0);
  Point<double> displacement_vector{ 1.7, 1.7 };
  displacement_vector = rotate(displacement_vector, rotation);

  const Point<double> first_point_second_xy_plane{ -2.7525758646436, -3.1862060032425 };
  const Point<double> second_point_second_xy_plane{ -3.2509147462909, -2.1357769080992 };
  const Point<double> arbitrary_point_second_xy_plane{ -3.5009016129054, 4.9216261177084 };

  auto result = affine_transform_from_points_only_translation_and_rotation(
      first_point_first_xy_plane, second_point_first_xy_plane, first_point_second_xy_plane,
      second_point_second_xy_plane);
  EXPECT_NEAR(rotation_of(result), rotation, EPSILON)
      << "Expected rotation to be " << rotation << " for points: (" << first_point_first_xy_plane.x << ", "
      << first_point_first_xy_plane.y << ") and (" << second_point_first_xy_plane.x << ", "
      << second_point_first_xy_plane.y << ") to (" << first_point_second_xy_plane.x << ", "
      << first_point_second_xy_plane.y << ") and (" << second_point_second_xy_plane.x << ", "
      << second_point_second_xy_plane.y << ").";
  EXPECT_NEAR(result.tx, displacement_vector.x, EPSILON)
      << "Expected displacement vector x to be " << displacement_vector.x << " for points: ("
      << first_point_first_xy_plane.x << ", " << first_point_first_xy_plane.y << ") and ("
      << second_point_first_xy_plane.x << ", " << second_point_first_xy_plane.y << ") to ("
      << first_point_second_xy_plane.x << ", " << first_point_second_xy_plane.y << ") and ("
      << second_point_second_xy_plane.x << ", " << second_point_second_xy_plane.y << ").";
  EXPECT_NEAR(result.ty, displacement_vector.y, EPSILON)
      << "Expected displacement vector y to be " << displacement_vector.y << " for points: ("
      << first_point_first_xy_plane.x << ", " << first_point_first_xy_plane.y << ") and ("
      << second_point_first_xy_plane.x << ", " << second_point_first_xy_plane.y << ") to ("
      << first_point_second_xy_plane.x << ", " << first_point_second_xy_plane.y << ") and ("
      << second_point_second_xy_plane.x << ", " << second_point_second_xy_plane.y << ").";
  EXPECT_NEAR(first_point_second_xy_plane.x, transform_point(result, first_point_first_xy_plane).x, EPSILON)
      << "Expected first point in second coordinate system to be (" << first_point_second_xy_plane.x << ", "
      << first_point_second_xy_plane.y << ") but got (" << transform_point(result, first_point_first_xy_plane).x << ", "
      << transform_point(result, first_point_first_xy_plane).y << ").";
  EXPECT_NEAR(first_point_second_xy_plane.y, transform_point(result, first_point_first_xy_plane).y, EPSILON)
      << "Expected first point in second coordinate system to be (" << first_point_second_xy_plane.x << ", "
      << first_point_second_xy_plane.y << ") but got (" << transform_point(result, first_point_first_xy_plane).x << ", "
      << transform_point(result, first_point_first_xy_plane).y << ").";
  EXPECT_NEAR(second_point_second_xy_plane.x, transform_point(result, second_point_first_xy_plane).x, EPSILON)
      << "Expected second point in second coordinate system to be (" << second_point_second_xy_plane.x << ", "
      << second_point_second_xy_plane.y << ") but got (" << transform_point(result, second_point_first_xy_plane).x
      << ", " << transform_point(result, second_point_first_xy_plane).y << ").";
  EXPECT_NEAR(second_point_second_xy_plane.y, transform_point(result, second_point_first_xy_plane).y, EPSILON)
      << "Expected second point in second coordinate system to be (" << second_point_second_xy_plane.x << ", "
      << second_point_second_xy_plane.y << ") but got (" << transform_point(result, second_point_first_xy_plane).x
      << ", " << transform_point(result, second_point_first_xy_plane).y << ").";
  EXPECT_NEAR(arbitrary_point_second_xy_plane.x, transform_point(result, arbitrary_point_first_xy_plane).x, EPSILON)
      << "Expected arbitrary point in second coordinate system to be (" << arbitrary_point_second_xy_plane.x << ", "
      << arbitrary_point_second_xy_plane.y << ") but got (" << transform_point(result, arbitrary_point_first_xy_plane).x
      << ", " << transform_point(result, arbitrary_point_first_xy_plane).y << ").";
  EXPECT_NEAR(arbitrary_point_second_xy_plane.y, transform_point(result, arbitrary_point_first_xy_plane).y, EPSILON)
      << "Expected arbitrary point in second coordinate system to be (" << arbitrary_point_second_xy_plane.x << ", "
      << arbitrary_point_second_xy_plane.y << ") but got (" << transform_point(result, arbitrary_point_first_xy_plane).x
      << ", " << transform_point(result, arbitrary_point_first_xy_plane).y << ").";
}

TEST(EdgeCases, PointsOnYaxis) {
  constexpr Point<double> first_point_first_xy_plane{ 0.0, 2.0 };
  constexpr Point<double> second_point_first_xy_plane{ 0.0, 3.0 };
  constexpr Point<double> arbitrary_point_first_xy_plane{ 0.0, 4.0 };

  double rotation = degrees_to_radians(0.0);
  Point<double> displacement_vector{ 0.0, -1.6 };
  displacement_vector = rotate(displacement_vector, rotation);

  const Point<double> first_point_second_xy_plane{ 0.0, 0.4 };
  const Point<double> second_point_second_xy_plane{ 0.0, 1.4 };
  const Point<double> arbitrary_point_second_xy_plane{ 0.0, 2.4 };
  auto result = affine_transform_from_points_only_translation_and_rotation(
      first_point_first_xy_plane, second_point_first_xy_plane, first_point_second_xy_plane,
      second_point_second_xy_plane);
  EXPECT_NEAR(rotation_of(result), rotation, EPSILON)
      << "Expected rotation to be " << rotation << " for points: (" << first_point_first_xy_plane.x << ", "
      << first_point_first_xy_plane.y << ") and (" << second_point_first_xy_plane.x << ", "
      << second_point_first_xy_plane.y << ") to (" << first_point_second_xy_plane.x << ", "
      << first_point_second_xy_plane.y << ") and (" << second_point_second_xy_plane.x << ", "
      << second_point_second_xy_plane.y << ").";
  EXPECT_NEAR(result.tx, displacement_vector.x, EPSILON)
      << "Expected displacement vector x to be " << displacement_vector.x << " for points: ("
      << first_point_first_xy_plane.x << ", " << first_point_first_xy_plane.y << ") and ("
      << second_point_first_xy_plane.x << ", " << second_point_first_xy_plane.y << ") to ("
      << first_point_second_xy_plane.x << ", " << first_point_second_xy_plane.y << ") and ("
      << second_point_second_xy_plane.x << ", " << second_point_second_xy_plane.y << ").";
  EXPECT_NEAR(result.ty, displacement_vector.y, EPSILON)
      << "Expected displacement vector y to be " << displacement_vector.y << " for points: ("
      << first_point_first_xy_plane.x << ", " << first_point_first_xy_plane.y << ") and ("
      << second_point_first_xy_plane.x << ", " << second_point_first_xy_plane.y << ") to ("
      << first_point_second_xy_plane.x << ", " << first_point_second_xy_plane.y << ") and ("
      << second_point_second_xy_plane.x << ", " << second_point_second_xy_plane.y << ").";
  EXPECT_NEAR(first_point_second_xy_plane.x, transform_point(result, first_point_first_xy_plane).x, EPSILON)
      << "Expected first point in second coordinate system to be (" << first_point_second_xy_plane.x << ", "
      << first_point_second_xy_plane.y << ") but got (" << transform_point(result, first_point_first_xy_plane).x << ", "
      << transform_point(result, first_point_first_xy_plane).y << ").";
  EXPECT_NEAR(first_point_second_xy_plane.y, transform_point(result, first_point_first_xy_plane).y, EPSILON)
      << "Expected first point in second coordinate system to be (" << first_point_second_xy_plane.x << ", "
      << first_point_second_xy_plane.y << ") but got (" << transform_point(result, first_point_first_xy_plane).x << ", "
      << transform_point(result, first_point_first_xy_plane).y << ").";
  EXPECT_NEAR(second_point_second_xy_plane.x, transform_point(result, second_point_first_xy_plane).x, EPSILON)
      << "Expected second point in second coordinate system to be (" << second_point_second_xy_plane.x << ", "
      << second_point_second_xy_plane.y << ") but got (" << transform_point(result, second_point_first_xy_plane).x
      << ", " << transform_point(result, second_point_first_xy_plane).y << ").";
  EXPECT_NEAR(second_point_second_xy_plane.y, transform_point(result, second_point_first_xy_plane).y, EPSILON)
      << "Expected second point in second coordinate system to be (" << second_point_second_xy_plane.x << ", "
      << second_point_second_xy_plane.y << ") but got (" << transform_point(result, second_point_first_xy_plane).x
      << ", " << transform_point(result, second_point_first_xy_plane).y << ").";
  EXPECT_NEAR(arbitrary_point_second_xy_plane.x, transform_point(result, arbitrary_point_first_xy_plane).x, EPSILON)
      << "Expected arbitrary point in second coordinate system to be (" << arbitrary_point_second_xy_plane.x << ", "
      << arbitrary_point_second_xy_plane.y << ") but got (" << transform_point(result, arbitrary_point_first_xy_plane).x
      << ", " << transform_point(result, arbitrary_point_first_xy_plane).y << ").";
  EXPECT_NEAR(arbitrary_point_second_xy_plane.y, transform_point(result, arbitrary_point_first_xy_plane).y, EPSILON)
      << "Expected arbitrary point in second coordinate system to be (" << arbitrary_point_second_xy_plane.x << ", "
      << arbitrary_point_second_xy_plane.y << ") but got (" << transform_point(result, arbitrary_point_first_xy_plane).x
      << ", " << transform_point(result, arbitrary_point_first_xy_plane).y << ").";
}

TEST(EdgeCases, PointsOnXaxis) {
  constexpr Point<double> first_point_first_xy_plane{ 3.0, 0.0 };
  constexpr Point<double> second_point_first_xy_plane{ 2.0, 0.0 };
  constexpr Point<double> arbitrary_point_first_xy_plane{ 4.0, 0.0 };

  double rotation = degrees_to_radians(0.0);
  Point<double> displacement_vector{ 3.0, 0.0 };
  displacement_vector = rotate(displacement_vector, rotation);

  const Point<double> first_point_second_xy_plane{ 6.0, 0.0 };
  const Point<double> second_point_second_xy_plane{ 5.0, 0.0 };
  const Point<double> arbitrary_point_second_xy_plane{ 7.0, 0.0 };
  auto result = affine_transform_from_points_only_translation_and_rotation(
      first_point_first_xy_plane, second_point_first_xy_plane, first_point_second_xy_plane,
      second_point_second_xy_plane);
  EXPECT_NEAR(rotation_of(result), rotation, EPSILON)
      << "Expected rotation to be " << rotation << " for points: (" << first_point_first_xy_plane.x << ", "
      << first_point_first_xy_plane.y << ") and (" << second_point_first_xy_plane.x << ", "
      << second_point_first_xy_plane.y << ") to (" << first_point_second_xy_plane.x << ", "
      << first_point_second_xy_plane.y << ") and (" << second_point_second_xy_plane.x << ", "
      << second_point_second_xy_plane.y << ").";
  EXPECT_NEAR(result.tx, displacement_vector.x, EPSILON)
      << "Expected displacement vector x to be " << displacement_vector.x << " for points: ("
      << first_point_first_xy_plane.x << ", " << first_point_first_xy_plane.y << ") and ("
      << second_point_first_xy_plane.x << ", " << second_point_first_xy_plane.y << ") to ("
      << first_point_second_xy_plane.x << ", " << first_point_second_xy_plane.y << ") and ("
      << second_point_second_xy_plane.x << ", " << second_point_second_xy_plane.y << ").";
  EXPECT_NEAR(result.ty, displacement_vector.y, EPSILON)
      << "Expected displacement vector y to be " << displacement_vector.y << " for points: ("
      << first_point_first_xy_plane.x << ", " << first_point_first_xy_plane.y << ") and ("
      << second_point_first_xy_plane.x << ", " << second_point_first_xy_plane.y << ") to ("
      << first_point_second_xy_plane.x << ", " << first_point_second_xy_plane.y << ") and ("
      << second_point_second_xy_plane.x << ", " << second_point_second_xy_plane.y << ").";
  EXPECT_NEAR(first_point_second_xy_plane.x, transform_point(result, first_point_first_xy_plane).x, EPSILON)
      << "Expected first point in second coordinate system to be (" << first_point_second_xy_plane.x << ", "
      << first_point_second_xy_plane.y << ") but got (" << transform_point(result, first_point_first_xy_plane).x << ", "
      << transform_point(result, first_point_first_xy_plane).y << ").";
  EXPECT_NEAR(first_point_second_xy_plane.y, transform_point(result, first_point_first_xy_plane).y, EPSILON)
      << "Expected first point in second coordinate system to be (" << first_point_second_xy_plane.x << ", "
      << first_point_second_xy_plane.y << ") but got (" << transform_point(result, first_point_first_xy_plane).x << ", "
      << transform_point(result, first_point_first_xy_plane).y << ").";
  EXPECT_NEAR(second_point_second_xy_plane.x, transform_point(result, second_point_first_xy_plane).x, EPSILON)
      << "Expected second point in second coordinate system to be (" << second_point_second_xy_plane.x << ", "
      << second_point_second_xy_plane.y << ") but got (" << transform_point(result, second_point_first_xy_plane).x
      << ", " << transform_point(result, second_point_first_xy_plane).y << ").";
  EXPECT_NEAR(second_point_second_xy_plane.y, transform_point(result, second_point_first_xy_plane).y, EPSILON)
      << "Expected second point in second coordinate system to be (" << second_point_second_xy_plane.x << ", "
      << second_point_second_xy_plane.y << ") but got (" << transform_point(result, second_point_first_xy_plane).x
      << ", " << transform_point(result, second_point_first_xy_plane).y << ").";
  EXPECT_NEAR(arbitrary_point_second_xy_plane.x, transform_point(result, arbitrary_point_first_xy_plane).x, EPSILON)
      << "Expected arbitrary point in second coordinate system to be (" << arbitrary_point_second_xy_plane.x << ", "
      << arbitrary_point_second_xy_plane.y << ") but got (" << transform_point(result, arbitrary_point_first_xy_plane).x
      << ", " << transform_point(result, arbitrary_point_first_xy_plane).y << ").";
  EXPECT_NEAR(arbitrary_point_second_xy_plane.y, transform_point(result, arbitrary_point_first_xy_plane).y, EPSILON)
      << "Expected arbitrary point in second coordinate system to be (" << arbitrary_point_second_xy_plane.x << ", "
      << arbitrary_point_second_xy_plane.y << ") but got (" << transform_point(result, arbitrary_point_first_xy_plane).x
      << ", " << transform_point(result, arbitrary_point_first_xy_plane).y << ").";
}

}  // namespace scorpio_utils::geometry
