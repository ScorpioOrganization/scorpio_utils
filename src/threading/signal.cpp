#include "scorpio_utils/threading/signal.hpp"

extern "C" {
  #include <linux/futex.h>
  #include <sys/syscall.h>
  #include <unistd.h>
}

#include <cstring>

using scorpio_utils::threading::Signal;

void Signal::notify(int count) {
  SCU_ASSUME(count > 0);
  _futex.fetch_add(count, std::memory_order_relaxed);
  SCU_UNLIKELY_THROW_IF(::syscall(SYS_futex, &_futex, FUTEX_WAKE | FUTEX_PRIVATE_FLAG, count, nullptr, nullptr, 0) < 0,
    SignalException, std::string("Futex wake failed: ") + std::strerror(errno));
}

void Signal::close() {
  _futex.store(SCU_AS(int, -1e9), std::memory_order_relaxed);
  SCU_UNLIKELY_THROW_IF(::syscall(
    SYS_futex, &_futex, FUTEX_WAKE | FUTEX_PRIVATE_FLAG, INT32_MAX, nullptr, nullptr, 0) < 0, SignalException,
    std::string("Futex wake failed: ") + std::strerror(errno));
}

void Signal::wait() {
  while (SCU_UNLIKELY(!SCU_EAGER_SELECT_IS_READY())) {
    const auto error = ::syscall(SYS_futex, &_futex, FUTEX_WAIT | FUTEX_PRIVATE_FLAG, 0, nullptr, nullptr, 0);
    SCU_UNLIKELY_THROW_IF(error != 0 && errno != EAGAIN && errno != EINTR, SignalException,
      std::string("Futex wait failed: ") + std::strerror(errno));
  }
}
