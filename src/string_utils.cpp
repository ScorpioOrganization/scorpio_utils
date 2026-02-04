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

#include "scorpio_utils/string_utils.hpp"

SCU_CONST_FUNC std::vector<std::string_view> scorpio_utils::split(std::string_view str) {
  std::vector<std::string_view> result;
  size_t p = str.find_first_not_of(SCU_STRING_WHITESPACES);
  if (p == std::string_view::npos) {
    return result;
  }
  size_t q;
  while ((q = str.find_first_of(SCU_STRING_WHITESPACES, p)) != std::string_view::npos) {
    result.emplace_back(str.data() + p, q - p);
    p = str.find_first_not_of(SCU_STRING_WHITESPACES, q);
  }
  if (p != std::string_view::npos) {
    result.emplace_back(str.data() + p, str.size() - p);
  }
  return result;
}

SCU_CONST_FUNC std::vector<std::string_view> scorpio_utils::split(std::string_view str, size_t max_count) {
  std::vector<std::string_view> result;
  result.reserve(max_count + 1ul);
  size_t p = str.find_first_not_of(SCU_STRING_WHITESPACES);
  if (p == std::string_view::npos) {
    return result;
  }
  size_t q;
  while (max_count != 0 && (q = str.find_first_of(SCU_STRING_WHITESPACES, p)) != std::string_view::npos) {
    result.emplace_back(str.data() + p, q - p);
    p = str.find_first_not_of(SCU_STRING_WHITESPACES, q);
    --max_count;
  }
  if (p != std::string_view::npos) {
    result.emplace_back(str.data() + p, str.size() - p);
  }
  return result;
}

SCU_CONST_FUNC std::vector<std::string_view> scorpio_utils::split(std::string_view str, std::string_view separator) {
  std::vector<std::string_view> result;
  size_t p = 0;
  size_t q;
  while ((q = str.find(separator, p)) != std::string_view::npos) {
    result.emplace_back(str.data() + p, q - p);
    p = q + separator.size();
  }
  result.emplace_back(str.data() + p, str.size() - p);
  return result;
}

SCU_CONST_FUNC std::vector<std::string_view> scorpio_utils::split(
  std::string_view str, std::string_view separator,
  size_t max_count) {
  std::vector<std::string_view> result;
  result.reserve(max_count + 1ul);
  size_t p = 0;
  size_t q;
  while (max_count != 0 && (q = str.find(separator, p)) != std::string_view::npos) {
    result.emplace_back(str.data() + p, q - p);
    p = q + separator.size();
    --max_count;
  }
  result.emplace_back(str.data() + p, str.size() - p);
  return result;
}
