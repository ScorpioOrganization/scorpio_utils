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
#include <functional>
#include <memory>
#include <mutex>
#include <optional>
#include <tuple>
#include <utility>
#include "declaration/thread_pool_declaration.hpp"
#include "declaration/and_then_declaration.hpp"
#include "waitable.hpp"

namespace scorpio_utils::threading {
template<typename T>
class Future : public WaitableBase, public AndThen<Future<T>, T> {
  friend class ThreadPool;
  template<class Self, typename ... Args>
  friend class AndThen;

  mutable std::condition_variable _cv;
  mutable std::mutex _mutex;
  std::optional<T> _result;
  std::atomic<bool> _started;
  std::atomic<bool> _done;

  Future()
  : WaitableBase(), _result(std::nullopt), _started(false), _done(false) { }
  Future(const Future&) = delete;
  Future(Future&&) = delete;
  Future& operator=(const Future&) = delete;
  Future& operator=(Future&&) = delete;

  void set_started() {
    _started.store(true, std::memory_order_relaxed);
  }

  void set_done(T&& result) {
    std::unique_lock<std::mutex> lock(_mutex);
    _result = std::optional<T>(std::move(result));
    AndThen<Future<T>, T>::call();
    WaitableBase::done();
    // Can be relaxed because lock sync is used
    _done.store(true, std::memory_order_relaxed);
    lock.unlock();
    _cv.notify_all();
  }

  std::tuple<std::reference_wrapper<const T>> get_value() const {
    return std::tuple<std::reference_wrapper<const T>>(std::cref(*_result));
  }

  std::tuple<T> take_value() {
    return std::tuple<T>(std::move(*_result));
  }

  std::lock_guard<std::mutex> lock_mutex() const {
    return std::lock_guard<std::mutex>(_mutex);
  }

public:
  using value_type = T;
  using shared_ptr = std::shared_ptr<Future<T>>;

  ~Future() {
    std::ignore = await();
  }

  template<typename U = T>
  std::enable_if_t<std::is_move_constructible_v<U>, T&&> await() && {
    std::unique_lock<std::mutex> lock(_mutex);
    _cv.wait(lock, [this]() { return _done.load(std::memory_order_relaxed); });
    std::atomic_thread_fence(std::memory_order_acquire);
    return *std::move(_result);
  }

  const T& await() const& {
    std::unique_lock<std::mutex> lock(_mutex);
    _cv.wait(lock, [this]() { return _done.load(std::memory_order_relaxed); });
    std::atomic_thread_fence(std::memory_order_acquire);
    return *_result;
  }

  SCU_ALWAYS_INLINE bool is_completed() const {
    return _done.load(std::memory_order_relaxed);
  }

  SCU_ALWAYS_INLINE bool is_started() const {
    return _started.load(std::memory_order_relaxed);
  }
};

template<>
class Future<void>: public WaitableBase, public AndThen<Future<void>> {
  friend class ThreadPool;
  template<class Self, typename ... Args>
  friend class AndThen;

  mutable std::condition_variable _cv;
  mutable std::mutex _mutex;
  std::atomic<bool> _started;
  std::atomic<bool> _done;

  Future()
  : WaitableBase(), _started(false), _done(false) { }
  Future(const Future&) = delete;
  Future& operator=(const Future&) = delete;
  Future(Future&&) = delete;
  Future& operator=(Future&&) = delete;

  void set_started() {
    _started.store(true, std::memory_order_relaxed);
  }

  void set_done() {
    std::unique_lock<std::mutex> lock(_mutex);
    _done.store(true, std::memory_order_relaxed);
    WaitableBase::done();
    AndThen::call();
    lock.unlock();
    _cv.notify_all();
  }

  std::tuple<> get_value() const { return std::tuple<>(); }
  std::tuple<> take_value() { return std::tuple<>(); }

  std::lock_guard<std::mutex> lock_mutex() const {
    return std::lock_guard<std::mutex>(_mutex);
  }

public:
  using value_type = void;
  using shared_ptr = std::shared_ptr<Future<void>>;

  ~Future() {
    await();
  }

  void await() const {
    std::unique_lock<std::mutex> lock(_mutex);
    _cv.wait(lock, [this]() -> bool { return _done.load(std::memory_order_relaxed); });
  }

  SCU_ALWAYS_INLINE bool is_completed() const {
    return _done.load(std::memory_order_relaxed);
  }
};

template<typename T>
struct IsFuture : std::false_type { };

template<typename T>
struct IsFuture<Future<T>>: std::true_type { };

template<typename T>
inline constexpr bool is_future_v = IsFuture<T>::value;

}  // namespace scorpio_utils::threading

#include "and_then.hpp"
