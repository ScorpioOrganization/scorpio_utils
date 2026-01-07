#include "scorpio_utils/time_provider/lazy_time_provider.hpp"
#include "scorpio_utils/decorators.hpp"

void scorpio_utils::time_provider::LazyTimeProvider::thread_worker() {
  while (SCU_LIKELY(_running)) {
    _time.store(std::chrono::system_clock::now().time_since_epoch().count(), std::memory_order_relaxed);
    std::this_thread::sleep_for(std::chrono::nanoseconds(_time_offset.load()));
  }
}

scorpio_utils::time_provider::LazyTimeProvider::LazyTimeProvider(const int64_t offset)
: _time(std::chrono::system_clock::now().time_since_epoch().count()),
  _time_offset(offset),
  _running(true),
  _thread(&LazyTimeProvider::thread_worker, this) {
}

scorpio_utils::time_provider::LazyTimeProvider::~LazyTimeProvider() {
  _running.store(false, std::memory_order_relaxed);
  _thread.join();
}
