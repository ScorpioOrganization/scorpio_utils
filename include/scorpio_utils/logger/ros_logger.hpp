#pragma once

#include <string>
#include <utility>

#include <rclcpp/rclcpp.hpp>
#include "scorpio_utils/logger/logger.hpp"

namespace scorpio_utils::logger {
class ROSLogger : public Logger {
public:
  explicit ROSLogger(rclcpp::Logger logger)
  : _logger(std::move(logger)) {}

  void log(Level level, std::string&& message) override {
    switch (level) {
      case Level::FATAL:
      case Level::ERROR:   RCLCPP_ERROR(_logger, "%s", message.c_str()); break;
      case Level::WARNING: RCLCPP_WARN (_logger, "%s", message.c_str()); break;
      case Level::INFO:    RCLCPP_INFO (_logger, "%s", message.c_str()); break;
      case Level::DEBUG:   RCLCPP_DEBUG(_logger, "%s", message.c_str()); break;
      case Level::TRACE:   RCLCPP_DEBUG(_logger, "%s", message.c_str()); break;
    }
  }

private:
  rclcpp::Logger _logger;
};
}  // namespace scorpio_utils::logger
