#include <gtest/gtest.h>
#define SCU_ASSERT_TERMINATE throw std::runtime_error("Assertion failed");
#include "scorpio_utils/unique_function.hpp"

TEST(UniqueFunction, baseUsage) {
  int x = 0;
  scorpio_utils::UniqueFunction<int()> f([x]() mutable {
      return x += 2;
    });

  ASSERT_TRUE(f);
  ASSERT_TRUE(f.has_value());
  EXPECT_EQ(f.call(), 2);
  EXPECT_EQ(f(), 4);
}

TEST(UniqueFunction, move) {
  int x = 0;
  scorpio_utils::UniqueFunction<int()> f([x]() mutable {
      return x += 2;
    });

  ASSERT_TRUE(f);
  ASSERT_TRUE(f.has_value());
  EXPECT_EQ(f(), 2);
  auto g = std::move(f);
  ASSERT_TRUE(g);
  ASSERT_TRUE(g.has_value());
  EXPECT_EQ(g(), 4);
  EXPECT_FALSE(f);
  EXPECT_FALSE(f.has_value());
  EXPECT_ANY_THROW(f());

  f = std::move(g);
  ASSERT_TRUE(f);
  ASSERT_TRUE(f.has_value());
  EXPECT_EQ(f.call(), 6);
  EXPECT_FALSE(g);
  EXPECT_FALSE(g.has_value());
  EXPECT_ANY_THROW(g());
  EXPECT_ANY_THROW(g.call());
}

TEST(UniqueFunction, unutilized) {
  scorpio_utils::UniqueFunction<int()> f;
  EXPECT_FALSE(f);
  EXPECT_FALSE(f.has_value());
  EXPECT_ANY_THROW(f());
  EXPECT_ANY_THROW(f.call());
}
