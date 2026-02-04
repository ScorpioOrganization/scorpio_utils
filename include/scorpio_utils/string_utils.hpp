/*
 * scorpio_utils - Scorpio Utility Library for C++
 * Copyright (C) 2026 Igor Zaworski
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program. If not, see <https://www.gnu.org/licenses/>.
 */

#pragma once

#include <string>
#include <string_view>
#include <vector>
#include "scorpio_utils/decorators.hpp"

// Default characters to be stripped (whitespaces)
#define SCU_STRING_WHITESPACES " \t\n\r\f\v"

namespace scorpio_utils {
/**
 * Returns a new string with leading and trailing characters specified in `stripped_chars` removed from the input string `str`.
 * By default, it removes whitespace characters (space, tab, newline, carriage return, form feed, vertical tab).
 * Character removed by default are defined by `SCU_STRING_WHITESPACES` macro.
 * \param str The string to be stripped.
 * \param stripped_chars A C-string containing characters to be stripped from both ends of `str`.
 * \return A new string with specified characters removed from both ends.
 */
SCU_ALWAYS_INLINE SCU_CONST_FUNC std::string strip(
  std::string str,
  std::string_view stripped_chars = SCU_STRING_WHITESPACES) {
  str.erase(str.find_last_not_of(stripped_chars) + 1ul);
  str.erase(0, str.find_first_not_of(stripped_chars));
  return str;
}

/**
 * Returns a new string view with leading and trailing characters specified in `stripped_chars` removed from the input string view `str`.
 * By default, it removes whitespace characters (space, tab, newline, carriage return, form feed, vertical tab).
 * Character removed by default are defined by `SCU_STRING_WHITESPACES` macro.
 * \param str The string view to be stripped.
 * \param stripped_chars A C-string containing characters to be stripped from both ends of `str`.
 * \return A new string view with specified characters removed from both ends.
 */
SCU_ALWAYS_INLINE SCU_CONST_FUNC constexpr std::string_view strip(
  std::string_view str,
  std::string_view stripped_chars = " ") {
  auto last_not_of = str.find_last_not_of(stripped_chars);
  if (last_not_of == std::string_view::npos) {
    return std::string_view();
  }
  str.remove_suffix(str.size() - str.find_last_not_of(stripped_chars) - 1ul);
  str.remove_prefix(str.find_first_not_of(stripped_chars));
  return str;
}

/**
 * Checks if the given string view `str` starts with the specified string `prefix`.
 * \param str The string view to check.
 * \param prefix The string prefix to look for at the start of `str`.
 * \return `true` if `str` starts with `prefix`, otherwise `false`.
 */
SCU_CONST_FUNC SCU_ALWAYS_INLINE constexpr bool starts_with(const std::string_view str, std::string_view prefix) {
  return str.rfind(prefix, prefix.size() - 1ul) != std::string_view::npos;
}

/**
 * Checks if the given string view `str` ends with the specified string `prefix`.
 * \param str The string view to check.
 * \param prefix The string prefix to look for at the end of `str`.
 * \return `true` if `str` ends with `prefix`, otherwise `false`.
 */
SCU_CONST_FUNC SCU_ALWAYS_INLINE constexpr bool ends_with(const std::string_view str, std::string_view prefix) {
  return str.find(prefix, str.size() - prefix.size()) != std::string_view::npos;
}

/**
 * Splits the input string view `str` into a vector of string views using whitespace as the delimiter.
 * Consecutive whitespace characters are treated as a single delimiter.
 * Leading and trailing whitespace is ignored.
 * \param str The string view to split.
 * \return A vector of string views resulting from the split operation.
 */
SCU_CONST_FUNC std::vector<std::string_view> split(std::string_view str);

/**
 * Splits the input string view `str` into a vector of string views using whitespace as the delimiter, up to a maximum of `max_count` splits.
 * Consecutive whitespace characters are treated as a single delimiter.
 * Leading and trailing whitespace is ignored.
 * \param str The string view to split.
 * \param max_count The maximum number of splits to perform. The resulting vector will contain at most `max_count + 1` elements.
 * \return A vector of string views resulting from the split operation - at most first `max_count` is result of a split and last element is a rest of a string.
 */
SCU_CONST_FUNC std::vector<std::string_view> split(std::string_view str, size_t max_count);

/**
 * Splits the input string view `str` into a vector of string views using the specified `separator`.
 * \param str The string view to split.
 * \param separator The string view delimiter to use for splitting.
 * \return A vector of string views resulting from the split operation.
 */
SCU_CONST_FUNC std::vector<std::string_view> split(std::string_view str, std::string_view separator);

/**
 * Splits the input string view `str` into a vector of string views using the specified `separator`, up to a maximum of `max_count` splits.
 * \param str The string view to split.
 * \param separator The string view delimiter to use for splitting.
 * \param max_count The maximum number of splits to perform. The resulting vector will contain at most `max_count + 1` elements.
 * \return A vector of string views resulting from the split operation - at most first `max_count` is result of a split and last element is a rest of a string.
 */
SCU_CONST_FUNC std::vector<std::string_view> split(std::string_view str, std::string_view separator, size_t max_count);
}  // namespace scorpio_utils
