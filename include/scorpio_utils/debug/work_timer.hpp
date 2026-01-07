#pragma once


// #define WORK_TIMER_ENABLE
#define WORK_TIMER_ENABLE_ROS_PRINT

#ifdef WORK_TIMER_ENABLE

#include <iostream>
#include <optional>
#include <string>
#include <unordered_map>
#include <utility>
#include "stopwatch.hpp"

#ifdef WORK_TIMER_ENABLE_ROS_PRINT
#include "rclcpp/rclcpp.hpp"
#endif

#warning "Debug is not supported yet"

namespace debug {
template<typename T>
class WorkTimer {
  std::unordered_map<T, std::pair<size_t, Stopwatch>> _stopwatches;

public:
  inline WorkTimer() = default;

  inline void start(const T& work) {
    _stopwatches[work].second.resume();
  }

  inline void finish(const T& work) {
    auto x = _stopwatches.find(work);
    x->second.second.stop();
    ++x->second.first;
  }

  inline double get_time(const T& work) const {
    return _stopwatches.at(work).elapsed();
  }

  inline void reset(const T& work) {
    _stopwatches[work].reset();
  }

  inline void clear() {
    _stopwatches.clear();
  }

  inline void print() {
    for (const auto& [work, x] : _stopwatches) {
      std::cout << work << " in " << x.first << " calls: " << (x.second.total_elapsed()) << "ms\n";
    }
  }

  #ifdef WORK_TIMER_ENABLE_ROS_PRINT
  inline void print(const rclcpp::Logger& logger) {
    for (const auto& [work, x] : _stopwatches) {
      RCLCPP_INFO(logger, "%s in %zu calls: %lfms", work.c_str(), x.first, x.second.total_elapsed());
    }
  }
  #endif
};

template<typename T>
class WorkTimerGuard {
  T _work;
  WorkTimer<T>& _timer;

public:
  inline WorkTimerGuard(T work, WorkTimer<T>& timer)
  : _work(std::move(work)),
    _timer(timer) {
    _timer.start(_work);
  }

  inline ~WorkTimerGuard() {
    _timer.finish(_work);
  }
};

inline WorkTimer<std::string> timer;
}  // namespace debug


#define START_WORK(work) debug::timer.start(work)
#define START_WORK_GUARD(work) debug::WorkTimerGuard guard(std::string(work), debug::timer)
#define FINISH_WORK(work) debug::timer.finish(work)
#define PRINT_WORK_TIME() debug::timer.print()
#define PRINT_WORK_TIME_ROS(logger) debug::timer.print(logger)
#define TIME(name, work) START_WORK(name); work; FINISH_WORK(name)
#define CLEAR_WORK_TIMERS() debug::timer.clear()

#else

#define START_WORK(work)
#define START_WORK_GUARD(work)
#define FINISH_WORK(work)
#define PRINT_WORK_TIME()
#define TIME(name, work)
#define PRINT_WORK_TIME_ROS(logger)
#define CLEAR_WORK_TIMERS()

#endif
