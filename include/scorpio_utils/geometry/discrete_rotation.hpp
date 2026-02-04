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
