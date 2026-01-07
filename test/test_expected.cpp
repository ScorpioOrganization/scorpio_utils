#include <gtest/gtest.h>
#include "scorpio_utils/expected.hpp"
#include "scorpio_utils/testing/lifetime_helper.hpp"

TEST(ExpectedTest, OkValue) {
  scorpio_utils::Expected<int, std::string> expected_value(42);
  EXPECT_FALSE(expected_value.is_err()) << "Expected value should not be an error";
  ASSERT_TRUE(expected_value.is_ok()) << "Expected value should be ok";
  EXPECT_EQ(*expected_value.ok(), 42) << "Expected value should match";
  EXPECT_FALSE(expected_value.err().has_value()) << "Expected error should not be present";
}

TEST(ExpectedTest, ErrorValue) {
  scorpio_utils::Expected<int, std::string> expected_error(scorpio_utils::Unexpected<std::string>("Error occurred"));
  EXPECT_FALSE(expected_error.is_ok()) << "Expected value should not be ok";
  ASSERT_TRUE(expected_error.is_err()) << "Expected value should be an error";
  EXPECT_EQ(expected_error.err()->get(), "Error occurred") << "Expected error message should match";
  EXPECT_FALSE(expected_error.ok().has_value()) << "Expected value should not be present";
}

TEST(ExpectedTest, MoveOkValue) {
  scorpio_utils::Expected<scorpio_utils::testing::LifetimeHelper,
    std::string> expected_value{ scorpio_utils::testing::LifetimeHelper() };
  EXPECT_FALSE(expected_value.is_err()) << "Expected value should not be an error";
  ASSERT_TRUE(expected_value.is_ok()) << "Expected value should be ok";
  auto moved_value = std::move(expected_value).ok();
  ASSERT_TRUE(moved_value.has_value()) << "Moved value should be present";
  EXPECT_EQ(moved_value->get_copy_count(), 0) << "Moved value should not have been copied";
  EXPECT_EQ(moved_value->get_copy_assign_count(), 0) << "Moved value should not have been copy assigned";
}

TEST(ExpectedTest, MoveErrorValue) {
  scorpio_utils::Expected<scorpio_utils::testing::LifetimeHelper,
    std::string> expected_error(scorpio_utils::Unexpected<std::string>("Error occurred"));
  EXPECT_FALSE(expected_error.is_ok()) << "Expected value should not be ok";
  ASSERT_TRUE(expected_error.is_err()) << "Expected value should be an error";
  auto moved_error = std::move(expected_error).err();
  ASSERT_TRUE(moved_error.has_value()) << "Moved error should be present";
  EXPECT_EQ(*moved_error, "Error occurred") << "Moved error message should match";
  EXPECT_FALSE(expected_error.ok().has_value()) << "Expected value should not be present after move";
}
