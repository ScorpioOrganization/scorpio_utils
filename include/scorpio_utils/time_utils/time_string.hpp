#pragma once

#include <string>

namespace scorpio_utils::time_utils {

/**
 * Converts a time in nanoseconds to a formatted string.
 * The format can be specified using the `strftime` format specifiers.
 *
 * \param nanosec The time in nanoseconds to convert.
 * \param format The format string to use for conversion, default is "%Y-%m-%d_%H:%M:%S".
 * \return A formatted string representing the time.
 */
std::string time_string(int64_t nanosec, const char* format = "%Y-%m-%d_%H:%M:%S");

}  // namespace scorpio_utils::time_utils
