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

#include <ios>
#include <optional>
#include <type_traits>
#include <utility>
#include "scorpio_utils/decorators.hpp"
#include "scorpio_utils/types.hpp"

namespace scorpio_utils {
/**
 * Utility to check if a type has a flush method.
 * If the type has a flush method this will provide a callable operator() that
 * can be used to flush the stream.
 * If the type does not have a flush method, the operator() will do nothing.
 */
template<typename T, typename = std::void_t<>>
struct HasFlush : std::false_type {
  void operator()(T&) const { }
};

/**
 * Utility to check if a type has a flush method.
 * If the type has a flush method this will provide a callable operator() that
 * can be used to flush the stream.
 * If the type does not have a flush method, the operator() will do nothing.
 */
template<typename T>
struct HasFlush<T, std::void_t<decltype(std::declval<T>().flush())>>: std::true_type {
  void operator()(T& stream) const {
    stream.flush();
  }
};

/**
 * Utility to check if a type has seekp methods.
 */
template<typename T, typename = std::void_t<>>
struct HasSeekpPos : std::false_type { };

/**
 * Utility to check if a type has seekp methods.
 */
template<typename T>
struct HasSeekpPos<T,
  std::void_t<decltype(std::declval<T>().seekp(std::declval<std::streampos>()))>>: std::true_type { };

/**
 * Utility to check if a type has seekp methods with offset and direction.
 * This is used to check if the stream supports seeking with an offset and direction.
 */
template<typename T, typename = std::void_t<>>
struct HasSeekpOffset : std::false_type { };

/**
 * Utility to check if a type has seekp methods with offset and direction.
 * This is used to check if the stream supports seeking with an offset and direction.
 */
template<typename T>
struct HasSeekpOffset<T,
  std::void_t<decltype(std::declval<T>().seekp(std::declval<std::streamoff>(),
    std::declval<std::ios_base::seekdir>()))>>: std::true_type { };

/**
 * Utility to check if a type has bitwise left shift operator.
 * This is used to check if the stream supports writing data using the << operator.
 */
template<typename Stream, typename T, typename = std::void_t<>>
struct HasBitShiftLeft : std::false_type { };

template<typename Stream, typename T>
struct HasBitShiftLeft<Stream, T,
  std::void_t<decltype(std::declval<Stream>() << std::declval<T>())>>: std::true_type { };

/**
 * Utility to check if a type has arrow operator.
 * This is used to check if the stream supports accessing members using the -> operator.
 */
template<typename Stream, typename = std::void_t<>>
struct HasArrow : std::false_type { };

/**
 * Utility to check if a type has arrow operator.
 * This is used to check if the stream supports accessing members using the -> operator.
 */
template<typename Stream>
struct HasArrow<Stream, std::void_t<decltype(std::declval<Stream>().operator->())>>: std::true_type { };

/**
 * Utility to check if a type has bitwise left shift operator.
 * This is used to check if the stream supports writing data using the << operator.
 */
template<typename Stream, typename T, typename = std::void_t<>>
struct HasBitShiftRight : std::false_type { };

template<typename Stream, typename T>
struct HasBitShiftRight<Stream, T,
  std::void_t<decltype(std::declval<Stream>() >> std::declval<T>())>>: std::true_type { };

template<typename T>
struct FlattenedOptionalType {
  using type = T;
};

template<typename T>
struct FlattenedOptionalType<std::optional<T>> {
  using type = typename FlattenedOptionalType<T>::type;
};

template<typename T>
struct IsOptional : std::false_type { };

template<typename T>
struct IsOptional<std::optional<T>>: std::true_type { };

template<typename T>
struct RemoveVoid {
  using type = T;
  template<typename Fn>
  SCU_ALWAYS_INLINE decltype(auto) operator()(Fn && fn) const noexcept(noexcept(fn())) {
    return fn();
  }
};

template<>
struct RemoveVoid<void> {
  using type = Empty;
  template<typename Fn>
  SCU_ALWAYS_INLINE Empty operator()(Fn&& fn) const noexcept(noexcept(fn())) {
    fn();
    return Empty{ };
  }
};

template<typename T>
using RemoveVoidT = typename RemoveVoid<T>::type;

#define SCU_FORWARD_REMOVE_VOID(Fn) \
  RemoveVoid<decltype(Fn)>{ }([&]() SCU_ALWAYS_INLINE_RAW->decltype(auto) { return Fn; })

#define SCU_REQUIRE_CALLABLE(Type, Fn, To, ...) \
  struct SCU_UNIQUE_NAME (CallableChecker) { \
  template<typename T, typename = std::void_t<>> \
  struct Checker : std::false_type { }; \
 \
  template<typename T> \
  struct Checker<T, \
    std::enable_if_t<std::is_convertible_v<decltype(std::declval<Type>().Fn(__VA_ARGS__)), To>>>: std::true_type { }; \
 \
  static_assert(Checker<Type>::value, "Requirement failed"); \
};

template<typename>
struct IsPair : std::false_type { };

template<typename T, typename U>
struct IsPair<std::pair<T, U>>: std::true_type {
  using FirstType = T;
  using SecondType = U;
};

template<typename>
struct IsPairWithSameTypes : std::false_type { };

template<typename T>
struct IsPairWithSameTypes<std::pair<T, T>>: std::true_type {
  using Type = T;
};
}  // namespace scorpio_utils
