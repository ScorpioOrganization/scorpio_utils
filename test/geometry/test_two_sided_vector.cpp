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
#include "scorpio_utils/geometry/two_sided_vector.hpp"

namespace scorpio_utils::geometry {

TEST(TwoSidedVector_Test, EmptyVectorTest) {
  TwoSidedVector<int> v;

  EXPECT_FALSE(v.is_contained(0)) << "Expected vector to not contain any data.";
  EXPECT_TRUE(v.empty()) << "Vector should be empty.";
  EXPECT_EQ(v.size(), 0) << "Size of vector should be 0.";
  EXPECT_EQ(v.get_val(0), std::nullopt) << "Vector shouldn't have any values";
}

TEST(TwoSidedVector_Test, ConstructionTests) {
  TwoSidedVector<int> v(-2, 3, 7);

  EXPECT_FALSE(v.empty()) << "Vector shouldn't be empty.";
  EXPECT_EQ(v.size(), 4) << "Wrong vector size.";
  ASSERT_FALSE(v.is_contained(-2)) << "Expected -2 to be out of range.";
  ASSERT_TRUE(v.is_contained(2)) << "Expected 2 to be within the vector's range.";
  ASSERT_FALSE(v.is_contained(3)) << "Expected 3 to be out of range.";
  ASSERT_EQ(v.get_val(-1), std::make_optional(7)) << "Incorrect value at index -1.";
  ASSERT_EQ(v.get_val(2), std::make_optional(7)) << "Incorrect value at index 2.";
  ASSERT_EQ(v.get_val(3), std::nullopt) << "Index 3 shouldn't have value";
  ASSERT_EQ(v.get_val(-2), std::nullopt) << "Index -2 shouldn't have value";
}

TEST(TwoSidedVector_Test, ConstructionWithVector) {
  std::vector<int> data = { 1, 2, 3 };
  TwoSidedVector<int> v(-1, data);

  ASSERT_EQ(v.get_val(0), std::make_optional(1)) << "Expected value at index 0 to be 1.";
  ASSERT_EQ(v.get_val(2), std::make_optional(3)) << "Expected value at index 2 to be 3.";
  ASSERT_FALSE(v.get_val(-2).has_value()) << "Index -2 should not be contained in the vector.";
  ASSERT_EQ(v.size(), 3) << "Vector size should be 3.";
  ASSERT_FALSE(v.empty()) << "Vector should not be empty.";
}

TEST(TwoSidedVector_Test, PushTests) {
  TwoSidedVector<int> v;
  v.push_back(10);
  v.push_front(5);

  ASSERT_EQ(v.get_val(0), std::make_optional(10)) << "Expected value at index 0 to be 10.";
  ASSERT_EQ(v.get_val(-1), std::make_optional(5)) << "Expected value at index -1 to be 5.";
  ASSERT_EQ(v.size(), 2) << "Vector size should be 2.";

  v.clear();

  ASSERT_TRUE(v.empty()) << "Vector should be empty.";
}

TEST(TwoSidedVector_Test, DataModificationTests) {
  TwoSidedVector<int> v(-1, 2, 0);
  v.push_front(13);
  v.set_value(1, 42);
  EXPECT_EQ(v.get_val(1), std::make_optional(42)) << "Expected value at index 1 to be 42.";

  auto ptr = v.get_ptr(1);
  ASSERT_TRUE(ptr.has_value()) << "Pointer should have value.";
  EXPECT_EQ(ptr.value().get(), 42) << "Expected pointer value is 42.";

  auto ptr_negative = v.get_ptr(-1);
  ASSERT_TRUE(ptr_negative.has_value()) << "Pointer should have value.";
  EXPECT_EQ(ptr_negative.value().get(), 13) << "Expected pointer value is 0.";
  v.set_value(-1, 17);
  EXPECT_EQ(ptr_negative.value().get(), 17) << "Expected pointer value is 13.";

  auto mut_ptr = v.get_mut_ptr(1);
  ASSERT_TRUE(mut_ptr.has_value()) << "Pointer should have value.";
  mut_ptr.value().get() = 7;
  EXPECT_EQ(v.get_val(1), std::make_optional(7)) << "Expected value in vector is 7.";

  auto mut_ptr_negative = v.get_mut_ptr(-1);
  ASSERT_TRUE(mut_ptr_negative.has_value()) << "Expected pointer to have value";
  mut_ptr_negative.value().get() = 3;
  EXPECT_EQ(v.get_val(-1), std::make_optional(3)) << "Expected value in vector is 3.";

  ASSERT_EQ(v.get_ptr(4), std::nullopt) << "Function shouldn't return ptr on index 4";
  ASSERT_EQ(v.get_mut_ptr(4), std::nullopt) << "Function shouldn't return mut ptr on index 4";
}

TEST(TwoSidedVector_Test, ResizeToContain) {
  TwoSidedVector<int> v(-1, 2, 0);

  ASSERT_TRUE(v.resize_to_contain(-3, 7)) << "Resize should return true.";
  ASSERT_TRUE(v.is_contained(-3)) << "-3 should be contained in vector.";
}

}  // namespace scorpio_utils::geometry
