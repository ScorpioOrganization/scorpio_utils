#pragma once

#include <cstddef>

namespace scorpio_utils::geometry {
enum class DiscreteRotation : char {
  DEGREES_0,
  DEGREES_90,
  DEGREES_180,
  DEGREES_270,
};
constexpr size_t DISCRETE_ROTATION_COUNT = 4;

constexpr inline int discrete_rotation_to_int(const DiscreteRotation& r) {
  switch (r) {
    case DiscreteRotation::DEGREES_0:
      return 0;
    case DiscreteRotation::DEGREES_90:
      return 1;
    case DiscreteRotation::DEGREES_180:
      return 2;
    case DiscreteRotation::DEGREES_270:
      [[fallthrough]];
    default:
      return 3;
  }
}

constexpr inline DiscreteRotation discrete_rotation_from_int(const int r) {
  switch (r % 4) {
    case 0:
      return DiscreteRotation::DEGREES_0;
    case 1:
      return DiscreteRotation::DEGREES_90;
    case 2:
      return DiscreteRotation::DEGREES_180;
    case 3:
      [[fallthrough]];
    default:
      return DiscreteRotation::DEGREES_270;
  }
}

constexpr inline DiscreteRotation operator+(const DiscreteRotation& d, const int c) {
  return discrete_rotation_from_int(discrete_rotation_to_int(d) + c);
}

constexpr inline DiscreteRotation operator+=(DiscreteRotation& d, const int c) {
  d = discrete_rotation_from_int(discrete_rotation_to_int(d) + c);
  return d;
}

constexpr inline DiscreteRotation operator+(const DiscreteRotation& d, const DiscreteRotation& c) {
  return d + discrete_rotation_to_int(c);
}

constexpr inline DiscreteRotation operator+=(DiscreteRotation& d, const DiscreteRotation& c) {
  return d += discrete_rotation_to_int(c);
}

constexpr inline DiscreteRotation operator-(const DiscreteRotation& d, const int c) {
  return discrete_rotation_from_int(4 + discrete_rotation_to_int(d) - (c % 4));
}

constexpr inline DiscreteRotation operator-=(DiscreteRotation& d, const int c) {
  d = discrete_rotation_from_int(4 + discrete_rotation_to_int(d) - (c % 4));
  return d;
}

constexpr inline DiscreteRotation operator-(const DiscreteRotation& d, const DiscreteRotation& c) {
  return d - discrete_rotation_to_int(c);
}

constexpr inline DiscreteRotation operator-=(DiscreteRotation& d, const DiscreteRotation& c) {
  return d -= discrete_rotation_to_int(c);
}
}  // namespace scorpio_utils::geometry
