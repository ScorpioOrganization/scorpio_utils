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
