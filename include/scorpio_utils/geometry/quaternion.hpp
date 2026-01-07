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
