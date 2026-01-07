#include "scorpio_utils/time_utils/time_string.hpp"

#include <chrono>
#include <cstddef>
#include <ctime>
#include <iomanip>

std::string scorpio_utils::time_utils::time_string(int64_t nanosec, const char* format) {
  std::chrono::nanoseconds timestamp(nanosec);
  std::chrono::time_point<std::chrono::system_clock> now(timestamp);
  auto time = std::chrono::system_clock::to_time_t(now);
  std::tm* tm = std::localtime(&time);
  std::stringstream ss;
  ss << std::put_time(tm, format);
  return ss.str();
}
