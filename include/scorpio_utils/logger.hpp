#pragma once

#include <atomic>
#include <string>
#include <thread>
#include <utility>
#include "scorpio_utils/threading/channel.hpp"
#include "scorpio_utils/time_provider/system_time_provider.hpp"
#include "scorpio_utils/time_utils/time_string.hpp"

namespace scorpio_utils {
template<typename OS>
class Logger {
  scorpio_utils::threading::Channel<std::string, 1024 * 1024> _log_channel;
  OS _log_file;
  std::atomic<bool> _stop;
  std::thread _log_thread;
  scorpio_utils::time_provider::SystemTimeProvider _log_time_provider;

  void log_thread_func() {
    while (!_stop.load(std::memory_order_relaxed)) {
      std::string message;
      try {
        message = _log_channel.receive<true>();
      } catch (const threading::ClosedChannelException&) {
        break;
      }
      auto time = scorpio_utils::time_utils::time_string(
        _log_time_provider.get_time(),
        "%Y-%m-%d %H:%M:%S");
      _log_file << "[" << std::move(time) << "] " << std::move(message) << '\n';
    }
  }

public:
  template<typename ... Args>
  explicit Logger(Args&&... args)
  : _log_file(std::forward<Args>(args)...),
    _stop(false),
    _log_thread(&Logger::log_thread_func, this) {
  }

  ~Logger() {
    _stop.store(true, std::memory_order_relaxed);
    _log_channel.close();
    _log_thread.join();
  }

  void log(std::string&& message) {
    _log_channel.send<true>(std::move(message));
  }
};
}  // namespace scorpio_utils
