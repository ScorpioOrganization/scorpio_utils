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

#include "point.hpp"
#include "utils.hpp"
#include "scorpio_utils/assert.hpp"

namespace scorpio_utils::geometry {


struct CoordinateSystemTransformation {
  double rotation;
  Point<double> displacement_vector;
};

template<typename T>
inline Point<double> transform_point(const CoordinateSystemTransformation transform, const Point<T> point) {
  return rotate(point, transform.rotation) + transform.displacement_vector;
}

template<typename T>
inline CoordinateSystemTransformation transform_from_points(
  const Point<T> first_point_in_first_system,
  const Point<T> second_point_in_first_system,
  const Point<T> first_point_in_second_system,
  const Point<T> second_point_in_second_system) {
  SCU_ASSERT(first_point_in_first_system != second_point_in_first_system, "The two points in the first coordinate system must be distinct.");
  SCU_ASSERT(first_point_in_second_system != second_point_in_second_system, "The two points in the second coordinate system must be distinct.");
  const auto rotation = get_angle(first_point_in_second_system, second_point_in_second_system) - get_angle(
      first_point_in_first_system, second_point_in_first_system);
  const auto rotated_anchor = rotate(first_point_in_first_system, rotation);
  return CoordinateSystemTransformation{
    rotation,
    Point<double>{
      static_cast<double>(first_point_in_second_system.x) - rotated_anchor.x,
      static_cast<double>(first_point_in_second_system.y) - rotated_anchor.y,
    },
  };
}

}  // namespace scorpio_utils::geometry
