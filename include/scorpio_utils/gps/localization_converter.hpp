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
