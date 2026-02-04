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

#include "scorpio_utils/testing/lifetime_helper.hpp"

std::atomic<size_t> scorpio_utils::testing::LifetimeHelper::_created_counter(0);
std::atomic<size_t> scorpio_utils::testing::LifetimeHelper::_destroyed_counter(0);

std::ostream& operator<<(std::ostream& str, scorpio_utils::testing::LifetimeHelper::EventType event) {
  switch (event) {
    case scorpio_utils::testing::LifetimeHelper::EventType::CREATED:
      str << "CREATED";
      break;
    case scorpio_utils::testing::LifetimeHelper::EventType::MOVE:
      str << "MOVE";
      break;
    case scorpio_utils::testing::LifetimeHelper::EventType::COPY:
      str << "COPY";
      break;
    case scorpio_utils::testing::LifetimeHelper::EventType::COPY_ASSIGN:
      str << "COPY_ASSIGN";
      break;
    case scorpio_utils::testing::LifetimeHelper::EventType::MOVE_ASSIGN:
      str << "MOVE_ASSIGN";
      break;
    case scorpio_utils::testing::LifetimeHelper::EventType::HAS_BEEN_COPIED:
      str << "HAS_BEEN_COPIED";
      break;
    case scorpio_utils::testing::LifetimeHelper::EventType::HAS_BEEN_MOVED:
      str << "HAS_BEEN_MOVED";
      break;
    default:
      str << "<UNKNOWN>";
      break;
  }
  return str;
}
