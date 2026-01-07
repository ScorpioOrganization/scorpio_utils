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
