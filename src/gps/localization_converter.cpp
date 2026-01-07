#include "scorpio_utils/gps/localization_converter.hpp"

#include <cmath>

namespace scorpio_utils::gps {
// Convert geodetic coordinates (lat, lon, alt) to ECEF (Earth-Centered, Earth-Fixed)
void LocalizationConverter::geodetic_to_ecef(
  double lat, double lon, double alt_wgs84,
  double& x, double& y, double& z
) {
  const double lat_rad = lat * M_PI / 180.0;
  const double lon_rad = lon * M_PI / 180.0;

  const double sin_lat = std::sin(lat_rad);
  const double cos_lat = std::cos(lat_rad);
  const double sin_lon = std::sin(lon_rad);
  const double cos_lon = std::cos(lon_rad);

  // Radius of curvature in the prime vertical
  const double N = WGS84_A / std::sqrt(1.0 - WGS84_E2 * sin_lat * sin_lat);

  x = (N + alt_wgs84) * cos_lat * cos_lon;
  y = (N + alt_wgs84) * cos_lat * sin_lon;
  z = (N * (1.0 - WGS84_E2) + alt_wgs84) * sin_lat;
}

// Convert ECEF coordinates to geodetic (lat, lon, alt)
void LocalizationConverter::ecef_to_geodetic(
  double x, double y, double z,
  double& lat, double& lon, double& alt_wgs84
) {
  // Longitude is straightforward
  lon = std::atan2(y, x) * 180.0 / M_PI;

  // Iterative method for latitude and altitude (Bowring's method)
  const double p = std::sqrt(x * x + y * y);
  const double e2 = WGS84_E2;
  const double a = WGS84_A;
  const double b = WGS84_B;

  // Initial estimate
  double lat_rad = std::atan2(z, p * (1.0 - e2));

  // Iterate to refine (only if not near poles)
  if (p > 1e-10) {
    for (int i = 0; i < 5; ++i) {
      const double sin_lat = std::sin(lat_rad);
      const double cos_lat = std::cos(lat_rad);
      const double N = a / std::sqrt(1.0 - e2 * sin_lat * sin_lat);

      // Guard against division by zero near poles
      if (std::abs(cos_lat) > 1e-10) {
        const double h = p / cos_lat - N;
        lat_rad = std::atan2(z, p * (1.0 - e2 * N / (N + h)));
      } else {
        // Near poles, use direct calculation
        break;
      }
    }
  }

  lat = lat_rad * 180.0 / M_PI;

  // Calculate altitude
  const double sin_lat = std::sin(lat_rad);
  const double cos_lat = std::cos(lat_rad);
  const double N = a / std::sqrt(1.0 - e2 * sin_lat * sin_lat);

  if (std::abs(cos_lat) > 1e-10) {
    alt_wgs84 = p / cos_lat - N;
  } else {
    alt_wgs84 = std::abs(z) - b;
  }
}

// Constructor: initialize reference point and rotation matrix
LocalizationConverter::LocalizationConverter(
  double lat_ref,
  double lon_ref,
  double alt_ref_egm96
)
: _lat_ref(lat_ref),
  _lon_ref(lon_ref),
  _alt_ref_egm96(alt_ref_egm96) {
  // Convert reference point to ECEF
  // Note: Using alt_ref_egm96 directly as we assume geoid height = 0
  geodetic_to_ecef(lat_ref, lon_ref, alt_ref_egm96, _x0_ecef, _y0_ecef, _z0_ecef);

  // Build rotation matrix from ECEF to local ENU (East-North-Up)
  const double lat_rad = lat_ref * M_PI / 180.0;
  const double lon_rad = lon_ref * M_PI / 180.0;

  const double sin_lat = std::sin(lat_rad);
  const double cos_lat = std::cos(lat_rad);
  const double sin_lon = std::sin(lon_rad);
  const double cos_lon = std::cos(lon_rad);

  // Rotation matrix from ECEF to ENU
  _r11 = -sin_lon;
  _r12 = cos_lon;
  _r13 = 0.0;

  _r21 = -sin_lat * cos_lon;
  _r22 = -sin_lat * sin_lon;
  _r23 = cos_lat;

  _r31 = cos_lat * cos_lon;
  _r32 = cos_lat * sin_lon;
  _r33 = sin_lat;
}

void LocalizationConverter::gps_to_local(
  double lat, double lon, double alt_egm96,
  double& x, double& y, double& z
) const {
  // Convert GPS to ECEF
  // Note: Using alt_egm96 directly as we assume geoid height = 0
  double x_ecef, y_ecef, z_ecef;
  geodetic_to_ecef(lat, lon, alt_egm96, x_ecef, y_ecef, z_ecef);

  // Translate to reference point
  const double dx = x_ecef - _x0_ecef;
  const double dy = y_ecef - _y0_ecef;
  const double dz = z_ecef - _z0_ecef;

  // Rotate from ECEF to local ENU
  const double e = _r11 * dx + _r12 * dy + _r13 * dz;
  const double n = _r21 * dx + _r22 * dy + _r23 * dz;
  const double u = _r31 * dx + _r32 * dy + _r33 * dz;

  // Output: x=East, y=North, z=Up
  x = e;
  y = n;
  z = u;
}

void LocalizationConverter::local_to_gps(
  double x, double y, double z,
  double& lat, double& lon, double& alt_egm96
) const {
  // Input: x=East, y=North, z=Up
  const double e = x;
  const double n = y;
  const double u = z;

  // Rotate from local ENU to ECEF (transpose of rotation matrix)
  const double dx = _r11 * e + _r21 * n + _r31 * u;
  const double dy = _r12 * e + _r22 * n + _r32 * u;
  const double dz = _r13 * e + _r23 * n + _r33 * u;

  // Translate from reference point
  const double x_ecef = _x0_ecef + dx;
  const double y_ecef = _y0_ecef + dy;
  const double z_ecef = _z0_ecef + dz;

  // Convert ECEF to geodetic
  double alt_wgs84;
  ecef_to_geodetic(x_ecef, y_ecef, z_ecef, lat, lon, alt_wgs84);

  // Note: Using alt_wgs84 directly as alt_egm96 (assuming geoid height = 0)
  alt_egm96 = alt_wgs84;
}
}  // namespace scorpio_utils::gps
