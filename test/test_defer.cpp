#include <gtest/gtest.h>
#include "scorpio_utils/defer.hpp"
#include "scorpio_utils/testing/lifetime_helper.hpp"

struct CopyOnly {
  int value;
  explicit CopyOnly(int v)
  : value(v) { }
  CopyOnly(const CopyOnly& other)
  : value(other.value) { }
  CopyOnly(CopyOnly&&) = delete;
  CopyOnly& operator=(const CopyOnly& other) {
    value = other.value;
    return *this;
  }
  CopyOnly& operator=(CopyOnly&&) = delete;
};

struct NoMoveAssignment {
  int value;
  explicit NoMoveAssignment(int v)
  : value(v) { }
  NoMoveAssignment(const NoMoveAssignment& other)
  : value(other.value) { }
  NoMoveAssignment(NoMoveAssignment&& other)
  : value(other.value) { }
  NoMoveAssignment& operator=(const NoMoveAssignment& other) {
    value = other.value;
    return *this;
  }
  NoMoveAssignment& operator=(NoMoveAssignment&&) = delete;
};

TEST(Defer, BasicUsage) {
  int x = 0;
  {
    SCU_DEFER([&x]() { x = 42; });
    EXPECT_EQ(x, 0);
  }
  EXPECT_EQ(x, 42);
}

TEST(Restorer, BasicUsage) {
  int x = 10;
  {
    scorpio_utils::Restorer restorer(x);
    x = 20;
    EXPECT_EQ(x, 20);
  }
  EXPECT_EQ(x, 10);
}

TEST(Restorer, MultipleRestorers) {
  int x = 5;
  {
    scorpio_utils::Restorer restorer1(x);
    scorpio_utils::Restorer restorer2(x);
    x = 15;
    EXPECT_EQ(x, 15);
  }
  EXPECT_EQ(x, 5);
}

TEST(Restorer, Macro) {
  int x = 30;
  int y = 130;
  {
    SCU_RESTORER(x);
    SCU_MOVE_RESTORER(y);
    x = 40;
    y = 140;
    EXPECT_EQ(x, 40);
    EXPECT_EQ(y, 140);
  }
  EXPECT_EQ(x, 30);
  EXPECT_EQ(y, 130);
}

TEST(Restorer, NoMove) {
  CopyOnly c{ 7 };
  {
    SCU_RESTORER(c);
    c.value = 8;
  }
  EXPECT_EQ(c.value, 7);
}

TEST(MoveRestorer, BasicUsage) {
  scorpio_utils::testing::LifetimeHelper h;
  {
    scorpio_utils::MoveRestorer j(h);
    ASSERT_EQ(h.get_event_log().size(), 2);
    EXPECT_EQ(h.get_event_log()[0], scorpio_utils::testing::LifetimeHelper::EventType::CREATED);
    EXPECT_EQ(h.get_event_log()[1], scorpio_utils::testing::LifetimeHelper::EventType::HAS_BEEN_MOVED);
  }
  ASSERT_EQ(h.get_value_event_log().size(), 3);
  EXPECT_EQ(h.get_value_event_log()[0], scorpio_utils::testing::LifetimeHelper::EventType::CREATED);
  EXPECT_EQ(h.get_value_event_log()[1], scorpio_utils::testing::LifetimeHelper::EventType::MOVE);
  EXPECT_EQ(h.get_value_event_log()[2], scorpio_utils::testing::LifetimeHelper::EventType::MOVE);
}

TEST(MoveRestorer, NoMoveAssignment) {
  NoMoveAssignment c{ 7 };
  {
    scorpio_utils::MoveRestorer k(c);
    c.value = 8;
  }
  EXPECT_EQ(c.value, 7);
}
