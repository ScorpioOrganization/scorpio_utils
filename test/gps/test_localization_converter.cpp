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

#include <gtest/gtest.h>
#include <cmath>
#include "scorpio_utils/gps/localization_converter.hpp"

class LocalizationConverterTest : public ::testing::Test {
protected:
  void SetUp() override {
    ref_lat = 52.2297;
    ref_lon = 21.0122;
    ref_alt = 100.0;

    converter = std::make_unique<scorpio_utils::gps::LocalizationConverter>(
      ref_lat, ref_lon, ref_alt);
  }

  void TearDown() override {
    converter.reset();
  }

  bool isApproxEqual(double a, double b, double tolerance = 1e-6) {
    return std::abs(a - b) < tolerance;
  }

  double ref_lat, ref_lon, ref_alt;
  std::unique_ptr<scorpio_utils::gps::LocalizationConverter> converter;
};

TEST_F(LocalizationConverterTest, ReferencePointConversion) {
  double x, y, z;
  converter->gps_to_local(ref_lat, ref_lon, ref_alt, x, y, z);

  EXPECT_TRUE(isApproxEqual(x, 0.0, 1e-3));
  EXPECT_TRUE(isApproxEqual(y, 0.0, 1e-3));
  EXPECT_TRUE(isApproxEqual(z, 0.0, 1e-3));
}

TEST_F(LocalizationConverterTest, GpsToLocalConversion) {
  double test_lat = ref_lat + 0.009;
  double test_lon = ref_lon + 0.014;
  double test_alt = ref_alt + 50.0;

  double x, y, z;
  converter->gps_to_local(test_lat, test_lon, test_alt, x, y, z);

  EXPECT_GT(std::abs(x), 900.0);
  EXPECT_LT(std::abs(x), 1100.0);
  EXPECT_GT(std::abs(y), 900.0);
  EXPECT_LT(std::abs(y), 1100.0);
  EXPECT_TRUE(isApproxEqual(z, 50.0, 5.0));
}

TEST_F(LocalizationConverterTest, LocalToGpsConversion) {
  double test_x = 500.0;
  double test_y = 300.0;
  double test_z = 25.0;

  double lat, lon, alt;
  converter->local_to_gps(test_x, test_y, test_z, lat, lon, alt);

  EXPECT_NE(lat, ref_lat);
  EXPECT_NE(lon, ref_lon);
  EXPECT_TRUE(isApproxEqual(alt, ref_alt + test_z, 5.0));
}

TEST_F(LocalizationConverterTest, RoundTripConversionGpsToLocalToGps) {
  double original_lat = 52.25;
  double original_lon = 21.05;
  double original_alt = 125.0;

  double x, y, z;
  converter->gps_to_local(original_lat, original_lon, original_alt, x, y, z);

  double recovered_lat, recovered_lon, recovered_alt;
  converter->local_to_gps(x, y, z, recovered_lat, recovered_lon, recovered_alt);

  EXPECT_TRUE(isApproxEqual(original_lat, recovered_lat, 1e-8));
  EXPECT_TRUE(isApproxEqual(original_lon, recovered_lon, 1e-8));
  EXPECT_TRUE(isApproxEqual(original_alt, recovered_alt, 1e-6));
}

TEST_F(LocalizationConverterTest, RoundTripConversionLocalToGpsToLocal) {
  double original_x = 1234.5;
  double original_y = -567.8;
  double original_z = 89.2;

  double lat, lon, alt;
  converter->local_to_gps(original_x, original_y, original_z, lat, lon, alt);

  double recovered_x, recovered_y, recovered_z;
  converter->gps_to_local(lat, lon, alt, recovered_x, recovered_y, recovered_z);

  EXPECT_TRUE(isApproxEqual(original_x, recovered_x, 1e-6));
  EXPECT_TRUE(isApproxEqual(original_y, recovered_y, 1e-6));
  EXPECT_TRUE(isApproxEqual(original_z, recovered_z, 1e-6));
}

