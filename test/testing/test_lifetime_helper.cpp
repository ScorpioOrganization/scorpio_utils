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
#include "scorpio_utils/testing/lifetime_helper.hpp"

class LifetimeHelperTest : public ::testing::Test {
protected:
  void SetUp() override {
    ASSERT_TRUE(scorpio_utils::testing::LifetimeHelper::reset_counters());
  }
  void TearDown() override { }
};

TEST_F(LifetimeHelperTest, DefaultConstructor) {
  EXPECT_EQ(scorpio_utils::testing::LifetimeHelper::get_created_count(), 0);
  EXPECT_EQ(scorpio_utils::testing::LifetimeHelper::get_destroyed_count(), 0);
  EXPECT_EQ(scorpio_utils::testing::LifetimeHelper::get_existing_count(), 0);
  {
    scorpio_utils::testing::LifetimeHelper helper;
    EXPECT_EQ(helper.get_copy_count(), 0);
    EXPECT_EQ(helper.get_move_count(), 0);
    EXPECT_EQ(helper.get_copy_assign_count(), 0);
    EXPECT_EQ(helper.get_move_assign_count(), 0);
    EXPECT_EQ(scorpio_utils::testing::LifetimeHelper::get_created_count(), 1);
    EXPECT_EQ(scorpio_utils::testing::LifetimeHelper::get_destroyed_count(), 0);
    EXPECT_EQ(scorpio_utils::testing::LifetimeHelper::get_existing_count(), 1);
  }
  EXPECT_EQ(scorpio_utils::testing::LifetimeHelper::get_created_count(), 1);
  EXPECT_EQ(scorpio_utils::testing::LifetimeHelper::get_destroyed_count(), 1);
  EXPECT_EQ(scorpio_utils::testing::LifetimeHelper::get_existing_count(), 0);
}

TEST_F(LifetimeHelperTest, CopyConstructor) {
  EXPECT_EQ(scorpio_utils::testing::LifetimeHelper::get_created_count(), 0);
  EXPECT_EQ(scorpio_utils::testing::LifetimeHelper::get_destroyed_count(), 0);
  EXPECT_EQ(scorpio_utils::testing::LifetimeHelper::get_existing_count(), 0);
  {
    scorpio_utils::testing::LifetimeHelper helper1;
    {
      scorpio_utils::testing::LifetimeHelper helper2(helper1);
      EXPECT_EQ(helper1.get_copy_count(), 1);
      EXPECT_EQ(helper1.get_move_count(), 0);
      EXPECT_EQ(helper1.get_copy_assign_count(), 0);
      EXPECT_EQ(helper1.get_move_assign_count(), 0);
      EXPECT_EQ(helper2.get_copy_count(), 0);
      EXPECT_EQ(helper2.get_move_count(), 0);
      EXPECT_EQ(helper2.get_copy_assign_count(), 0);
      EXPECT_EQ(helper2.get_move_assign_count(), 0);
      EXPECT_EQ(scorpio_utils::testing::LifetimeHelper::get_created_count(), 2);
      EXPECT_EQ(scorpio_utils::testing::LifetimeHelper::get_destroyed_count(), 0);
      EXPECT_EQ(scorpio_utils::testing::LifetimeHelper::get_existing_count(), 2);
    }
    EXPECT_EQ(scorpio_utils::testing::LifetimeHelper::get_created_count(), 2);
    EXPECT_EQ(scorpio_utils::testing::LifetimeHelper::get_destroyed_count(), 1);
    EXPECT_EQ(scorpio_utils::testing::LifetimeHelper::get_existing_count(), 1);
  }
  EXPECT_EQ(scorpio_utils::testing::LifetimeHelper::get_created_count(), 2);
  EXPECT_EQ(scorpio_utils::testing::LifetimeHelper::get_destroyed_count(), 2);
  EXPECT_EQ(scorpio_utils::testing::LifetimeHelper::get_existing_count(), 0);
}

