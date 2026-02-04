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
#include <string>
#include <string_view>
#include <vector>
#include "scorpio_utils/string_utils.hpp"

using scorpio_utils::ends_with;
using scorpio_utils::split;
using scorpio_utils::starts_with;
using scorpio_utils::strip;
using std::literals::operator""sv;

TEST(StringUtilsTest, StripCopy) {
  std::string original = "  hello world  ";
  std::string result = scorpio_utils::strip(std::string(
    original), SCU_STRING_WHITESPACES);
  EXPECT_EQ(result, "hello world");
  EXPECT_EQ(original, "  hello world  ");

  EXPECT_EQ(scorpio_utils::strip(std::string(
    "\t\n\r\f\v  test  \t\n\r\f\v"), SCU_STRING_WHITESPACES), "test");
  EXPECT_EQ(scorpio_utils::strip(std::string("no whitespace"),
    SCU_STRING_WHITESPACES), "no whitespace");
  EXPECT_EQ(scorpio_utils::strip(std::string("   "),
    SCU_STRING_WHITESPACES), "");
  EXPECT_EQ(scorpio_utils::strip(std::string(""),
    SCU_STRING_WHITESPACES), "");

  // Test custom characters
  EXPECT_EQ(scorpio_utils::strip(std::string("xxxhello worldxxx"), "x"), "hello world");
}

TEST(StringUtilsTest, StripStringView) {
  std::string_view str1("  hello world  ");
  auto result1 = scorpio_utils::strip(str1, "\t\n\r\f\v ");  // Use explicit delimiter
  EXPECT_EQ(result1, "hello world"sv);

  std::string_view str2("\t\n\r\f\v  test  \t\n\r\f\v");
  auto result2 = scorpio_utils::strip(str2, "\t\n\r\f\v ");
  EXPECT_EQ(result2, "test"sv);

  std::string_view str3("no whitespace");
  auto result3 = scorpio_utils::strip(str3, "\t\n\r\f\v ");
  EXPECT_EQ(result3, "no whitespace"sv);

  std::string_view str4("   ");
  auto result4 = scorpio_utils::strip(str4, "\t\n\r\f\v ");
  EXPECT_EQ(result4, ""sv);

  std::string_view str5("");
  auto result5 = scorpio_utils::strip(str5, "\t\n\r\f\v ");
  EXPECT_EQ(result5, ""sv);

  // Test custom characters
  std::string_view str6("xxxhello worldxxx");
  auto result6 = scorpio_utils::strip(str6, "x");
  EXPECT_EQ(result6, "hello world"sv);
}

// Test starts_with functions
TEST(StringUtilsTest, StartsWithCString) {
  std::string_view str("hello world");

  EXPECT_TRUE(scorpio_utils::starts_with(str, "hello"));
  EXPECT_TRUE(scorpio_utils::starts_with(str, "h"));
  EXPECT_TRUE(scorpio_utils::starts_with(str, ""));
  EXPECT_FALSE(scorpio_utils::starts_with(str, "world"));
  EXPECT_FALSE(scorpio_utils::starts_with(str, "Hello"));
  EXPECT_FALSE(scorpio_utils::starts_with(str, "hello world test"));

  std::string_view empty_str("");
  EXPECT_TRUE(scorpio_utils::starts_with(empty_str, ""));
  EXPECT_FALSE(scorpio_utils::starts_with(empty_str, "a"));
}

TEST(StringUtilsTest, StartsWithString) {
  std::string_view str("hello world");

  EXPECT_TRUE(scorpio_utils::starts_with(str, std::string("hello")));
  EXPECT_TRUE(scorpio_utils::starts_with(str, std::string("h")));
  EXPECT_TRUE(scorpio_utils::starts_with(str, std::string("")));
  EXPECT_FALSE(scorpio_utils::starts_with(str, std::string("world")));
  EXPECT_FALSE(scorpio_utils::starts_with(str, std::string("Hello")));
  EXPECT_FALSE(scorpio_utils::starts_with(str, std::string("hello world test")));

  std::string_view empty_str("");
  EXPECT_TRUE(scorpio_utils::starts_with(empty_str, std::string("")));
  EXPECT_FALSE(scorpio_utils::starts_with(empty_str, std::string("a")));
}