TEST_F(LocalizationConverterTest, CoordinateSystemOrientation) {
  double x_pos, y_pos, z_pos;
  double x_neg, y_neg, z_neg;

  converter->gps_to_local(ref_lat + 0.001, ref_lon, ref_alt, x_pos, y_pos, z_pos);
  converter->gps_to_local(ref_lat - 0.001, ref_lon, ref_alt, x_neg, y_neg, z_neg);

  EXPECT_NE(y_pos, y_neg);

  converter->gps_to_local(ref_lat, ref_lon + 0.001, ref_alt, x_pos, y_pos, z_pos);
  converter->gps_to_local(ref_lat, ref_lon - 0.001, ref_alt, x_neg, y_neg, z_neg);

  EXPECT_NE(x_pos, x_neg);

  converter->gps_to_local(ref_lat, ref_lon, ref_alt + 10.0, x_pos, y_pos, z_pos);
  converter->gps_to_local(ref_lat, ref_lon, ref_alt - 10.0, x_neg, y_neg, z_neg);

  EXPECT_GT(z_pos, 0.0);
  EXPECT_LT(z_neg, 0.0);
}

TEST_F(LocalizationConverterTest, LargeDistanceConversion) {
  double test_lat = ref_lat + 0.09;
  double test_lon = ref_lon + 0.14;
  double test_alt = ref_alt + 500.0;

  double x, y, z;
  converter->gps_to_local(test_lat, test_lon, test_alt, x, y, z);

  EXPECT_GT(std::abs(x), 9000.0);
  EXPECT_LT(std::abs(x), 11000.0);
  EXPECT_GT(std::abs(y), 9000.0);
  EXPECT_LT(std::abs(y), 11000.0);

  // For large distances, geoid height variations can cause altitude differences
  // The tolerance needs to account for EGM96 geoid height variations over ~10km
  EXPECT_TRUE(isApproxEqual(z, 500.0, 20.0));

  double recovered_lat, recovered_lon, recovered_alt;
  converter->local_to_gps(x, y, z, recovered_lat, recovered_lon, recovered_alt);

  EXPECT_TRUE(isApproxEqual(test_lat, recovered_lat, 1e-7));
  EXPECT_TRUE(isApproxEqual(test_lon, recovered_lon, 1e-7));
  EXPECT_TRUE(isApproxEqual(test_alt, recovered_alt, 1e-5));
}

TEST_F(LocalizationConverterTest, NegativeAltitudes) {
  double test_alt = -50.0;

  double x, y, z;
  converter->gps_to_local(ref_lat, ref_lon, test_alt, x, y, z);

  EXPECT_TRUE(isApproxEqual(x, 0.0, 1e-3));
  EXPECT_TRUE(isApproxEqual(y, 0.0, 1e-3));
  EXPECT_LT(z, 0.0);

  double recovered_lat, recovered_lon, recovered_alt;
  converter->local_to_gps(x, y, z, recovered_lat, recovered_lon, recovered_alt);

  EXPECT_TRUE(isApproxEqual(ref_lat, recovered_lat, 1e-8))
    << "Negative altitude round-trip should preserve latitude";
  EXPECT_TRUE(isApproxEqual(ref_lon, recovered_lon, 1e-8))
    << "Negative altitude round-trip should preserve longitude";
  EXPECT_TRUE(isApproxEqual(test_alt, recovered_alt, 1e-6))
    << "Negative altitude round-trip should preserve altitude";
}

TEST_F(LocalizationConverterTest, ZeroAltitudeReference) {
  auto zero_alt_converter = std::make_unique<scorpio_utils::gps::LocalizationConverter>(
    ref_lat, ref_lon, 0.0);

  double x, y, z;
  zero_alt_converter->gps_to_local(ref_lat, ref_lon, 0.0, x, y, z);

  EXPECT_TRUE(isApproxEqual(x, 0.0, 1e-3));
  EXPECT_TRUE(isApproxEqual(y, 0.0, 1e-3));
  EXPECT_TRUE(isApproxEqual(z, 0.0, 1e-3));
}

TEST_F(LocalizationConverterTest, MultipleInstances) {
  auto converter2 = std::make_unique<scorpio_utils::gps::LocalizationConverter>(
    ref_lat + 0.01, ref_lon + 0.01, ref_alt + 100.0);

  double x1, y1, z1, x2, y2, z2;
  double test_lat = ref_lat + 0.005;
  double test_lon = ref_lon + 0.005;
  double test_alt = ref_alt + 50.0;

  converter->gps_to_local(test_lat, test_lon, test_alt, x1, y1, z1);
  converter2->gps_to_local(test_lat, test_lon, test_alt, x2, y2, z2);

  EXPECT_FALSE(isApproxEqual(x1, x2, 1.0))
    << "Different reference points should give different X coordinates";
  EXPECT_FALSE(isApproxEqual(y1, y2, 1.0))
    << "Different reference points should give different Y coordinates";
  EXPECT_FALSE(isApproxEqual(z1, z2, 1.0))
    << "Different reference points should give different Z coordinates";
}
