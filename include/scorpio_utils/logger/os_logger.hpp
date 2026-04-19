#pragma once

#include <fmt/core.h>
#include <atomic>
#include <string>
#include <thread>
#include <utility>
#include "scorpio_utils/logger/logger.hpp"
#include "scorpio_utils/magic_enum_include.hpp"
#include "scorpio_utils/threading/channel.hpp"
#include "scorpio_utils/time_provider/system_time_provider.hpp"
#include "scorpio_utils/time_utils/time_string.hpp"

namespace scorpio_utils::logger {
template<typename OS>
class OSLogger : public Logger {
  scorpio_utils::threading::Channel<std::pair<Logger::Level, std::string>, 1024 * 1024> _log_channel;
  OS _target;
  std::atomic<bool> _stop;
  std::thread _log_thread;
  scorpio_utils::time_provider::SystemTimeProvider _log_time_provider;

  void log_thread_func() {
    std::pair<Logger::Level, std::string> message;
    while (!_stop.load(std::memory_order_relaxed)) {
      try {
        message = _log_channel.receive<true>();
      } catch (const threading::ClosedChannelException&) {
        break;
      }
      _target << fmt::format("[{}] {}: {}\n", _log_time_provider.get_time(), magic_enum::enum_name(
          message.first), std::move(message.second));
    }
  }

public:
  template<typename ... Args>
  explicit OSLogger(Args&&... args)
  : _target(std::forward<Args>(args)...),
    _stop(false),
    _log_thread(&OSLogger::log_thread_func, this) {
  }

  ~OSLogger() {
    _stop.store(true, std::memory_order_relaxed);
    _log_channel.close();
    _log_thread.join();
  }

  void log(Logger::Level level, std::string&& message) override {
    _log_channel.send<true>(std::make_pair(level, std::move(message)));
  }
};
}  // namespace scorpio_utils::logger
