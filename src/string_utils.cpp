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