TEST_F(LifetimeHelperTest, CopyAssignment) {
  EXPECT_EQ(scorpio_utils::testing::LifetimeHelper::get_created_count(), 0);
  EXPECT_EQ(scorpio_utils::testing::LifetimeHelper::get_destroyed_count(), 0);
  EXPECT_EQ(scorpio_utils::testing::LifetimeHelper::get_existing_count(), 0);
  {
    scorpio_utils::testing::LifetimeHelper helper1;
    {
      scorpio_utils::testing::LifetimeHelper helper2;
      helper2 = helper1;
      EXPECT_EQ(helper1.get_copy_count(), 1);
      EXPECT_EQ(helper1.get_move_count(), 0);
      EXPECT_EQ(helper1.get_copy_assign_count(), 0);
      EXPECT_EQ(helper1.get_move_assign_count(), 0);
      EXPECT_EQ(helper2.get_copy_count(), 0);
      EXPECT_EQ(helper2.get_move_count(), 0);
      EXPECT_EQ(helper2.get_copy_assign_count(), 1);
      EXPECT_EQ(helper2.get_move_assign_count(), 0);
      helper2 = helper1;
      EXPECT_EQ(helper1.get_copy_count(), 2);
      EXPECT_EQ(helper2.get_copy_assign_count(), 2);
      EXPECT_EQ(scorpio_utils::testing::LifetimeHelper::get_created_count(), 2);
      EXPECT_EQ(scorpio_utils::testing::LifetimeHelper::get_destroyed_count(), 0);
      EXPECT_EQ(scorpio_utils::testing::LifetimeHelper::get_existing_count(), 2);
    }
    EXPECT_EQ(scorpio_utils::testing::LifetimeHelper::get_created_count(), 2);
    EXPECT_EQ(scorpio_utils::testing::LifetimeHelper::get_destroyed_count(), 1);
    EXPECT_EQ(scorpio_utils::testing::LifetimeHelper::get_existing_count(), 1);
  }
  EXPECT_EQ(scorpio_utils::testing::LifetimeHelper::get_created_count(), 2);
  EXPECT_EQ(scorpio_utils::testing::LifetimeHelper::get_destroyed_count(), 2);
  EXPECT_EQ(scorpio_utils::testing::LifetimeHelper::get_existing_count(), 0);
}

TEST_F(LifetimeHelperTest, MoveConstructor) {
  EXPECT_EQ(scorpio_utils::testing::LifetimeHelper::get_created_count(), 0);
  EXPECT_EQ(scorpio_utils::testing::LifetimeHelper::get_destroyed_count(), 0);
  EXPECT_EQ(scorpio_utils::testing::LifetimeHelper::get_existing_count(), 0);
  {
    scorpio_utils::testing::LifetimeHelper helper1;
    {
      scorpio_utils::testing::LifetimeHelper helper2(std::move(helper1));
      EXPECT_EQ(helper1.get_copy_count(), 0);
      EXPECT_EQ(helper1.get_move_count(), 1);
      EXPECT_EQ(helper1.get_copy_assign_count(), 0);
      EXPECT_EQ(helper1.get_move_assign_count(), 0);
      EXPECT_EQ(helper2.get_copy_count(), 0);
      EXPECT_EQ(helper2.get_move_count(), 0);
      EXPECT_EQ(helper2.get_copy_assign_count(), 0);
      EXPECT_EQ(helper2.get_move_assign_count(), 0);
      EXPECT_EQ(scorpio_utils::testing::LifetimeHelper::get_created_count(), 2);
      EXPECT_EQ(scorpio_utils::testing::LifetimeHelper::get_destroyed_count(), 0);
      EXPECT_EQ(scorpio_utils::testing::LifetimeHelper::get_existing_count(), 2);
    }
    EXPECT_EQ(scorpio_utils::testing::LifetimeHelper::get_created_count(), 2);
    EXPECT_EQ(scorpio_utils::testing::LifetimeHelper::get_destroyed_count(), 1);
    EXPECT_EQ(scorpio_utils::testing::LifetimeHelper::get_existing_count(), 1);
  }
  EXPECT_EQ(scorpio_utils::testing::LifetimeHelper::get_created_count(), 2);
  EXPECT_EQ(scorpio_utils::testing::LifetimeHelper::get_destroyed_count(), 2);
  EXPECT_EQ(scorpio_utils::testing::LifetimeHelper::get_existing_count(), 0);
}