// Test ends_with functions
TEST(StringUtilsTest, EndsWithCString) {
  std::string_view str("hello world");

  EXPECT_TRUE(scorpio_utils::ends_with(str, "world"));
  EXPECT_TRUE(scorpio_utils::ends_with(str, "d"));
  EXPECT_TRUE(scorpio_utils::ends_with(str, ""));
  EXPECT_FALSE(scorpio_utils::ends_with(str, "hello"));
  EXPECT_FALSE(scorpio_utils::ends_with(str, "World"));
  EXPECT_FALSE(scorpio_utils::ends_with(str, "test hello world"));

  std::string_view empty_str("");
  EXPECT_TRUE(scorpio_utils::ends_with(empty_str, ""));
  EXPECT_FALSE(scorpio_utils::ends_with(empty_str, "a"));
}

TEST(StringUtilsTest, EndsWithString) {
  std::string_view str("hello world");

  EXPECT_TRUE(scorpio_utils::ends_with(str, std::string("world")));
  EXPECT_TRUE(scorpio_utils::ends_with(str, std::string("d")));
  EXPECT_TRUE(scorpio_utils::ends_with(str, std::string("")));
  EXPECT_FALSE(scorpio_utils::ends_with(str, std::string("hello")));
  EXPECT_FALSE(scorpio_utils::ends_with(str, std::string("World")));
  EXPECT_FALSE(scorpio_utils::ends_with(str, std::string("test hello world")));

  std::string_view empty_str("");
  EXPECT_TRUE(scorpio_utils::ends_with(empty_str, std::string("")));
  EXPECT_FALSE(scorpio_utils::ends_with(empty_str, std::string("a")));
}

// Test split functions
TEST(StringUtilsTest, SplitDefaultBasic) {
  // Test basic splitting on whitespace
  std::string_view str("hello world test");
  auto result = scorpio_utils::split(str);
  EXPECT_EQ(result.size(), 3);
  EXPECT_EQ(result[0], "hello"sv);
  EXPECT_EQ(result[1], "world"sv);
  EXPECT_EQ(result[2], "test"sv);
}

TEST(StringUtilsTest, SplitDefaultMultipleWhitespace) {
  // Test with multiple whitespace types
  std::string_view str("hello\t\nworld\r\ftest\v");
  auto result = scorpio_utils::split(str);
  EXPECT_EQ(result.size(), 3);
  EXPECT_EQ(result[0], "hello"sv);
  EXPECT_EQ(result[1], "world"sv);
  EXPECT_EQ(result[2], "test"sv);
}

TEST(StringUtilsTest, SplitDefaultEmptyString) {
  // Test empty string
  std::string_view str("");
  auto result = scorpio_utils::split(str);
  EXPECT_EQ(result.size(), 0);
}

TEST(StringUtilsTest, SplitDefaultOnlyWhitespace) {
  // Test string with only whitespace
  std::string_view str("   \t\n  ");
  auto result = scorpio_utils::split(str);
  // This depends on implementation - may return empty parts or single empty string
  EXPECT_EQ(result.size(), 0);
}

TEST(StringUtilsTest, SplitDefaultSingleWord) {
  // Test single word
  std::string_view str("hello");
  auto result = scorpio_utils::split(str);
  EXPECT_EQ(result.size(), 1);
  EXPECT_EQ(result[0], "hello"sv);
}

TEST(StringUtilsTest, SplitDefaultLeadingWhitespace) {
  // Test with leading whitespace
  std::string_view str("  \t hello world test");
  auto result = scorpio_utils::split(str);
  EXPECT_EQ(result.size(), 3);
  EXPECT_EQ(result[0], "hello"sv);
  EXPECT_EQ(result[1], "world"sv);
  EXPECT_EQ(result[2], "test"sv);
}

TEST(StringUtilsTest, SplitDefaultTrailingWhitespace) {
  // Test with trailing whitespace
  std::string_view str("hello world test  \t ");
  auto result = scorpio_utils::split(str);
  EXPECT_EQ(result.size(), 3);
  EXPECT_EQ(result[0], "hello"sv);
  EXPECT_EQ(result[1], "world"sv);
  EXPECT_EQ(result[2], "test"sv);
}

TEST(StringUtilsTest, SplitDefaultDoubleWhitespace) {
  // Test with double/multiple whitespace sequences
  std::string_view str("hello   \t\n  world    test");
  auto result = scorpio_utils::split(str);
  EXPECT_EQ(result.size(), 3);
  EXPECT_EQ(result[0], "hello"sv);
  EXPECT_EQ(result[1], "world"sv);
  EXPECT_EQ(result[2], "test"sv);
}

TEST(StringUtilsTest, SplitWithMaxCountBasic) {
  std::string_view str("hello world test example");

  auto result = scorpio_utils::split(str, 2);
  EXPECT_EQ(result.size(), 3);
  EXPECT_EQ(result[0], "hello"sv);
  EXPECT_EQ(result[1], "world"sv);
  EXPECT_EQ(result[2], "test example"sv);
}

