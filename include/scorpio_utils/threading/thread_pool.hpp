#pragma once

#include <memory>
#include <utility>
#include "declaration/thread_pool_declaration.hpp"
#include "wait_group.hpp"
#include "future.hpp"

template<typename Return, typename Fn>
constexpr void scorpio_utils::threading::ThreadPool::async_call(
  Fn&& task,
  std::shared_ptr<Future<Return>> future) {
  static_assert(std::is_invocable_v<Fn>, "Task must be invocable");
  static_assert(std::is_same_v<Return, std::invoke_result_t<Fn>>,
                "Return type must match the task's return type");
  add_task([this, task = std::move(task), future = std::move(future)]() mutable {
      future->set_started();
      if constexpr (std::is_same_v<Return, void>) {
        task();
        future->set_done();
      } else {
        future->set_done(task());
      }
   });
}

template<typename Fn, bool Wait>
std::conditional_t<Wait, void, bool> scorpio_utils::threading::ThreadPool::add_task(Fn&& task) {
  static_assert(std::is_invocable_v<Fn>, "Task must be invocable");
  static_assert(std::is_same_v<std::invoke_result_t<Fn>, void>,
                "Task must return void, use async() tasks returning anything");
  if constexpr (Wait) {
    _tasks->send<true>(std::move(task));
    _condition.notify_one();
  } else {
    // It isn't unlikely always, but in normal usage it is
    // I don't believe that it should ever happen, in our usage
    if (SCU_UNLIKELY(_tasks->send<false>(std::move(task)))) {
      return false;
    }
    _condition.notify_one();
    return true;
  }
}

template<typename Fn>
constexpr auto scorpio_utils::threading::ThreadPool::async(
  Fn&& task
) -> typename Future<std::invoke_result_t<Fn>>::shared_ptr {
  static_assert(std::is_invocable_v<Fn>, "Task must be invocable");
  using ReturnType = std::invoke_result_t<Fn>;
  auto future = typename Future<ReturnType>::shared_ptr(new Future<ReturnType>());
  async_call(std::move(task), future);
  return future;
}

template<typename Iter, typename Fn>
constexpr scorpio_utils::threading::WaitGroup::shared_ptr scorpio_utils::threading::ThreadPool::for_each(
  Iter begin, const Iter& end, const Fn& task) {
  static_assert(std::is_invocable_v<Fn, typename std::iterator_traits<Iter>::value_type>,
                "Task must be invocable with the iterator's value type");
  static_assert(std::is_same_v<std::invoke_result_t<Fn, typename std::iterator_traits<Iter>::value_type>, void>,
                "Task must return void, use async() tasks returning anything");
  WaitGroup::shared_ptr wait_group = std::make_shared<WaitGroup>();
  for (; begin != end; ++begin) {
    const auto future = async([task, begin = *begin]() mutable { task(begin); });
    wait_group->add(std::move(std::static_pointer_cast<WaitableBase>(future)));
  }
  return wait_group;
}

template<typename N, typename Fn>
constexpr scorpio_utils::threading::WaitGroup::shared_ptr scorpio_utils::threading::ThreadPool::for_each_range(
  N begin, const N end, const Fn& task) {
  static_assert(std::is_integral_v<N>, "N must be an integral type");
  static_assert(std::is_invocable_v<Fn, N>, "Task must be invocable with N");
  static_assert(std::is_same_v<std::invoke_result_t<Fn, N>, void>,
                "Task must return void, use async() tasks returning anything");
  WaitGroup::shared_ptr wait_group = std::make_shared<WaitGroup>();
  if (begin >= end) {
    return wait_group;
  }
  const auto len = static_cast<size_t>(end - begin);
  const auto threads_count = _threads_count.load(std::memory_order_relaxed);
  const auto work_per_thread = static_cast<N>(len / threads_count);
  for (auto work_extra = len % threads_count; work_extra > 0; --work_extra) {
    const auto future = async([task, begin, work_per_thread]() mutable {
          for (N i = 0; i <= work_per_thread; ++i) {
            task(static_cast<N>(begin + i));
          }
    });
    wait_group->add(std::static_pointer_cast<WaitableBase>(future));
    begin += static_cast<N>(work_per_thread + 1);
  }
  for (; begin < end; begin += work_per_thread) {
    const auto future = async([task, begin, work_per_thread]() mutable {
          for (N i = 0; i < work_per_thread; ++i) {
            task(static_cast<N>(begin + i));
          }
    });
    wait_group->add(std::static_pointer_cast<WaitableBase>(future));
  }
  return wait_group;
}

#ifdef SCU_THREADING_THREAD_POOL_SINGLETON
#include <algorithm>
scorpio_utils::threading::ThreadPool thread_pool(std::max(std::thread::hardware_concurrency(), 1u));
inline scorpio_utils::threading::ThreadPool::shared_ptr thread_pool_shared =
  std::shared_ptr<scorpio_utils::threading::ThreadPool>(&thread_pool, [](
      auto) { });
#endif
