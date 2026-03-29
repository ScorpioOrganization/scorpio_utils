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

#include "scorpio_utils/threading/thread_pool.hpp"

void scorpio_utils::threading::ThreadPool::thread_worker() {
  std::optional<UniqueFunction<void()>> task;
  while (!_stop.load(std::memory_order_relaxed)) {
    while (_threads_count.load(std::memory_order_relaxed) >= _living_threads.load(std::memory_order_relaxed) &&
      (task = _tasks->receive()).has_value()) {
      task->call();
      task.reset();
    }
    if (_threads_count.load(std::memory_order_relaxed) <
      _pseudo_living_threads.fetch_sub(1, std::memory_order_relaxed)) {
      _living_threads.fetch_sub(1, std::memory_order_relaxed);
      return;
    }
    _pseudo_living_threads.fetch_add(1, std::memory_order_relaxed);
    std::unique_lock<std::mutex> lock(_mutex);
    _condition.wait(lock, [this]() {
        return _tasks->available() > 0 || _stop.load(std::memory_order_relaxed) || _threads_count.load(
        std::memory_order_relaxed) < _living_threads.load(std::memory_order_relaxed);
      });
  }
  _pseudo_living_threads.fetch_sub(1, std::memory_order_relaxed);
  _living_threads.fetch_sub(1, std::memory_order_relaxed);
}

scorpio_utils::threading::ThreadPool::ThreadPool(const size_t threads_count)
: _tasks(std::make_unique<decltype(_tasks)::element_type>()),
  _stop(false),
  _threads_count(0),
  _living_threads(0),
  _pseudo_living_threads(0) {
  set_threads_count(threads_count);
}

scorpio_utils::threading::ThreadPool::~ThreadPool() {
  // We have to ensure that every task is completed
  work();
  _stop = true;
  do {
    _condition.notify_all();
    std::this_thread::yield();
  } while (_living_threads.load() != 0);
}

void scorpio_utils::threading::ThreadPool::set_threads_count(const size_t threads_count, const bool wait) {
  _threads_count.store(threads_count, std::memory_order_relaxed);
  auto living_threads = _living_threads.load(std::memory_order_relaxed);
  if (threads_count > living_threads) {
    auto n = threads_count - living_threads;
    for (size_t i = 0; i < n; ++i) {
      _pseudo_living_threads.fetch_add(1, std::memory_order_relaxed);
      _living_threads.fetch_add(1, std::memory_order_relaxed);
      std::thread(&ThreadPool::thread_worker, this).detach();
    }
  } else if (threads_count < living_threads) {
    _condition.notify_all();
    if (wait) {
      while (_living_threads.load(std::memory_order_relaxed) != _threads_count.load(std::memory_order_relaxed)) {
        _condition.notify_all();
        std::this_thread::yield();
      }
    }
  }
}

void scorpio_utils::threading::ThreadPool::work() {
  while (auto task = _tasks->receive()) {
    task->call();
  }
}
