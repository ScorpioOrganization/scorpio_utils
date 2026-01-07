#include "threading/thread_pool.hpp"

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
