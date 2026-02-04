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
