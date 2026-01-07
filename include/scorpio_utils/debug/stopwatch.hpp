#pragma once

#include <chrono>

#warning "Debug is not supported yet"

namespace debug {
class Stopwatch {
  std::chrono::time_point<std::chrono::high_resolution_clock> _start;
  double _total;

public:
  inline Stopwatch()
  : _start(std::chrono::high_resolution_clock::now()),
    _total(0.0) { }

  inline void reset() {
    _total = 0.0;
    _start = std::chrono::high_resolution_clock::now();
  }

  inline double elapsed_in_current_run() const {
    return std::chrono::duration_cast<std::chrono::duration<double, std::milli>>(
      std::chrono::high_resolution_clock::now() - _start).count();
  }

  inline double elapsed() const {
    return _total + elapsed_in_current_run();
  }

  inline void stop() {
    _total += elapsed_in_current_run();
    _start = std::chrono::high_resolution_clock::now();
  }

  inline double total_elapsed() const {
    return _total;
  }

  inline void resume() {
    _start = std::chrono::high_resolution_clock::now();
  }
};
}  // namespace debug
