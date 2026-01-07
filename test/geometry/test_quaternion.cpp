#include <gtest/gtest.h>
#include <cmath>
#include "scorpio_utils/geometry/quaternion.hpp"

constexpr double THRESHOLD = 1e-6;

namespace scorpio_utils::geometry {

TEST(Quaternion_Test, ConstructorFromComponents) {
  // This is put here to prevent compile time optimization caused by 'constexpr'.
  // Making quaternion dependent on non-const variable (since const variables with
  // known at compile time values are considered 'constexpr') makes it evaluated at runtime
  // and thanks to that we get 100% code coverage in lcov  :)
  double x = 1.0;
  Quaternion q(x, 2.0, 3.0, 4.0);

  ASSERT_DOUBLE_EQ(q.x, 1.0) << "Expected x = 1.0";
  ASSERT_DOUBLE_EQ(q.y, 2.0) << "Expected y = 2.0";
  ASSERT_DOUBLE_EQ(q.z, 3.0) << "Expected z = 3.0";
  ASSERT_DOUBLE_EQ(q.w, 4.0) << "Expected w = 4.0";
}

TEST(Quaternion_Test, ConstructorFromYawPitchRoll_Zero) {
  Quaternion q(0.0, 0.0, 0.0);

  ASSERT_NEAR(q.x, 0.0, THRESHOLD) << "Expected x ~ 0.0";
  ASSERT_NEAR(q.y, 0.0, THRESHOLD) << "Expected y ~ 0.0";
  ASSERT_NEAR(q.z, 0.0, THRESHOLD) << "Expected z ~ 0.0";
  ASSERT_NEAR(q.w, 1.0, THRESHOLD) << "Expected w ~ 1.0";
}

TEST(Quaternion_Test, YawPitchRoll_Reconstruction) {
  double yaw = M_PI / 4;    // 45°
  double pitch = M_PI / 6;  // 30°
  double roll = M_PI / 3;   // 60°

  Quaternion q(yaw, pitch, roll);

  ASSERT_NEAR(q.yaw(), yaw, THRESHOLD) << "Expected reconstructed yaw ~ original yaw";
  ASSERT_NEAR(q.pitch(), pitch, THRESHOLD) << "Expected reconstructed pitch ~ original pitch";
  ASSERT_NEAR(q.roll(), roll, THRESHOLD) << "Expected reconstructed roll ~ original roll";
}

TEST(Quaternion_Test, YawOnly) {
  double yaw = M_PI / 2;
  Quaternion q(yaw, 0.0, 0.0);

  ASSERT_NEAR(q.yaw(), yaw, THRESHOLD) << "Expected yaw ~ π/2";
  ASSERT_NEAR(q.pitch(), 0.0, THRESHOLD) << "Expected pitch ~ 0";
  ASSERT_NEAR(q.roll(), 0.0, THRESHOLD) << "Expected roll ~ 0";
}

TEST(Quaternion_Test, PitchOnly) {
  double pitch = M_PI / 4;
  Quaternion q(0.0, pitch, 0.0);

  ASSERT_NEAR(q.yaw(), 0.0, THRESHOLD) << "Expected yaw ~ 0";
  ASSERT_NEAR(q.pitch(), pitch, THRESHOLD) << "Expected pitch ~ π/4";
  ASSERT_NEAR(q.roll(), 0.0, THRESHOLD) << "Expected roll ~ 0";
}

TEST(Quaternion_Test, RollOnly) {
  double roll = M_PI / 6;
  Quaternion q(0.0, 0.0, roll);

  ASSERT_NEAR(q.yaw(), 0.0, THRESHOLD) << "Expected yaw ~ 0";
  ASSERT_NEAR(q.pitch(), 0.0, THRESHOLD) << "Expected pitch ~ 0";
  ASSERT_NEAR(q.roll(), roll, THRESHOLD) << "Expected roll ~ π/6";
}

}  // namespace scorpio_utils::geometry
