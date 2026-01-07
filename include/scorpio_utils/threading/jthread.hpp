#pragma once

#include <thread>
#include <utility>
#include "scorpio_utils/decorators.hpp"

namespace scorpio_utils::threading {
class JThread : public std::thread {
public:
  template<typename ... Args>
  SCU_ALWAYS_INLINE explicit JThread(Args&&... args)
  : std::thread(std::forward<Args>(args)...) { }
  SCU_ALWAYS_INLINE ~JThread() {
    if (joinable()) {
      join();
    }
  }
};

#define SCU_JTHREAD(...) scorpio_utils::threading::JThread SCU_UNIQUE_NAME(scorpio_jthread)(__VA_ARGS__)

}  // namespace scorpio_utils::threading
