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

#pragma once

#include <cmath>

#include "scorpio_utils/geometry/point.hpp"
#include "scorpio_utils/geometry/utils.hpp"
#include "scorpio_utils/geometry/position.hpp"
#include "scorpio_utils/assert.hpp"

namespace scorpio_utils::geometry {

constexpr double SAME_DISTANCE_EPSILON = 1e-10;

/*
  AffineTransformationMatrix represents a 2D affine transformation matrix.
  The matrix is represented in the form:
  [ a  b  tx ]
  [ c  d  ty ]
  [ 0  0   1 ]

  where (a, b, c, d) represent the linear transformation (rotation, scaling, shearing),
  and (tx, ty) represent the translation.distance

  The default values correspond to the identity transformation.
*/
struct AffineTransformationMatrix {
  double a = 1.0, b = 0.0;
  double c = 0.0, d = 1.0;

  double tx = 0.0, ty = 0.0;

  const double zeroX = 0.0;
  const double zeroY = 0.0;
  const double one = 1.0;
};
/*
  This function transforms points using the affine transformation matrix as follows:

  [ a  b  tx ][ x ]   [ax + by + tx]   [x_new]
  [ c  d  ty ][ y ] = [cx + dy + ty] = [y_new]
  [ 0  0  1  ][ 1 ]   [0x + 0y + 1 ]   [1]
*/
template<typename T>
inline Point<double> transform_point(const AffineTransformationMatrix transform, const Point<T> point) {
  return Point<double>{
    static_cast<double>(transform.a * point.x + transform.b * point.y + transform.tx),
    static_cast<double>(transform.c * point.x + transform.d * point.y + transform.ty),
  };
}

/*
  This function computes the affine transformation matrix based on two pairs of corresponding points in two different
  coordinate systems.
  The transformation includes only translation and rotation.
  Because we only consider translation and rotation in this function, therefore the matrix is constructed as follows:

  [ cos(theta)  -sin(theta)  tx ]
  [ sin(theta)   cos(theta)  ty ]
  [     0            0        1  ]

  where theta is the rotation angle, and (tx, ty) is the translation vector.
*/
template<typename T>
inline AffineTransformationMatrix affine_transform_from_points_only_translation_and_rotation(
  const Point<T> first_point_in_first_system,
  const Point<T> second_point_in_first_system,
  const Point<T> first_point_in_second_system,
  const Point<T> second_point_in_second_system) {
  SCU_ASSERT(!first_point_in_first_system.equal_with_threshold(second_point_in_first_system, SAME_DISTANCE_EPSILON),
  "The two points in the first coordinate system must be distinct.");
  SCU_ASSERT(!second_point_in_second_system.equal_with_threshold(first_point_in_second_system, SAME_DISTANCE_EPSILON),
  "The two points in the second coordinate system must be distinct.");
  const auto rotation = std::remainder(get_angle(first_point_in_second_system,
                                                 second_point_in_second_system) - get_angle(
      first_point_in_first_system, second_point_in_first_system), 2 * M_PI);
  const auto rotated_anchor = rotate(first_point_in_first_system, rotation);
  return AffineTransformationMatrix{
    std::cos(rotation),
    -std::sin(rotation),
    std::sin(rotation),
    std::cos(rotation),
    first_point_in_second_system.x - rotated_anchor.x,
    first_point_in_second_system.y - rotated_anchor.y,
  };
}

}  // namespace scorpio_utils::geometry
