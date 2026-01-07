#pragma once

#include <algorithm>
#include <cmath>
#include "scorpio_utils/geometry/discrete_rotation.hpp"
#include "point.hpp"
#include "position.hpp"

namespace scorpio_utils::geometry {

template<typename T>
constexpr inline Point<T> rotate_left(const Point<T>& point) {
  return Point<T>{
    static_cast<T>(-point.y),
    point.x,
  };
}

template<typename T>
constexpr inline Point<T> rotate_right(const Point<T>& point) {
  return Point<T>{
    point.y,
    static_cast<T>(-point.x),
  };
}

template<typename T>
constexpr inline Point<T> rotate_180(const Point<T>& point) {
  return Point<T>{
    static_cast<T>(-point.x),
    static_cast<T>(-point.y),
  };
}

template<typename T>
constexpr inline Point<double> rotate(const Point<T>& point, const double angle) {
  const auto sin = std::sin(angle);
  const auto cos = std::cos(angle);
  const auto x = static_cast<double>(point.x);
  const auto y = static_cast<double>(point.y);
  return geometry::Point<double>{
    cos* x - sin * y,
    cos* y + sin * x,
  };
}

template<typename T>
constexpr inline Point<T> rotate(const Point<T> point, const DiscreteRotation angle) {
  switch (angle) {
    case geometry::DiscreteRotation::DEGREES_90:
      return geometry::rotate_left(point);
    case geometry::DiscreteRotation::DEGREES_180:
      return geometry::rotate_180(point);
    case geometry::DiscreteRotation::DEGREES_270:
      return rotate_right(point);
    case geometry::DiscreteRotation::DEGREES_0:
      [[fallthrough]];
    default:
      return point;
  }
}

template<typename T, typename Y, typename Z>
constexpr inline Point<T> translate_relative_point_to_position(
  const Position<T, Y>& position,
  const Point<Z> point) {
  return rotate(point, position.rotation) + position.point;
}

template<typename T>
constexpr inline double distance(Point<T> a, Point<T> b) {
  return std::sqrt(std::pow(static_cast<double>(a.x) - static_cast<double>(b.x), 2.0) +
  std::pow(static_cast<double>(a.y) - static_cast<double>(b.y), 2.0));
}

template<typename T>
inline double length(const Point<T>& point) {
  return distance(point, Point<T>{ });
}

template<typename T>
constexpr inline double get_angle(const Point<T>& a, const Point<T>& b) {
  return std::atan2(static_cast<double>(b.y - a.y), static_cast<double>(b.x - a.x));
}

template<typename T, typename Y>
constexpr inline double get_angle(
  const Point<T>& a, const Point<T>& b, const Y a_height, const Y b_height,
  const double resolution = 1.0) {
  const auto dist = distance(a, b) * resolution;
  const auto height_difference = b_height - a_height;
  return std::atan2(static_cast<double>(height_difference), dist);
}

constexpr inline DiscreteRotation discrete_rotation_from_radians(double r) {
  while (r < 0.0) {
    r += 2 * M_PI;
  }
  r = std::fmod(r, 2 * M_PI);
  if (r < M_PI / 4.0) {
    return DiscreteRotation::DEGREES_0;
  }
  if (r < 3 * M_PI / 4.0) {
    return DiscreteRotation::DEGREES_90;
  }
  if (r < 5 * M_PI / 4.0) {
    return DiscreteRotation::DEGREES_180;
  }
  if (r < 7 * M_PI / 4.0) {
    return DiscreteRotation::DEGREES_270;
  }
  return DiscreteRotation::DEGREES_0;
}

constexpr inline double discrete_rotation_to_radians(DiscreteRotation rotation) {
  switch (rotation) {
    case DiscreteRotation::DEGREES_90:
      return M_PI / 2.0;
    case DiscreteRotation::DEGREES_180:
      return M_PI;
    case DiscreteRotation::DEGREES_270:
      return 3 * M_PI / 2.0;
    case DiscreteRotation::DEGREES_0:
      [[fallthrough]];
    default:
      return 0.0;
  }
}

template<typename T>
constexpr inline T metropolitan_distance(const Point<T>& a, const Point<T>& b) {
  return static_cast<T>(std::abs(a.x - b.x) + std::abs(a.y - b.y));
}

template<typename T>
constexpr inline T yaw_difference(const T a, const T b) {
  static_assert(std::is_floating_point_v<T>, "T must be a floating point type");
  const auto diff = std::abs(a - b);
  return std::min(diff, 2 * M_PI - diff);
}

}  // namespace scorpio_utils::geometry