TEST_F(LifetimeHelperTest, MoveAssignment) {
  EXPECT_EQ(scorpio_utils::testing::LifetimeHelper::get_created_count(), 0);
  EXPECT_EQ(scorpio_utils::testing::LifetimeHelper::get_destroyed_count(), 0);
  EXPECT_EQ(scorpio_utils::testing::LifetimeHelper::get_existing_count(), 0);
  {
    scorpio_utils::testing::LifetimeHelper helper1;
    {
      scorpio_utils::testing::LifetimeHelper helper2;
      helper2 = std::move(helper1);
      EXPECT_EQ(helper1.get_copy_count(), 0);
      EXPECT_EQ(helper1.get_move_count(), 1);
      EXPECT_EQ(helper1.get_copy_assign_count(), 0);
      EXPECT_EQ(helper1.get_move_assign_count(), 0);
      EXPECT_EQ(helper2.get_copy_count(), 0);
      EXPECT_EQ(helper2.get_move_count(), 0);
      EXPECT_EQ(helper2.get_copy_assign_count(), 0);
      EXPECT_EQ(helper2.get_move_assign_count(), 1);
      helper2 = std::move(helper1);
      EXPECT_EQ(helper1.get_move_count(), 2);
      EXPECT_EQ(helper2.get_move_assign_count(), 2);
      EXPECT_EQ(scorpio_utils::testing::LifetimeHelper::get_created_count(), 2);
      EXPECT_EQ(scorpio_utils::testing::LifetimeHelper::get_destroyed_count(), 0);
      EXPECT_EQ(scorpio_utils::testing::LifetimeHelper::get_existing_count(), 2);
    }
    EXPECT_EQ(scorpio_utils::testing::LifetimeHelper::get_created_count(), 2);
    EXPECT_EQ(scorpio_utils::testing::LifetimeHelper::get_destroyed_count(), 1);
    EXPECT_EQ(scorpio_utils::testing::LifetimeHelper::get_existing_count(), 1);
  }
  EXPECT_EQ(scorpio_utils::testing::LifetimeHelper::get_created_count(), 2);
  EXPECT_EQ(scorpio_utils::testing::LifetimeHelper::get_destroyed_count(), 2);
  EXPECT_EQ(scorpio_utils::testing::LifetimeHelper::get_existing_count(), 0);
}

TEST_F(LifetimeHelperTest, ResetCounters) {
  EXPECT_EQ(scorpio_utils::testing::LifetimeHelper::get_existing_count(), 0);
  EXPECT_EQ(scorpio_utils::testing::LifetimeHelper::get_created_count(), 0);
  EXPECT_EQ(scorpio_utils::testing::LifetimeHelper::get_destroyed_count(), 0);
  EXPECT_TRUE(scorpio_utils::testing::LifetimeHelper::reset_counters());
  {
    scorpio_utils::testing::LifetimeHelper helper;
    EXPECT_FALSE(scorpio_utils::testing::LifetimeHelper::reset_counters());
  }
  EXPECT_EQ(scorpio_utils::testing::LifetimeHelper::get_existing_count(), 0);
  EXPECT_EQ(scorpio_utils::testing::LifetimeHelper::get_created_count(), 1);
  EXPECT_EQ(scorpio_utils::testing::LifetimeHelper::get_destroyed_count(), 1);
  EXPECT_TRUE(scorpio_utils::testing::LifetimeHelper::reset_counters());
  EXPECT_EQ(scorpio_utils::testing::LifetimeHelper::get_existing_count(), 0);
  EXPECT_EQ(scorpio_utils::testing::LifetimeHelper::get_created_count(), 0);
  EXPECT_EQ(scorpio_utils::testing::LifetimeHelper::get_destroyed_count(), 0);
}

TEST_F(LifetimeHelperTest, printingState) {
  testing::internal::CaptureStdout();
  std::cout << scorpio_utils::testing::LifetimeHelper::EventType::CREATED;
  EXPECT_EQ(testing::internal::GetCapturedStdout(), "CREATED");
  testing::internal::CaptureStdout();
  std::cout << scorpio_utils::testing::LifetimeHelper::EventType::COPY;
  EXPECT_EQ(testing::internal::GetCapturedStdout(), "COPY");
  testing::internal::CaptureStdout();
  std::cout << scorpio_utils::testing::LifetimeHelper::EventType::MOVE;
  EXPECT_EQ(testing::internal::GetCapturedStdout(), "MOVE");
  testing::internal::CaptureStdout();
  std::cout << scorpio_utils::testing::LifetimeHelper::EventType::COPY_ASSIGN;
  EXPECT_EQ(testing::internal::GetCapturedStdout(), "COPY_ASSIGN");
  testing::internal::CaptureStdout();
  std::cout << scorpio_utils::testing::LifetimeHelper::EventType::MOVE_ASSIGN;
  EXPECT_EQ(testing::internal::GetCapturedStdout(), "MOVE_ASSIGN");
  testing::internal::CaptureStdout();
  std::cout << scorpio_utils::testing::LifetimeHelper::EventType::HAS_BEEN_COPIED;
  EXPECT_EQ(testing::internal::GetCapturedStdout(), "HAS_BEEN_COPIED");
  testing::internal::CaptureStdout();
  std::cout << scorpio_utils::testing::LifetimeHelper::EventType::HAS_BEEN_MOVED;
  EXPECT_EQ(testing::internal::GetCapturedStdout(), "HAS_BEEN_MOVED");

  scorpio_utils::testing::LifetimeHelper::EventType v;
  *reinterpret_cast<char*>(&v) = static_cast<char>(255);
  testing::internal::CaptureStdout();
  std::cout << v;
  EXPECT_EQ(testing::internal::GetCapturedStdout(), "<UNKNOWN>");
}
