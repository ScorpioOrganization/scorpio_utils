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

#include <atomic>
#include <condition_variable>
#include <memory>
#include <mutex>
#include <tuple>
#include <type_traits>
#include <utility>
#include <vector>
#include "declaration/and_then_declaration.hpp"
#include "waitable.hpp"

namespace scorpio_utils::threading {
class WaitGroup : public WaitableBase, public AndThen<WaitGroup> {
  friend class AndThen<WaitGroup>;
  friend class threading::WaitableBase;

  mutable std::mutex _mutex;
  mutable std::condition_variable _cv;
  std::vector<std::shared_ptr<WaitableBase>> _tasks;
  std::atomic<size_t> _count;
  std::atomic<bool> _closed;

  void empty_group() {
    WaitableBase::done();
    if (_closed) {
      AndThen::call();
    }
    _cv.notify_all();
  }

  void task_done() {
    if (_count.fetch_sub(1, std::memory_order_relaxed) == 1) {
      empty_group();
    }
  }

  SCU_ALWAYS_INLINE WaitGroup * get_wait_group() {
    return this;
  }

  std::tuple<> get_value() const { return std::tuple<>(); }
  std::tuple<> take_value() { return std::tuple<>(); }

  std::lock_guard<std::mutex> lock_mutex() const {
    return std::lock_guard<std::mutex>(_mutex);
  }

public:
  using shared_ptr = std::shared_ptr<WaitGroup>;

  WaitGroup()
  : AndThen<WaitGroup>(), _count(0), _closed(false) { }

  WaitGroup& operator=(const WaitGroup& other) {
    std::unique_lock<std::mutex> lock(_mutex, std::defer_lock);
    std::unique_lock<std::mutex> other_lock(other._mutex, std::defer_lock);
    std::lock(lock, other_lock);
    _tasks = other._tasks;
    other_lock.unlock();
    _count = _tasks.size();
    for (auto& waitable : _tasks) {
      std::lock_guard<std::mutex> waitable_lock(waitable->_mutex);
      waitable->wait_with(this);
    }
    if (_count == 0) {
      empty_group();
    }
    return *this;
  }

  WaitGroup& operator=(WaitGroup&& other) {
    std::unique_lock<std::mutex> lock(_mutex, std::defer_lock);
    std::unique_lock<std::mutex> other_lock(other._mutex, std::defer_lock);
    std::lock(lock, other_lock);
    _tasks = std::move(other._tasks);
    other_lock.unlock();
    _count = _tasks.size();
    for (auto& waitable : _tasks) {
      std::unique_lock<std::mutex> waitable_lock(waitable->_mutex, std::defer_lock);
      std::lock(other_lock, waitable_lock);
      waitable->wait_with(this);
      waitable->remove_wait_group(other.get_wait_group());
      other_lock.unlock();
    }
    other_lock.lock();
    _closed.store(other._closed.load(std::memory_order_relaxed), std::memory_order_relaxed);
    other._count = 0;
    other._tasks.clear();
    other_lock.unlock();
    other._cv.notify_all();
    if (_count == 0) {
      empty_group();
    }
    return *this;
  }

  WaitGroup(const WaitGroup& other) {
    *this = other;
  }

  WaitGroup(WaitGroup&& other) {
    *this = std::move(other);
  }

  ~WaitGroup() {
    std::unique_lock<std::mutex> lock(_mutex, std::defer_lock);
    for (auto& waitable : _tasks) {
      std::unique_lock<std::mutex> lock2(waitable->_mutex, std::defer_lock);
      std::lock(lock, lock2);
      waitable->remove_wait_group(this);
      lock.unlock();
    }
  }

  void add(std::shared_ptr<WaitableBase> waitable) {
    SCU_ASSERT(!_closed.load(std::memory_order_relaxed), "Cannot add a task to a closed WaitGroup");
    std::unique_lock<std::mutex> lock(_mutex, std::defer_lock);
    std::unique_lock<std::mutex> lock2(waitable->_mutex, std::defer_lock);
    std::lock(lock, lock2);
    ++_count;
    waitable->wait_with(this);
    _tasks.emplace_back(std::move(waitable));
  }

  template<typename T>
  SCU_ALWAYS_INLINE constexpr void add(std::shared_ptr<T> waitable) {
    static_assert(std::is_base_of_v<WaitableBase, T>, "Type T shall be derived from WaitableBase.");
    add(std::static_pointer_cast<WaitableBase>(std::move(waitable)));
  }

  template<typename T>
  SCU_ALWAYS_INLINE constexpr WaitGroup& operator<<(std::shared_ptr<T> waitable) {
    add(std::move(waitable));
    return *this;
  }

  // Closes the wait group, preventing any new tasks from being added
  SCU_ALWAYS_INLINE void close() {
    _closed.store(true, std::memory_order_relaxed);
  }

  SCU_ALWAYS_INLINE void wait() const {
    std::unique_lock<std::mutex> lock(_mutex);
    _cv.wait(lock, [this]() { return _count == 0; });
  }

  SCU_ALWAYS_INLINE size_t count() const {
    return _count;
  }
};
}  // namespace scorpio_utils::threading
