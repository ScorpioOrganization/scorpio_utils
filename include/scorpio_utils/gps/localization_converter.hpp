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

namespace scorpio_utils::gps {
class LocalizationConverter {
public:
  LocalizationConverter(
    double lat_ref,
    double lon_ref,
    double alt_ref_egm96 = 0.0);

  void gps_to_local(
    double lat, double lon, double alt_egm96,
    double& x, double& y, double& z) const;

  void local_to_gps(
    double x, double y, double z,
    double& lat, double& lon, double& alt_egm96) const;

private:
  // WGS84 ellipsoid parameters
  static constexpr double WGS84_A = 6378137.0;           // Semi-major axis (m)
  static constexpr double WGS84_F = 1.0 / 298.257223563;  // Flattening
  static constexpr double WGS84_B = WGS84_A * (1.0 - WGS84_F);  // Semi-minor axis
  static constexpr double WGS84_E2 = 2.0 * WGS84_F - WGS84_F * WGS84_F;  // First eccentricity squared

  // Reference point data
  double _lat_ref;
  double _lon_ref;
  double _alt_ref_egm96;

  // Reference point in ECEF coordinates
  double _x0_ecef;
  double _y0_ecef;
  double _z0_ecef;

  // Rotation matrix from ECEF to local ENU (East-North-Up)
  double _r11, _r12, _r13;
  double _r21, _r22, _r23;
  double _r31, _r32, _r33;

  // Helper methods
  static void geodetic_to_ecef(
    double lat, double lon, double alt_wgs84,
    double& x, double& y, double& z);
  static void ecef_to_geodetic(
    double x, double y, double z,
    double& lat, double& lon, double& alt_wgs84);
};
}  // namespace scorpio_utils::gps
