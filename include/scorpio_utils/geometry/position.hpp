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

namespace scorpio_utils::geometry {
template<typename UnitType = double, typename RotationUnit = double>
struct Position {
  Point<UnitType> point;
  RotationUnit rotation;

  constexpr inline bool operator==(const Position<UnitType, RotationUnit>& other) const {
    return point == other.point && rotation == other.rotation;
  }

  constexpr inline bool operator!=(const Position<UnitType, RotationUnit>& other) const {
    return point != other.point || rotation != other.rotation;
  }

  constexpr inline Position operator+(const Point<UnitType>& other) const {
    return { point + other, rotation };
  }

  constexpr inline Position operator+=(const Point<UnitType>& other) {
    point += other;
    return *this;
  }

  constexpr inline Position operator-(const Point<UnitType>& other) const {
    return { point - other, rotation };
  }

  constexpr inline Position operator-=(const Point<UnitType>& other) {
    point -= other;
    return *this;
  }
};
}  // namespace scorpio_utils::geometry
