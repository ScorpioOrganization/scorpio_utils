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
struct Quaternion {
  double x;
  double y;
  double z;
  double w;

  constexpr inline Quaternion(double x, double y, double z, double w)
  : x(x), y(y), z(z), w(w) { }

  constexpr inline Quaternion(double yaw, double pitch, double roll)
  : x(0.0), y(0.0), z(0.0), w(0.0) {
    const double cy = std::cos(yaw * 0.5);
    const double sy = std::sin(yaw * 0.5);
    const double cp = std::cos(pitch * 0.5);
    const double sp = std::sin(pitch * 0.5);
    const double cr = std::cos(roll * 0.5);
    const double sr = std::sin(roll * 0.5);

    w = cr * cp * cy + sr * sp * sy;
    x = sr * cp * cy - cr * sp * sy;
    y = cr * sp * cy + sr * cp * sy;
    z = cr * cp * sy - sr * sp * cy;
  }

  constexpr inline double yaw() const {
    return std::atan2(2.0 * (w * z + x * y), 1.0 - 2.0 * (y * y + z * z));
  }

  constexpr inline double pitch() const {
    return std::asin(2.0 * (w * y - z * x));
  }

  constexpr inline double roll() const {
    return std::atan2(2.0 * (w * x + y * z), 1.0 - 2.0 * (x * x + y * y));
  }
};
}  // namespace scorpio_utils::geometry
