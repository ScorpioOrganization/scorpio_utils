#include <gtest/gtest.h>
#include "scorpio_utils/geometry/plane.hpp"

namespace scorpio_utils::geometry {

TEST(Plane_Test, EmptyPlane_Test) {
  Plane<int> plane;

  auto neg_end = plane.get_negative_end();
  auto pos_end = plane.get_positive_end();

  ASSERT_EQ(neg_end.x, -1) << "Expected negative_end.x to be -1 on empty plane.";
  ASSERT_EQ(neg_end.y, -1) << "Expected negative_end.y to be -1 on empty plane.";
  ASSERT_EQ(pos_end.x, 0) << "Expected positive_end.x to be 0 on empty plane.";
  ASSERT_EQ(pos_end.y, 0) << "Expected positive_end.y to be 0 on empty plane.";
  ASSERT_EQ(plane.get_surface(), 0) << "Surface of empty plane should be 0.";
  ASSERT_EQ(plane.get_starting_point(), 0) << "Starting point should be 0.";
}

TEST(Plane_Test, ConstructionWithNegativeEnd) {
  Plane<int> plane(-2);

  auto neg_end = plane.get_negative_end();
  auto pos_end = plane.get_positive_end();

  ASSERT_EQ(plane.get_starting_point(), -1) << "Starting point should be -1.";
  ASSERT_EQ(plane.get_surface(), 0) << "Plane surface should be 0";
  ASSERT_EQ(neg_end.x, -1) << "Expected negative_end.x to be -1 on empty plane.";
  ASSERT_EQ(neg_end.y, -1) << "Expected negative_end.y to be -1 on empty plane.";
  ASSERT_EQ(pos_end.x, 0) << "Expected positive_end.x to be 0 on empty plane.";
  ASSERT_EQ(pos_end.y, 0) << "Expected positive_end.y to be 0 on empty plane.";
}

TEST(Plane_Test, ConstructionWithVector) {
  std::vector<std::vector<int>> values = {
    { 1, 2 },
    { 3, 4 }
  };

  Plane<int> plane({ -1, -1 }, values);

  ASSERT_EQ(plane.get_val({ 0, 0 }), std::make_optional(1)) << "Value at (0, 0) should be 1.";
  ASSERT_EQ(plane.get_val({ 1, 0 }), std::make_optional(3)) << "Value at (1, 0) should be 3.";
  ASSERT_EQ(plane.get_val({ 0, 1 }), std::make_optional(2)) << "Value at (0, 1) should be 2.";
  ASSERT_EQ(plane.get_val({ 1, 1 }), std::make_optional(4)) << "Value at (1, 1) should be 4.";
  ASSERT_EQ(plane.get_val({ -1, -1 }), std::nullopt) << "Value at (-1, -1) shouldn't exist";
  ASSERT_EQ(plane.get_starting_point(), 0) << "Starting point should be 0.";
}

TEST(Plane_Test, IsContainedTest) {
  std::vector<std::vector<int>> values = {
    { 10, 20 },
    { 30, 40 }
  };
  Plane<int> plane({ -1, -1 }, values);

  ASSERT_TRUE(plane.is_contained({ 0, 0 })) << "(0, 0) should be contained.";
  ASSERT_TRUE(plane.is_contained({ 1, 1 })) << "(1, 1) should be contained.";
  ASSERT_TRUE(plane.is_contained({ 1, 0 })) << "(1, 0) should be contained.";
  ASSERT_TRUE(plane.is_contained({ 0, 1 })) << "(0, 1) should be contained.";
  ASSERT_FALSE(plane.is_contained({ -2, 0 })) << "(-2, 0) should not be contained.";
  ASSERT_FALSE(plane.is_contained({ -1, -1 })) << "(-1, -1) should not be contained.";
}

TEST(Plane_Test, GetPtrAndSetValueTest) {
  Plane<int> plane({ -1, -1 }, { { 5, 6 }, { 7, 8 } });

  auto ptr1 = plane.get_ptr({ 0, 0 });
  ASSERT_TRUE(ptr1.has_value()) << "Pointer at (0, 0) should have value.";
  ASSERT_EQ(ptr1.value().get(), 5) << "Value at (0, 0) should be 5.";

  auto ptr2 = plane.get_ptr({ 1, 0 });
  ASSERT_TRUE(ptr2.has_value()) << "Pointer at (1, 0) should have value.";
  ASSERT_EQ(ptr2.value().get(), 7) << "Value at (1, 0) should be 6.";

  auto ptr3 = plane.get_ptr({ 0, 1 });
  ASSERT_TRUE(ptr3.has_value()) << "Pointer at (0, 1) should have value.";
  ASSERT_EQ(ptr3.value().get(), 6) << "Value at (10, 1) should be 7.";

  auto ptr4 = plane.get_ptr({ 1, 1 });
  ASSERT_TRUE(ptr4.has_value()) << "Pointer at (1, 1) should have value.";
  ASSERT_EQ(ptr4.value().get(), 8) << "Value at (1, 1) should be 8.";

  ASSERT_EQ(plane.get_ptr({ 2, 2 }), std::nullopt) << "You shouldn't be able to take pointer for (2,2).";

  auto mut_ptr = plane.get_mut_ptr({ 0, 0 });
  ASSERT_TRUE(mut_ptr.has_value()) << "Mutable pointer at (0, 0) should have value.";
  mut_ptr.value().get() = 42;
  ASSERT_EQ(plane.get_val({ 0, 0 }), std::make_optional(42)) << "Value at (0, 0) should be 42 after modification.";

  plane.set_value({ 0, 0 }, 99);
  ASSERT_EQ(plane.get_val({ 0, 0 }), std::make_optional(99)) << "Value at (0, 0) should be 99 after set_value.";
  ASSERT_EQ(mut_ptr.value().get(), 99) << "Value in mutable pointer should be 99 after set_value.";

  ASSERT_EQ(plane.get_mut_ptr({ 2, 2 }), std::nullopt) << "You shouldn't be able to take mutual pointer for (2,2).";
}

TEST(Plane_Test, ResizeToContainTest) {
  Plane<int> plane;

  ASSERT_TRUE(plane.resize_to_contain({ 2, 2 }, 1)) << "Plane should resize to contain (2, 2).";
  ASSERT_TRUE(plane.is_contained({ 1, 1 })) << "(1, 1) should be contained after resizing.";
  ASSERT_TRUE(plane.is_contained({ 2, 2 })) << "(2, 2) should be contained after resizing.";
  ASSERT_FALSE(plane.is_contained({ -1, -1 })) << "(-1, -1) should not be contained after resizing.";
  ASSERT_EQ(plane.get_val({ 2, 2 }), std::make_optional(1)) << "Value at (2, 2) should be 1 after resizing.";

  ASSERT_TRUE(plane.resize_to_contain({ 3, 3 }, 5)) << "Plane should resize to contain (3, 3).";
  ASSERT_TRUE(plane.is_contained({ 3, 3 })) << "(3, 3) should now be contained.";
  ASSERT_EQ(plane.get_val({ 3, 3 }), std::make_optional(5)) << "Value at (3, 3) should be 5.";

  ASSERT_FALSE(plane.resize_to_contain({ 2, 2 }, 7)) << "No resize should be needed.";
  ASSERT_EQ(plane.get_val({ 2, 2 }), std::make_optional(1)) << "Value should remain unchanged at (2, 2).";
}

TEST(Plane_Test, GetSurfaceTest) {
  std::vector<std::vector<int>> values = {
    { 1, 2 },
    { 3, 4 }
  };

  Plane<int> plane({ -1, -1 }, values);
  ASSERT_EQ(plane.get_surface(), 4) << "Surface should be 4 for 2x2 plane.";
}

TEST(Plane_Test, GetStartingPointTest) {
  std::vector<std::vector<int>> values = {
    { 1, 2 },
    { 3, 4 }
  };

  Plane<int> plane({ -2, -1 }, values);
  ASSERT_EQ(plane.get_starting_point(), -1) << "Starting point should be -1.";

  Plane<int> plane2({ -1, -2 }, values);
  ASSERT_EQ(plane2.get_starting_point(), 0) << "Starting point should be 0.";
}

}  // namespace scorpio_utils::geometry
