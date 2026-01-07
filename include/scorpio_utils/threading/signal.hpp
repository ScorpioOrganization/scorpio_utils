#pragma once

#include <array>
#include <atomic>
#include <exception>
#include <optional>
#include <string>
#include <thread>
#include <utility>
#include "scorpio_utils/decorators.hpp"
#include "scorpio_utils/threading/eager_select.hpp"
#include "scorpio_utils/type_traits.hpp"

namespace scorpio_utils::threading {
class SignalException : std::exception {
  std::string msg;

public:
  explicit SignalException(std::string message)
  : msg(std::move(message)) { }
  SCU_ALWAYS_INLINE const char * what() const noexcept override {
    return msg.c_str();
  }
};

class Signal {
  std::atomic<int> _futex;

public:
  SCU_ALWAYS_INLINE bool SCU_EAGER_SELECT_IS_READY() {
    auto current = _futex.load(std::memory_order_relaxed);
    while (current > 0 && !_futex.compare_exchange_weak(current, current - 1,
      std::memory_order_relaxed, std::memory_order_relaxed)) { }
    SCU_UNLIKELY_THROW_IF(current < 0, SignalException, "Signal is closed");
    return current > 0;
  }

  SCU_ALWAYS_INLINE void SCU_EAGER_SELECT_GET_VALUE() noexcept { }

  SCU_ALWAYS_INLINE Signal(int initial = 0)
  : _futex(initial) { }
  ~Signal() = default;

  SCU_UNCOPYBLE(Signal);
  SCU_UNMOVABLE(Signal);

  void notify(int count);
  SCU_ALWAYS_INLINE auto notify_one() {
    return notify(1);
  }
  void wait();
  void close();
  SCU_ALWAYS_INLINE auto count() const noexcept {
    return _futex.load(std::memory_order_relaxed);
  }
  /**
   * Attempts to take the signal without blocking.
   */
  SCU_ALWAYS_INLINE bool try_take() {
    return SCU_EAGER_SELECT_IS_READY();
  }
};
}  // namespace scorpio_utils::threading