TEST(StringUtilsTest, SplitWithMaxCountZero) {
  std::string_view str("hello world test example");

  auto result = scorpio_utils::split(str, 0);
  EXPECT_EQ(result.size(), 1);
  EXPECT_EQ(result[0], "hello world test example");
}

TEST(StringUtilsTest, SplitWithMaxCountExceeded) {
  std::string_view str("hello world test example");

  auto result = scorpio_utils::split(str, 10);
  EXPECT_EQ(result.size(), 4);
  EXPECT_EQ(result[0], "hello"sv);
  EXPECT_EQ(result[1], "world"sv);
  EXPECT_EQ(result[2], "test"sv);
  EXPECT_EQ(result[3], "example"sv);
}

TEST(StringUtilsTest, SplitWithMaxCountEmptyString) {
  // Test empty string
  std::string_view str("");
  auto result = scorpio_utils::split(str, 2);
  EXPECT_EQ(result.size(), 0);
}

TEST(StringUtilsTest, SplitWithMaxCountLeadingWhitespace) {
  // Test with leading whitespace
  std::string_view str("  \t hello world test example");
  auto result = scorpio_utils::split(str, 2);
  EXPECT_EQ(result.size(), 3);
  EXPECT_EQ(result[0], "hello"sv);
  EXPECT_EQ(result[1], "world"sv);
  EXPECT_EQ(result[2], "test example"sv);
}

TEST(StringUtilsTest, SplitWithMaxCountTrailingWhitespace) {
  // Test with trailing whitespace
  std::string_view str("hello world test example  \t ");
  auto result = scorpio_utils::split(str, 2);
  EXPECT_EQ(result.size(), 3);
  EXPECT_EQ(result[0], "hello"sv);
  EXPECT_EQ(result[1], "world"sv);
  EXPECT_EQ(result[2], "test example  \t "sv);
}

TEST(StringUtilsTest, SplitWithMaxCountDoubleWhitespace) {
  // Test with double/multiple whitespace sequences
  std::string_view str("hello   \t\n  world    test   example");
  auto result = scorpio_utils::split(str, 2);
  EXPECT_EQ(result.size(), 3);
  EXPECT_EQ(result[0], "hello"sv);
  EXPECT_EQ(result[1], "world"sv);
  EXPECT_EQ(result[2], "test   example"sv);
}

TEST(StringUtilsTest, SplitWithSeparatorBasic) {
  std::string_view str("hello,world,test");
  auto result = scorpio_utils::split(str, ",");
  EXPECT_EQ(result.size(), 3);
  EXPECT_EQ(result[0], "hello");
  EXPECT_EQ(result[1], "world");
  EXPECT_EQ(result[2], "test");
}

TEST(StringUtilsTest, SplitWithSeparatorMultiChar) {
  std::string_view str("hello::world::test");
  auto result = scorpio_utils::split(str, "::");
  EXPECT_EQ(result.size(), 3);
  EXPECT_EQ(result[0], "hello");
  EXPECT_EQ(result[1], "world");
  EXPECT_EQ(result[2], "test");
}

TEST(StringUtilsTest, SplitWithSeparatorAtEnds) {
  // Test with separator at start/end
  std::string_view str(",hello,world,");
  auto result = scorpio_utils::split(str, ",");
  EXPECT_EQ(result.size(), 4);
  EXPECT_EQ(result[0], "");
  EXPECT_EQ(result[1], "hello"sv);
  EXPECT_EQ(result[2], "world"sv);
  EXPECT_EQ(result[3], "");
}

TEST(StringUtilsTest, SplitWithSeparatorNotFound) {
  // Test with no separator
  std::string_view str("hello world");
  auto result = scorpio_utils::split(str, ",");
  EXPECT_EQ(result.size(), 1);
  EXPECT_EQ(result[0], "hello world");
}

TEST(StringUtilsTest, SplitWithSeparatorEmptyString) {
  // Test empty string
  std::string_view str("");
  auto result = scorpio_utils::split(str, ",");
  EXPECT_EQ(result.size(), 1);
  EXPECT_EQ(result[0], "");
}

TEST(StringUtilsTest, SplitWithSeparatorLeading) {
  // Test with leading separator
  std::string_view str(",hello,world,test");
  auto result = scorpio_utils::split(str, ",");
  EXPECT_EQ(result.size(), 4);
  EXPECT_EQ(result[0], "");
  EXPECT_EQ(result[1], "hello"sv);
  EXPECT_EQ(result[2], "world"sv);
  EXPECT_EQ(result[3], "test"sv);
}

