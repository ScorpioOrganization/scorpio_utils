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

namespace scorpio_utils::geometry {
template<typename UnitType = double>
struct Point {
  UnitType x;
  UnitType y;

  inline geometry::Point<UnitType> operator+(const geometry::Point<UnitType>& point) const {
    return Point{
      static_cast<UnitType>(x + point.x),
      static_cast<UnitType>(y + point.y),
    };
  }

  inline geometry::Point<UnitType> operator+=(const geometry::Point<UnitType>& point) {
    x += point.x;
    y += point.y;
    return *this;
  }

  inline geometry::Point<UnitType> operator-(const geometry::Point<UnitType>& point) const {
    return Point{
      static_cast<UnitType>(x - point.x),
      static_cast<UnitType>(y - point.y),
    };
  }

  inline geometry::Point<UnitType> operator-=(const geometry::Point<UnitType>& point) {
    x -= point.x;
    y -= point.y;
    return *this;
  }

  inline bool operator==(const geometry::Point<UnitType> other) const {
    return x == other.x && y == other.y;
  }

  inline bool operator!=(const geometry::Point<UnitType> other) const {
    return x != other.x || y != other.y;
  }

  inline bool equal_with_threshold(const geometry::Point<UnitType> other, const UnitType threshold) const {
    return std::abs(x - other.x) < threshold &&
           std::abs(y - other.y) < threshold;
  }

  inline geometry::Point<UnitType> operator*(const UnitType scalar) const {
    return Point{
      static_cast<UnitType>(x * scalar),
      static_cast<UnitType>(y * scalar),
    };
  }

  inline geometry::Point<UnitType> operator*=(const UnitType scalar) {
    x *= scalar;
    y *= scalar;
    return *this;
  }

  inline geometry::Point<UnitType> operator/(const UnitType scalar) const {
    return Point{
      static_cast<UnitType>(x / scalar),
      static_cast<UnitType>(y / scalar),
    };
  }

  inline geometry::Point<UnitType> operator/=(const UnitType scalar) {
    x /= scalar;
    y /= scalar;
    return *this;
  }
};
}  // namespace scorpio_utils::geometry
