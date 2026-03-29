#pragma once

#include <string>
#include <fmt/core.h>
#include "scorpio_utils/decorators.hpp"

namespace scorpio_utils::logger {
class Logger {
public:
  enum Level {
    FATAL = 0,
    ERROR = 1,
    WARNING = 2,
    INFO = 3,
    DEBUG = 4,
    TRACE = 5,
  };
  virtual ~Logger() = default;
  virtual void log(Level level, std::string&& message) = 0;
};
template<typename ... Args>
SCU_ALWAYS_INLINE std::string logger_formater_helper(std::string&& message, int, Args&&... args) {
  return fmt::format(message, std::forward<Args>(args)...);
}
} // namespace scorpio_utils::logger

#ifndef SCU_LOG_LEVEL
# define SCU_LOG_LEVEL 3
#endif

#define SCU_LOG(logger, level, ...) \
  do { \
    if (logger) { \
      logger->log(level, ::fmt::format(__VA_ARGS__)); \
    } \
  } while (0)

#if SCU_LOG_LEVEL >= 0
# define SCU_LOG_FATAL(loggerPtr, ...) \
  SCU_LOG(loggerPtr, scorpio_utils::logger::Logger::Level::FATAL, __VA_ARGS__)
#else
# define SCU_LOG_FATAL(loggerPtr, ...)
#endif
#if SCU_LOG_LEVEL >= 1
# define SCU_LOG_ERROR(loggerPtr, ...) \
  SCU_LOG(loggerPtr, scorpio_utils::logger::Logger::Level::ERROR, __VA_ARGS__)
#else
# define SCU_LOG_ERROR(loggerPtr, ...)
#endif
#if SCU_LOG_LEVEL >= 2
# define SCU_LOG_WARNING(loggerPtr, ...) \
  SCU_LOG(loggerPtr, scorpio_utils::logger::Logger::Level::WARNING, __VA_ARGS__)
#else
# define SCU_LOG_WARNING(loggerPtr, ...)
#endif
#if SCU_LOG_LEVEL >= 3
# define SCU_LOG_INFO(loggerPtr, ...) \
  SCU_LOG(loggerPtr, scorpio_utils::logger::Logger::Level::INFO, __VA_ARGS__)
#else
# define SCU_LOG_INFO(loggerPtr, ...)
#endif
#if SCU_LOG_LEVEL >= 4
# define SCU_LOG_DEBUG(loggerPtr, ...) \
  SCU_LOG(loggerPtr, scorpio_utils::logger::Logger::Level::DEBUG, __VA_ARGS__)
#else
# define SCU_LOG_DEBUG(loggerPtr, ...)
#endif
#if SCU_LOG_LEVEL >= 5
# define SCU_LOG_TRACE(loggerPtr, ...) \
  SCU_LOG(loggerPtr, scorpio_utils::logger::Logger::Level::TRACE, __VA_ARGS__)
#else
# define SCU_LOG_TRACE(loggerPtr, ...)
#endif
