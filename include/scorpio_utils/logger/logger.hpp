#pragma once

#include <fmt/core.h>
#include <string>
#include <sstream>
#include <type_traits>
#include <utility>
#include "scorpio_utils/magic_enum_include.hpp"
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
// This allows for changing logger formatting without changing the logger formatting globally
template<typename Arg>
struct LoggerFormatter {
  SCU_ALWAYS_INLINE static constexpr decltype(auto) format(Arg && arg) {
    using decayed = std::decay_t<Arg>;
    if constexpr (std::is_same_v<decayed, bool>) {
      return arg ? "true" : "false";
    } else if constexpr (std::is_enum_v<decayed>) {
      return ::magic_enum::enum_name(std::forward<Arg>(arg));
    } else if constexpr (std::is_pointer_v<decayed>) {
      if (arg == nullptr) {
        return "nullptr";
      } else {
        return (std::stringstream() << '{' << LoggerFormatter<decltype(*arg)>::format(*arg) << "}@" <<
               static_cast<uintptr_t>(arg)).str();
      }
    } else {
      return std::forward<Arg>(arg);
    }
  }
};
template<typename ... Args>
SCU_ALWAYS_INLINE std::string logger_formater_helper(std::string&& message, Args&&... args) {
  return ::fmt::format(message, (LoggerFormatter<Args>::format(std::forward<Args>(args)))...);
}
}  // namespace scorpio_utils::logger

#ifndef SCU_LOG_LEVEL
# define SCU_LOG_LEVEL 3
#endif

#define SCU_LOG(loggerPtr, level, ...) \
  do { \
    if (loggerPtr) { \
      loggerPtr->log(level, ::scorpio_utils::logger::logger_formater_helper(__VA_ARGS__)); \
    } \
  } while (0)

#if SCU_LOG_LEVEL >= 0
# define SCU_LOG_FATAL(loggerPtr, ...) \
  SCU_LOG(loggerPtr, ::scorpio_utils::logger::Logger::Level::FATAL, __VA_ARGS__)
#else
# define SCU_LOG_FATAL(loggerPtr, ...)
#endif
#if SCU_LOG_LEVEL >= 1
# define SCU_LOG_ERROR(loggerPtr, ...) \
  SCU_LOG(loggerPtr, ::scorpio_utils::logger::Logger::Level::ERROR, __VA_ARGS__)
#else
# define SCU_LOG_ERROR(loggerPtr, ...)
#endif
#if SCU_LOG_LEVEL >= 2
# define SCU_LOG_WARNING(loggerPtr, ...) \
  SCU_LOG(loggerPtr, ::scorpio_utils::logger::Logger::Level::WARNING, __VA_ARGS__)
#else
# define SCU_LOG_WARNING(loggerPtr, ...)
#endif
#if SCU_LOG_LEVEL >= 3
# define SCU_LOG_INFO(loggerPtr, ...) \
  SCU_LOG(loggerPtr, ::scorpio_utils::logger::Logger::Level::INFO, __VA_ARGS__)
#else
# define SCU_LOG_INFO(loggerPtr, ...)
#endif
#if SCU_LOG_LEVEL >= 4
# define SCU_LOG_DEBUG(loggerPtr, ...) \
  SCU_LOG(loggerPtr, ::scorpio_utils::logger::Logger::Level::DEBUG, __VA_ARGS__)
#else
# define SCU_LOG_DEBUG(loggerPtr, ...)
#endif
#if SCU_LOG_LEVEL >= 5
# define SCU_LOG_TRACE(loggerPtr, ...) \
  SCU_LOG(loggerPtr, ::scorpio_utils::logger::Logger::Level::TRACE, __VA_ARGS__)
#else
# define SCU_LOG_TRACE(loggerPtr, ...)
#endif