TEST(StringUtilsTest, SplitWithSeparatorTrailing) {
  // Test with trailing separator
  std::string_view str("hello,world,test,");
  auto result = scorpio_utils::split(str, ",");
  EXPECT_EQ(result.size(), 4);
  EXPECT_EQ(result[0], "hello"sv);
  EXPECT_EQ(result[1], "world"sv);
  EXPECT_EQ(result[2], "test"sv);
  EXPECT_EQ(result[3], "");
}

TEST(StringUtilsTest, SplitWithSeparatorDouble) {
  // Test with double/consecutive separators
  std::string_view str("hello,,world,,,test");
  auto result = scorpio_utils::split(str, ",");
  EXPECT_EQ(result.size(), 6);
  EXPECT_EQ(result[0], "hello"sv);
  EXPECT_EQ(result[1], ""sv);
  EXPECT_EQ(result[2], "world"sv);
  EXPECT_EQ(result[3], ""sv);
  EXPECT_EQ(result[4], ""sv);
  EXPECT_EQ(result[5], "test"sv);
}

TEST(StringUtilsTest, SplitWithSeparatorAndMaxCountBasic) {
  std::string_view str("hello,world,test,example");

  auto result = scorpio_utils::split(str, ",", 2);
  EXPECT_EQ(result.size(), 3);
  EXPECT_EQ(result[0], "hello");
  EXPECT_EQ(result[1], "world");
  EXPECT_EQ(result[2], "test,example");
}

TEST(StringUtilsTest, SplitWithSeparatorAndMaxCountZero) {
  std::string_view str("hello,world,test,example");

  auto result = scorpio_utils::split(str, ",", 0);
  EXPECT_EQ(result.size(), 1);
  EXPECT_EQ(result[0], "hello,world,test,example");
}

TEST(StringUtilsTest, SplitWithSeparatorAndMaxCountExceeded) {
  std::string_view str("hello,world,test,example");

  auto result = scorpio_utils::split(str, ",", 10);
  EXPECT_EQ(result.size(), 4);
  EXPECT_EQ(result[0], "hello");
  EXPECT_EQ(result[1], "world");
  EXPECT_EQ(result[2], "test");
  EXPECT_EQ(result[3], "example");
}

TEST(StringUtilsTest, SplitWithSeparatorAndMaxCountMultiChar) {
  // Test with multi-character separator
  std::string_view str("hello::world::test::example");
  auto result = scorpio_utils::split(str, "::", 1);
  EXPECT_EQ(result.size(), 2);
  EXPECT_EQ(result[0], "hello");
  EXPECT_EQ(result[1], "world::test::example");
}

TEST(StringUtilsTest, SplitWithSeparatorAndMaxCountEmptyString) {
  // Test empty string
  std::string_view str("");
  auto result = scorpio_utils::split(str, ",", 2);
  EXPECT_EQ(result.size(), 1);
  EXPECT_EQ(result[0], "");
}

TEST(StringUtilsTest, SplitWithSeparatorAndMaxCountLeading) {
  // Test with leading separator
  std::string_view str(",hello,world,test,example");
  auto result = scorpio_utils::split(str, ",", 2);
  EXPECT_EQ(result.size(), 3);
  EXPECT_EQ(result[0], "");
  EXPECT_EQ(result[1], "hello"sv);
  EXPECT_EQ(result[2], "world,test,example"sv);
}

TEST(StringUtilsTest, SplitWithSeparatorAndMaxCountTrailing) {
  // Test with trailing separator
  std::string_view str("hello,world,test,example,");
  auto result = scorpio_utils::split(str, ",", 2);
  EXPECT_EQ(result.size(), 3);
  EXPECT_EQ(result[0], "hello"sv);
  EXPECT_EQ(result[1], "world"sv);
  EXPECT_EQ(result[2], "test,example,"sv);
}

TEST(StringUtilsTest, SplitWithSeparatorAndMaxCountDouble) {
  // Test with double/consecutive separators
  std::string_view str("hello,,world,,,test,example");
  auto result = scorpio_utils::split(str, ",", 3);
  EXPECT_EQ(result.size(), 4);
  EXPECT_EQ(result[0], "hello"sv);
  EXPECT_EQ(result[1], ""sv);
  EXPECT_EQ(result[2], "world"sv);
  EXPECT_EQ(result[3], ",,test,example"sv);
}
