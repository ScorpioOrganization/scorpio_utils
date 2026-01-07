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
