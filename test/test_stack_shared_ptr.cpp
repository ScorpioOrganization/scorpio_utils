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

#include "scorpio_utils/stack_shared_ptr.hpp"

TEST(StackSharedPtr, BasicFunctionality) {
  {
    scorpio_utils::StackSharedPtr<int> ptr(42);
    EXPECT_EQ(*ptr, 42) << "Value should be 42";
    auto shared_ptr = ptr.get_shared();
    EXPECT_EQ(*shared_ptr, 42) << "Value from shared_ptr should be 42";
    EXPECT_EQ(shared_ptr.use_count(), 2) << "Use count should be 1";
    {
      auto shared_ptr2 = ptr.get_shared();
      EXPECT_EQ(shared_ptr2.use_count(), 3) << "Use count should be 2 after getting another shared_ptr";
      EXPECT_EQ(*shared_ptr2, 42) << "Value from second shared_ptr should be 42";
    }
    EXPECT_EQ(shared_ptr.use_count(), 2) << "Use count should be back to 1 after second shared_ptr is out of scope";
  }
  // Destructor should not assert
  {
    scorpio_utils::StackSharedPtr<std::string> ptr("Hello, World!");
    EXPECT_EQ(*ptr, "Hello, World!") << "Value should be 'Hello, World!'";
    auto shared_ptr = ptr.get_shared();
    EXPECT_EQ(*shared_ptr, "Hello, World!") << "Value from shared_ptr should be 'Hello, World!'";
    EXPECT_EQ(shared_ptr.use_count(), 2) << "Use count should be 1";
  }
  // Destructor should not assert
}

TEST(StackSharedPtr, ConstructorVariadic) {
  scorpio_utils::StackSharedPtr<std::string> ptr1("test");
  EXPECT_EQ(*ptr1, "test");

  std::vector<int> vec_data{ 1, 2, 3, 4, 5 };
  scorpio_utils::StackSharedPtr<std::vector<int>> ptr2(vec_data);
  EXPECT_EQ(ptr2->size(), 5u);
  EXPECT_EQ((*ptr2)[0], 1);
  EXPECT_EQ((*ptr2)[4], 5);

  scorpio_utils::StackSharedPtr<std::pair<int, std::string>> ptr3(42, "answer");
  EXPECT_EQ(ptr3->first, 42);
  EXPECT_EQ(ptr3->second, "answer");
}

TEST(StackSharedPtr, OperatorArrow) {
  scorpio_utils::StackSharedPtr<std::string> ptr("hello world");
  EXPECT_EQ(ptr->length(), 11);
  EXPECT_EQ(ptr->substr(0, 5), "hello");

  const auto& const_ptr = ptr;
  EXPECT_EQ(const_ptr->length(), 11);
  EXPECT_EQ(const_ptr->substr(6, 5), "world");
}

TEST(StackSharedPtr, OperatorDereference) {
  scorpio_utils::StackSharedPtr<int> ptr(100);
  EXPECT_EQ(*ptr, 100);

  *ptr = 200;
  EXPECT_EQ(*ptr, 200);

  const auto& const_ptr = ptr;
  EXPECT_EQ(*const_ptr, 200);
}

TEST(StackSharedPtr, ValueMethod) {
  scorpio_utils::StackSharedPtr<std::string> ptr("test_value");

  std::string& ref_value = ptr.value();
  EXPECT_EQ(ref_value, "test_value");
  ref_value = "modified";
  EXPECT_EQ(*ptr, "modified");

  const auto& const_ptr = ptr;
  const std::string& const_ref = const_ptr.value();
  EXPECT_EQ(const_ref, "modified");

  std::string moved_value = std::move(ptr).value();
  EXPECT_EQ(moved_value, "modified");
}

TEST(StackSharedPtr, ImplicitConversion) {
  scorpio_utils::StackSharedPtr<int> ptr(999);

  std::shared_ptr<int> shared = ptr;
  EXPECT_EQ(*shared, 999);
  EXPECT_EQ(shared.use_count(), 2);

  auto func = [](std::shared_ptr<int> p) { return *p * 2; };
  EXPECT_EQ(func(ptr), 1998);
}

TEST(StackSharedPtr, GetSharedMultiple) {
  scorpio_utils::StackSharedPtr<double> ptr(3.14159);

  auto shared1 = ptr.get_shared();
  auto shared2 = ptr.get_shared();
  auto shared3 = ptr.get_shared();

  EXPECT_EQ(shared1.use_count(), 4);
  EXPECT_EQ(shared2.use_count(), 4);
  EXPECT_EQ(shared3.use_count(), 4);

  EXPECT_EQ(*shared1, 3.14159);
  EXPECT_EQ(*shared2, 3.14159);
  EXPECT_EQ(*shared3, 3.14159);

  *shared1 = 2.71828;
  EXPECT_EQ(*shared2, 2.71828);
  EXPECT_EQ(*shared3, 2.71828);
  EXPECT_EQ(*ptr, 2.71828);
}

struct ComplexType {
  int x;
  std::string name;
  std::vector<double> values;

  ComplexType(int x, const std::string& name, std::vector<double> values)
  : x(x), name(name), values(std::move(values)) { }

  bool operator==(const ComplexType& other) const {
    return x == other.x && name == other.name && values == other.values;
  }
};

TEST(StackSharedPtr, ComplexTypes) {
  scorpio_utils::StackSharedPtr<ComplexType> ptr(42, "complex", std::vector<double>{ 1.1, 2.2, 3.3 });

  EXPECT_EQ(ptr->x, 42);
  EXPECT_EQ(ptr->name, "complex");
  EXPECT_EQ(ptr->values.size(), 3);
  EXPECT_DOUBLE_EQ(ptr->values[0], 1.1);

  auto shared = ptr.get_shared();
  shared->x = 100;
  shared->name = "modified";
  shared->values.push_back(4.4);

  EXPECT_EQ(ptr->x, 100);
  EXPECT_EQ(ptr->name, "modified");
  EXPECT_EQ(ptr->values.size(), 4);
  EXPECT_DOUBLE_EQ(ptr->values[3], 4.4);
}

TEST(StackSharedPtr, SharedPtrLifetime) {
  std::shared_ptr<int> external_shared;

  {
    scorpio_utils::StackSharedPtr<int> ptr(777);
    external_shared = ptr.get_shared();
    EXPECT_EQ(*external_shared, 777);
    EXPECT_EQ(external_shared.use_count(), 2);
    external_shared.reset();
  }
}

TEST(StackSharedPtr, MoveSemantics) {
  scorpio_utils::StackSharedPtr<std::unique_ptr<int>> ptr(std::make_unique<int>(123));

  EXPECT_NE(ptr->get(), nullptr);
  EXPECT_EQ(**ptr, 123);

  auto unique_moved = std::move(ptr).value();
  EXPECT_NE(unique_moved.get(), nullptr);
  EXPECT_EQ(*unique_moved, 123);
}

TEST(StackSharedPtr, ConstCorrectness) {
  const scorpio_utils::StackSharedPtr<std::string> const_ptr("const_test");

  EXPECT_EQ(*const_ptr, "const_test");
  EXPECT_EQ(const_ptr->length(), 10);

  const std::string& const_ref = const_ptr.value();
  EXPECT_EQ(const_ref, "const_test");
}
