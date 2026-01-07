#include <gtest/gtest.h>
#include "scorpio_utils/geometry/discrete_rotation.hpp"

namespace scorpio_utils::geometry {

TEST(DiscreteRotation_Test, DiscreteRotationToInt) {
  ASSERT_EQ(discrete_rotation_to_int(DiscreteRotation::DEGREES_0), 0)
    << "Invalid conversion of DiscreteRotation to int.";
  ASSERT_EQ(discrete_rotation_to_int(DiscreteRotation::DEGREES_90), 1)
    << "Invalid conversion of DiscreteRotation to int.";
  ASSERT_EQ(discrete_rotation_to_int(DiscreteRotation::DEGREES_180), 2)
    << "Invalid conversion of DiscreteRotation to int.";
  ASSERT_EQ(discrete_rotation_to_int(DiscreteRotation::DEGREES_270), 3)
    << "Invalid conversion of DiscreteRotation to int.";
}

TEST(DiscreteRotation_Test, DiscreteRotationFromInt) {
  ASSERT_EQ(discrete_rotation_from_int(0),
    DiscreteRotation::DEGREES_0) << "Invalid conversion from int to DiscreteRotation.";
  ASSERT_EQ(discrete_rotation_from_int(1),
    DiscreteRotation::DEGREES_90) << "Invalid conversion from int to DiscreteRotation.";
  ASSERT_EQ(discrete_rotation_from_int(2),
    DiscreteRotation::DEGREES_180) << "Invalid conversion from int to DiscreteRotation.";
  ASSERT_EQ(discrete_rotation_from_int(3),
    DiscreteRotation::DEGREES_270) << "Invalid conversion from int to DiscreteRotation.";
  ASSERT_EQ(discrete_rotation_from_int(15),
    DiscreteRotation::DEGREES_270) << "Invalid conversion from int to DiscreteRotation.";
}

TEST(DiscreteRotation_Test, ArithmeticsOperators) {
  ASSERT_EQ((DiscreteRotation::DEGREES_90 + DiscreteRotation::DEGREES_180),
  DiscreteRotation::DEGREES_270) << "Invalid addition two degrees handling.";
  ASSERT_EQ((DiscreteRotation::DEGREES_270 + DiscreteRotation::DEGREES_90),
  DiscreteRotation::DEGREES_0) << "Invalid addition two degrees handling.";

  ASSERT_EQ((DiscreteRotation::DEGREES_270 - DiscreteRotation::DEGREES_90),
  DiscreteRotation::DEGREES_180) << "Invalid subtraction of degrees handling.";
  ASSERT_EQ((DiscreteRotation::DEGREES_0 - DiscreteRotation::DEGREES_90),
  DiscreteRotation::DEGREES_270) << "Invalid subtraction of degrees handling.";

  ASSERT_EQ((DiscreteRotation::DEGREES_0 + 1), DiscreteRotation::DEGREES_90)
    << "Invalid addition of degree and number. ";
  ASSERT_EQ((DiscreteRotation::DEGREES_270 + 2), DiscreteRotation::DEGREES_90)
    << "Invalid addition of degree and number. ";

  DiscreteRotation val1 = DiscreteRotation::DEGREES_0;
  val1 += DiscreteRotation::DEGREES_90;
  DiscreteRotation val2 = DiscreteRotation::DEGREES_270;
  val2 += 2;

  ASSERT_EQ(val1, DiscreteRotation::DEGREES_90) << "Invalid addition assignment of degrees handling.";
  ASSERT_EQ(val2, DiscreteRotation::DEGREES_90) << "Invalid addition assignment of degree and number handling.";

  ASSERT_EQ((DiscreteRotation::DEGREES_0 - 1), DiscreteRotation::DEGREES_270)
    << "Invalid subtraction of degree and number. ";
  ASSERT_EQ((DiscreteRotation::DEGREES_270 - 2), DiscreteRotation::DEGREES_90)
    << "Invalid subtraction of degree and number. ";

    DiscreteRotation val3 = DiscreteRotation::DEGREES_270;
  val3 -= DiscreteRotation::DEGREES_90;
  DiscreteRotation val4 = DiscreteRotation::DEGREES_180;
  val4 -= 3;

  ASSERT_EQ(val3, DiscreteRotation::DEGREES_180) << "Invalid subtraction assignment of degrees handling.";
  ASSERT_EQ(val4, DiscreteRotation::DEGREES_270) << "Invalid subtraction assignment of degree and number handling.";
}

}  // namespace scorpio_utils::geometry
