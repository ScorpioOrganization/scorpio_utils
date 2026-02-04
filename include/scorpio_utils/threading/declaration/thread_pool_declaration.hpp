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
#include <list>
#include <memory>
#include <mutex>
#include <thread>
#include <type_traits>
#include "scorpio_utils/threading/channel.hpp"
#include "scorpio_utils/unique_function.hpp"

namespace scorpio_utils::threading {
class WaitGroup;

template<class Self, typename ... Args>
class AndThen;

template<typename T>
class Future;

class ThreadPool {
  template<class Self, typename ... Args>
  friend class AndThen;

  std::mutex _mutex;
  std::condition_variable _condition;
  std::unique_ptr<Channel<UniqueFunction<void()>, 1024 * 1024>> _tasks;
  std::atomic<bool> _stop;
  std::atomic<size_t> _threads_count;
  std::atomic<size_t> _living_threads;
  std::atomic<size_t> _pseudo_living_threads;

  void thread_worker();

  void add_threads(size_t n);

  template<typename Return, typename Fn>
  constexpr void async_call(Fn&& task, std::shared_ptr<Future<Return>> future);

  ThreadPool(const ThreadPool&) = delete;
  ThreadPool& operator=(const ThreadPool&) = delete;

public:
  using shared_ptr = std::shared_ptr<ThreadPool>;

  explicit ThreadPool(const size_t threads_count);

  ~ThreadPool();

  template<typename Fn, bool Wait = true>
  std::conditional_t<Wait, void, bool> add_task(Fn&& task);

  template<typename Fn>
  [[nodiscard]] constexpr auto async(Fn&& task) -> typename Future<std::invoke_result_t<Fn>>::shared_ptr;

  SCU_ALWAYS_INLINE size_t threads_count() const {
    return _threads_count.load(std::memory_order_relaxed);
  }

  void set_threads_count(size_t threads_count, bool wait = false);

  SCU_ALWAYS_INLINE size_t spawned_threads_count() const {
    return _living_threads.load(std::memory_order_relaxed);
  }

  template<typename Iter, typename Fn>
  constexpr std::shared_ptr<WaitGroup> for_each(Iter begin, const Iter& end, const Fn& task);

  template<typename N, typename Fn>
  constexpr std::shared_ptr<WaitGroup> for_each_range(N begin, const N end, const Fn& task);

  void work();

  SCU_ALWAYS_INLINE auto tasks_count() const {
    return _tasks->available();
  }

  SCU_ALWAYS_INLINE auto is_nonblocking() const {
    return _tasks->is_write_ready();
  }
};
}  // namespace scorpio_utils::threading
