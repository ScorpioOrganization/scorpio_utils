#include <gtest/gtest.h>
#include <variant>
#include "scorpio_utils/misc.hpp"

TEST(VisitorOverloadingHelperTest, BasicUsage) {
  auto helper = scorpio_utils::VisitorOverloadingHelper{
    [](int& i) { i += 1; },
    [](double& d) { d += 0.5; },
    [](std::string& str) { str += " Hello world"; }
  };

  std::variant<int, double, std::string> v = 42;
  std::visit(helper, v);
  ASSERT_TRUE(std::holds_alternative<int>(v));
  EXPECT_EQ(std::get<int>(v), 43);
  v = 41.7;
  std::visit(helper, v);
  ASSERT_TRUE(std::holds_alternative<double>(v));
  EXPECT_EQ(std::get<double>(v), 42.2);
  v = std::string("Hello");
  std::visit(helper, v);
  ASSERT_TRUE(std::holds_alternative<std::string>(v));
  EXPECT_EQ(std::get<std::string>(v), "Hello Hello world");
}
