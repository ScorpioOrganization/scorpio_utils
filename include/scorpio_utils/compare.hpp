#pragma once

#include <type_traits>
#include "scorpio_utils/decorators.hpp"

namespace scorpio_utils {

#define SCU_MAKE_UNSIGNED(v) SCU_AS(std::make_unsigned_t<std::remove_reference_t<decltype(v)>>, v)

template<typename T, typename U>
SCU_CONST_FUNC constexpr bool cmp_equal(const T& a, const U& b) noexcept {
  static_assert(std::is_integral_v<T>&& std::is_integral_v<U>,
                "Both types must be integral");
  constexpr bool is_T_signed = std::is_signed_v<T>;
  constexpr bool is_U_signed = std::is_signed_v<U>;
  if constexpr (is_T_signed && is_U_signed) {
    return a == b;
  } else if constexpr (is_T_signed && !is_U_signed) {
    return a >= 0 && SCU_MAKE_UNSIGNED(a) == b;
  } else if constexpr (!is_T_signed && is_U_signed) {
    return b >= 0 && a == SCU_MAKE_UNSIGNED(b);
  } else {
    return a == b;
  }
}

template<typename T, typename U>
SCU_CONST_FUNC constexpr bool cmp_less(const T& a, const U& b) noexcept {
  static_assert(std::is_integral_v<T>&& std::is_integral_v<U>,
                "Both types must be integral");
  constexpr bool is_T_signed = std::is_signed_v<T>;
  constexpr bool is_U_signed = std::is_signed_v<U>;
  if constexpr (is_T_signed && is_U_signed) {
    return a < b;
  } else if constexpr (is_T_signed && !is_U_signed) {
    return a < 0 || SCU_MAKE_UNSIGNED(a) < b;
  } else if constexpr (!is_T_signed && is_U_signed) {
    return b > 0 && a < SCU_MAKE_UNSIGNED(b);
  } else {
    return a < b;
  }
}

template<typename T, typename U>
SCU_CONST_FUNC constexpr bool cmp_greater(const T& a, const U& b) noexcept {
  static_assert(std::is_integral_v<T>&& std::is_integral_v<U>,
                "Both types must be integral");
  constexpr bool is_T_signed = std::is_signed_v<T>;
  constexpr bool is_U_signed = std::is_signed_v<U>;
  if constexpr (is_T_signed && is_U_signed) {
    return a > b;
  } else if constexpr (is_T_signed && !is_U_signed) {
    return a >= 0 && SCU_MAKE_UNSIGNED(a) > b;
  } else if constexpr (!is_T_signed && is_U_signed) {
    return b < 0 || a > SCU_MAKE_UNSIGNED(b);
  } else {
    return a > b;
  }
}

template<typename T, typename U>
SCU_CONST_FUNC constexpr bool cmp_less_equal(const T& a, const U& b) noexcept {
  static_assert(std::is_integral_v<T>&& std::is_integral_v<U>,
                "Both types must be integral");
  constexpr bool is_T_signed = std::is_signed_v<T>;
  constexpr bool is_U_signed = std::is_signed_v<U>;
  if constexpr (is_T_signed && is_U_signed) {
    return a <= b;
  } else if constexpr (is_T_signed && !is_U_signed) {
    return a < 0 || SCU_MAKE_UNSIGNED(a) <= b;
  } else if constexpr (!is_T_signed && is_U_signed) {
    return b >= 0 && a <= SCU_MAKE_UNSIGNED(b);
  } else {
    return a <= b;
  }
}

template<typename T, typename U>
SCU_CONST_FUNC constexpr bool cmp_greater_equal(const T& a, const U& b) noexcept {
  static_assert(std::is_integral_v<T>&& std::is_integral_v<U>,
                "Both types must be integral");
  constexpr bool is_T_signed = std::is_signed_v<T>;
  constexpr bool is_U_signed = std::is_signed_v<U>;
  if constexpr (is_T_signed && is_U_signed) {
    return a >= b;
  } else if constexpr (is_T_signed && !is_U_signed) {
    return a >= 0 && SCU_MAKE_UNSIGNED(a) >= b;
  } else if constexpr (!is_T_signed && is_U_signed) {
    return b < 0 || a >= SCU_MAKE_UNSIGNED(b);
  } else {
    return a >= b;
  }
}

#define SCU_EQ_FROM_NEQ(Self) \
  bool operator==(const Self& other) const noexcept(noexcept(*this != other)) { \
    static_assert(std::is_invocable_r_v<bool, decltype(&Self::operator!=), const Self&, const Self&>, \
                  "const decltype(*this) != const Self& must be defined and return bool"); \
    return !(*this != other); \
  }

#define SCU_EQ_FROM_LT(Self) \
  bool operator==(const Self& other) const noexcept(noexcept(*this < other)) { \
    static_assert(std::is_invocable_r_v < bool, decltype(&Self::operator< ), const Self&, const Self& >, \
                  "const decltype(*this)& < const Self& must be defined and return bool"); \
    static_assert(std::is_same_v<decltype(*this), decltype(other)>, "Types must be the to use this generative macro"); \
    return !(*this < other) && !(other < *this); \
  }

#define SCU_EQ_FROM_GT(Self) \
  bool operator==(const Self& other) const noexcept(noexcept(*this > other)) { \
    static_assert(std::is_invocable_r_v<bool, decltype(&Self::operator> ), const Self&, const Self&>, \
                  "const decltype(*this)& > const Self& must be defined and return bool"); \
    static_assert(std::is_same_v<decltype(*this), decltype(other)>, "Types must be the to use this generative macro"); \
    return !(*this > other) && !(other > *this); \
  }

#define SCU_EQ_FROM_LE(Self) \
  bool operator==(const Self& other) const noexcept(noexcept(*this <= other)) { \
    static_assert(std::is_invocable_r_v<bool, decltype(&Self::operator<= ), const Self&, const Self&>, \
                  "const decltype(*this)& <= const Self& must be defined and return bool"); \
    static_assert(std::is_same_v<decltype(*this), decltype(other)>, "Types must be the to use this generative macro"); \
    return (*this <= other) && (other <= *this); \
  }

#define SCU_EQ_FROM_GE(Self) \
  bool operator==(const Self& other) const noexcept(noexcept(*this >= other)) { \
    static_assert(std::is_invocable_r_v<bool, decltype(&Self::operator>=), const Self&, const Self&>, \
                  "const decltype(*this)& >= const Self& must be defined and return bool"); \
    static_assert(std::is_same_v<decltype(*this), decltype(other)>, "Types must be the to use this generative macro"); \
    return (*this >= other) && (other >= *this); \
  }

#define SCU_NEQ_FROM_EQ(Self) \
  bool operator!=(const Self& other) const noexcept(noexcept(*this == other)) { \
    static_assert(std::is_invocable_r_v<bool, decltype(&Self::operator==), const Self&, const Self&>, \
                  "const decltype(*this)& == const Self& must be defined and return bool"); \
    return !(*this == other); \
  }

#define SCU_NEQ_FROM_LT(Self) \
  bool operator!=(const Self& other) const noexcept(noexcept(*this < other)) { \
    static_assert(std::is_invocable_r_v < bool, decltype(&Self::operator< ), const Self&, const Self& >, \
                  "const decltype(*this)& < const Self& must be defined and return bool"); \
    static_assert(std::is_same_v<decltype(*this), decltype(other)>, "Types must be the to use this generative macro"); \
    return (*this < other) || (other < *this); \
  }

#define SCU_NEQ_FROM_GT(Self) \
  bool operator!=(const Self& other) const noexcept(noexcept(*this > other)) { \
    static_assert(std::is_invocable_r_v<bool, decltype(&Self::operator> ), const Self&, const Self&>, \
                  "const decltype(*this)& > const Self& must be defined and return bool"); \
    static_assert(std::is_same_v<decltype(*this), decltype(other)>, "Types must be the to use this generative macro"); \
    return (*this > other) || (other > *this); \
  }

#define SCU_NEQ_FROM_LE(Self) \
  bool operator!=(const Self& other) const noexcept(noexcept(*this <= other)) { \
    static_assert(std::is_invocable_r_v<bool, decltype(&Self::operator<= ), const Self&, const Self&>, \
                  "const decltype(*this)& <= const Self& must be defined and return bool"); \
    static_assert(std::is_same_v<decltype(*this), decltype(other)>, "Types must be the to use this generative macro"); \
    return !(*this <= other) || !(other <= *this); \
  }

#define SCU_NEQ_FROM_GE(Self) \
  bool operator!=(const Self& other) const noexcept(noexcept(*this >= other)) { \
    static_assert(std::is_invocable_r_v<bool, decltype(&Self::operator>= ), const Self&, const Self&>, \
                  "const decltype(*this)& >= const Self& must be defined and return bool"); \
    static_assert(std::is_same_v<decltype(*this), decltype(other)>, "Types must be the to use this generative macro"); \
    return !(*this >= other) || !(other >= *this); \
  }

#define SCU_LT_FROM_GT(Self) \
  bool operator<(const Self& other) const noexcept(noexcept(*this > other)) { \
    static_assert(std::is_invocable_r_v<bool, decltype(&Self::operator> ), const Self&, const Self&>, \
                  "const decltype(*this)>& > const Self& must be defined and return bool"); \
    static_assert(std::is_same_v<decltype(*this), decltype(other)>, "Types must be the to use this generative macro"); \
    return other > *this; \
  }

#define SCU_LT_FROM_GE(Self) \
  bool operator<(const Self& other) const noexcept(noexcept(*this >= other)) { \
    static_assert(std::is_invocable_r_v<bool, decltype(&Self::operator>= ), const Self&, const Self&>, \
                  "const decltype(*this)& >= const Self& must be defined and return bool"); \
    static_assert(std::is_same_v<decltype(*this), decltype(other)>, "Types must be the to use this generative macro"); \
    return (other >= *this) && !(*this >= other); \
  }

#define SCU_LT_FROM_LE(Self) \
  bool operator<(const Self& other) const noexcept(noexcept(*this <= other)) { \
    static_assert(std::is_invocable_r_v<bool, decltype(&Self::operator<= ), const Self&, const Self&>, \
                  "const decltype(*this)& <= const Self& must be defined and return bool"); \
    static_assert(std::is_same_v<decltype(*this), decltype(other)>, "Types must be the to use this generative macro"); \
    return (*this <= other) && !(other <= *this); \
  }

#define SCU_GT_FROM_LT(Self) \
  bool operator>(const Self& other) const noexcept(noexcept(*this < other)) { \
    static_assert(std::is_invocable_r_v < bool, decltype(&Self::operator< ), const Self&, const Self& >, \
                  "const decltype(*this)& < const Self& must be defined and return bool"); \
    static_assert(std::is_same_v<decltype(*this), decltype(other)>, "Types must be the to use this generative macro"); \
    return other < *this; \
  }

#define SCU_GT_FROM_LE(Self) \
  bool operator>(const Self& other) const noexcept(noexcept(*this <= other)) { \
    static_assert(std::is_invocable_r_v<bool, decltype(&Self::operator<= ), const Self&, const Self&>, \
                  "const decltype(*this)& <= const Self& must be defined and return bool"); \
    static_assert(std::is_same_v<decltype(*this), decltype(other)>, "Types must be the to use this generative macro"); \
    return (other <= *this) && !(*this <= other); \
  }

#define SCU_GT_FROM_GE(Self) \
  bool operator>(const Self& other) const noexcept(noexcept(*this >= other)) { \
    static_assert(std::is_invocable_r_v<bool, decltype(&Self::operator>= ), const Self&, const Self&>, \
                  "const decltype(*this)& >= const Self& must be defined and return bool"); \
    static_assert(std::is_same_v<decltype(*this), decltype(other)>, "Types must be the to use this generative macro"); \
    return (*this >= other) && !(other >= *this); \
  }

#define SCU_LE_FROM_GT(Self) \
  bool operator<=(const Self& other) const noexcept(noexcept(*this > other)) { \
    static_assert(std::is_invocable_r_v<bool, decltype(&Self::operator> ), const Self&, const Self&>, \
                  "const decltype(*this)& > const Self& must be defined and return bool"); \
    return !(*this > other); \
  }

#define SCU_LE_FROM_GE(Self) \
  bool operator<=(const Self& other) const noexcept(noexcept(*this >= other)) { \
    static_assert(std::is_invocable_r_v<bool, decltype(&Self::operator>= ), const Self&, const Self&>, \
                  "const decltype(*this)& >= const Self& must be defined and return bool"); \
    static_assert(std::is_same_v<decltype(*this), decltype(other)>, "Types must be the to use this generative macro"); \
    return other >= *this; \
  }

#define SCU_LE_FROM_LT(Self) \
  bool operator<=(const Self& other) const noexcept(noexcept(*this < other)) { \
    static_assert(std::is_invocable_r_v < bool, decltype(&Self::operator< ), const Self&, const Self& >, \
                  "const decltype(*this)& < const Self& must be defined and return bool"); \
    static_assert(std::is_same_v<decltype(*this), decltype(other)>, "Types must be the to use this generative macro"); \
    return (*this < other) || !(other < *this); \
  }

#define SCU_GE_FROM_LT(Self) \
  bool operator>=(const Self& other) const noexcept(noexcept(*this < other)) { \
    static_assert(std::is_invocable_r_v < bool, decltype(&Self::operator< ), const Self&, const Self& >, \
                  "const decltype(*this)& < const Self& must be defined and return bool"); \
    return !(*this < other); \
  }

#define SCU_GE_FROM_LE(Self) \
  bool operator>=(const Self& other) const noexcept(noexcept(*this <= other)) { \
    static_assert(std::is_invocable_r_v<bool, decltype(&Self::operator<= ), const Self&, const Self&>, \
                  "const decltype(*this)& <= const Self& must be defined and return bool"); \
    static_assert(std::is_same_v<decltype(*this), decltype(other)>, "Types must be the to use this generative macro"); \
    return other <= *this; \
  }

#define SCU_GE_FROM_GT(Self) \
  bool operator>=(const Self& other) const noexcept(noexcept(*this > other)) { \
    static_assert(std::is_invocable_r_v<bool, decltype(&Self::operator> ), const Self&, const Self&>, \
                  "const decltype(*this)& > const Self& must be defined and return bool"); \
    static_assert(std::is_same_v<decltype(*this), decltype(other)>, "Types must be the to use this generative macro"); \
    return (*this > other) || !(other > *this); \
  }
}  // namespace scorpio_utils
