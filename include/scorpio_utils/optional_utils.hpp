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

#include <optional>
#include <type_traits>
#include <utility>

#include "scorpio_utils/type_traits.hpp"

namespace scorpio_utils {

/**
 *  Invokes the function `fn` with values from the provided optional arguments.
 *  If all optional arguments have values, it returns a new `std::optional` containing
 *  the result of the function call. If any of the optional arguments is empty,
 *  it returns `std::nullopt`.
 *
 *  \param fn The function to be invoked with the values of the optional arguments.
 *  \param args The optional arguments to be passed to the function.
 *  \return A `std::optional` containing the result of the function call if all
 *          optional arguments have values, otherwise `std::nullopt`.
 */
template<typename Fn, typename ... Args>
constexpr inline auto optional_map(
  Fn&& fn,
  const std::optional<Args>& ... args) -> std::optional<std::invoke_result_t<Fn, Args...>> {
  static_assert(
    std::is_invocable_v<Fn, Args...>,
    "Function must be invocable with the provided optional arguments in the order they are given"
  );
  using Y = std::invoke_result_t<Fn, Args...>;
  static_assert(!std::is_same_v<void, Y>, "Function must not return void");
  return (args.has_value() && ...) ? std::optional<Y>(fn(args.value()...)) : std::nullopt;
}
/**
 *  Invokes the function `fn` with values from the provided optional arguments.
 *  If all optional arguments have values, it returns a new `std::optional` containing
 *  the result of the function call. If any of the optional arguments is empty,
 *  it returns `std::nullopt`.
 *
 *  \param fn The function to be invoked with the values of the optional arguments.
 *  \param args The optional arguments to be passed to the function.
 *  \return A `std::optional` containing the result of the function call if all
 *          optional arguments have values, otherwise `std::nullopt`.
 */
template<typename Fn, typename ... Args>
constexpr inline auto optional_map(
  Fn&& fn,
  std::optional<Args>&& ... args) -> std::optional<std::invoke_result_t<Fn, Args...>> {
  static_assert(
    std::is_invocable_v<Fn, Args...>,
    "Function must be invocable with the provided optional arguments in the order they are given"
  );
  using Y = std::invoke_result_t<Fn, Args...>;
  static_assert(!std::is_same_v<void, Y>, "Function must not return void");
  return (args.has_value() && ...) ? std::optional<Y>(fn(*std::forward<std::optional<Args>>(args)...)) : std::nullopt;
}

/**
 *  Invokes the function `fn` with values from the provided optional arguments.
 *  If all optional arguments have values, it returns a new `std::optional` containing
 *  the result of the function call. If any of the optional arguments is empty,
 *  it returns `std::nullopt`.
 *
 *  \param fn The function to be invoked with the values of the optional arguments.
 *  \param args The optional arguments to be passed to the function.
 *  \return A `std::optional` containing the result of the function call if all
 *          optional arguments have values, otherwise `std::nullopt`.
 */
template<typename Fn, typename ... Args>
constexpr inline auto optional_map(
  const Fn& fn,
  const std::optional<Args>& ... args) -> std::optional<std::invoke_result_t<Fn, Args...>> {
  static_assert(
    std::is_invocable_v<Fn, Args...>,
    "Function must be invocable with the provided optional arguments in the order they are given"
  );
  using Y = std::invoke_result_t<Fn, Args...>;
  static_assert(!std::is_same_v<void, Y>, "Function must not return void");
  return (args.has_value() && ...) ? std::optional<Y>(fn(args.value()...)) : std::nullopt;
}

/**
 *  Invokes the function `fn` with values from the provided optional arguments.
 *  If all optional arguments have values, it returns a new `std::optional` containing
 *  the result of the function call. If any of the optional arguments is empty,
 *  it returns `std::nullopt`.
 *
 *  \param fn The function to be invoked with the values of the optional arguments.
 *  \param args The optional arguments to be passed to the function.
 *  \return A `std::optional` containing the result of the function call if all
 *          optional arguments have values, otherwise `std::nullopt`.
 */
template<typename Fn, typename ... Args>
constexpr inline auto optional_map(
  const Fn& fn,
  std::optional<Args>&& ... args) -> std::optional<std::invoke_result_t<Fn, Args...>> {
  static_assert(
    std::is_invocable_v<Fn, Args...>,
    "Function must be invocable with the provided optional arguments in the order they are given"
  );
  using Y = std::invoke_result_t<Fn, Args...>;
  static_assert(!std::is_same_v<void, Y>, "Function must not return void");
  return (args.has_value() && ...) ? std::optional<Y>(fn(*std::forward<std::optional<Args>>(args)...)) : std::nullopt;
}

/**
 * Flattens a nested optional type into a single optional type.
 * If the input is a nested optional, it recursively flattens it until it reaches
 * a non-optional type.
 *
 * \param opt The optional value to flatten.
 * \return A new optional containing the flattened type, or std::nullopt if the input
 *         is empty.
 */
template<typename T>
constexpr inline auto optional_flatten(
  const std::optional<T>& opt
) -> std::optional<typename FlattenedOptionalType<T>::type> {
  using Y = typename FlattenedOptionalType<T>::type;
  if constexpr (std::is_same_v<T, Y>) {
    return opt;
  } else {
    return opt.has_value() ? optional_flatten(*opt) : std::nullopt;
  }
}

/**
 * Flattens a nested optional type into a single optional type.
 * If the input is a nested optional, it recursively flattens it until it reaches
 * a non-optional type.
 *
 * \param opt The optional value to flatten.
 * \return Forwarded optional containing the flattened type, or std::nullopt if the input
 *         is empty.
 */
template<typename T>
constexpr inline auto optional_flatten(std::optional<T>&& opt) -> std::optional<typename FlattenedOptionalType<T>::type>
{
  using Y = typename FlattenedOptionalType<T>::type;
  if constexpr (std::is_same_v<T, Y>) {
    return std::forward<std::optional<T>>(opt);
  } else {
    return opt.has_value() ? optional_flatten(*std::forward<std::optional<T>>(opt)) : std::nullopt;
  }
}

/**
 * Invokes the function `fn` with values from the provided optional arguments.
 * If all optional arguments have values, it returns a new `std::optional` containing
 * the result of the function call. If any of the optional arguments is empty,
 * it returns `std::nullopt`.
 *
 * \param fn The function to be invoked with the values of the optional arguments.
 * \param args The optional arguments to be passed to the function.
 * \return A `std::optional` which is a result of a function call, if all
 *          optional arguments have values, otherwise `std::nullopt`.
 */
template<typename Fn, typename ... Args>
constexpr inline auto optional_and_then(Fn&& fn, const std::optional<Args>& ... args) {
  static_assert(
    std::is_invocable_v<Fn, Args...>,
    "Function must be invocable with the provided optional arguments in the order they are given"
  );
  static_assert(
    IsOptional<std::invoke_result_t<Fn, Args...>>::value,
    "Function must return an optional type"
  );
  return (args.has_value() && ...) ? fn(args.value()...) : std::nullopt;
}

/**
 * Invokes the function `fn` with values from the provided optional arguments.
 * If all optional arguments have values, it returns a new `std::optional` containing
 * the result of the function call. If any of the optional arguments is empty,
 * it returns `std::nullopt`.
 *
 * \param fn The function to be invoked with the values of the optional arguments.
 * \param args The optional arguments to be passed to the function.
 * \return A `std::optional` which is a result of a function call, if all
 *          optional arguments have values, otherwise `std::nullopt`.
 */
template<typename Fn, typename ... Args>
constexpr inline auto optional_and_then(Fn&& fn, std::optional<Args>&& ... args) {
  static_assert(
    std::is_invocable_v<Fn, Args...>,
    "Function must be invocable with the provided optional arguments in the order they are given"
  );
  static_assert(
    IsOptional<std::invoke_result_t<Fn, Args...>>::value,
    "Function must return an optional type"
  );
  return (args.has_value() && ...) ? fn(*std::forward<std::optional<Args>>(args)...) : std::nullopt;
}

/**
 * Invokes the function `fn` with values from the provided optional arguments.
 * If all optional arguments have values, it returns a new `std::optional` containing
 * the result of the function call. If any of the optional arguments is empty,
 * it returns `std::nullopt`.
 *
 * \param fn The function to be invoked with the values of the optional arguments.
 * \param args The optional arguments to be passed to the function.
 * \return A `std::optional` which is a result of a function call, if all
 *          optional arguments have values, otherwise `std::nullopt`.
 */
template<typename Fn, typename ... Args>
constexpr inline auto optional_and_then(const Fn& fn, const std::optional<Args>& ... args) {
  static_assert(
    std::is_invocable_v<Fn, Args...>,
    "Function must be invocable with the provided optional arguments in the order they are given"
  );
  static_assert(
    IsOptional<std::invoke_result_t<Fn, Args...>>::value,
    "Function must return an optional type"
  );
  return (args.has_value() && ...) ? fn(args.value()...) : std::nullopt;
}

/**
 * Invokes the function `fn` with values from the provided optional arguments.
 * If all optional arguments have values, it returns a new `std::optional` containing
 * the result of the function call. If any of the optional arguments is empty,
 * it returns `std::nullopt`.
 *
 * \param fn The function to be invoked with the values of the optional arguments.
 * \param args The optional arguments to be passed to the function.
 * \return A `std::optional` which is a result of a function call, if all
 *          optional arguments have values, otherwise `std::nullopt`.
 */
template<typename Fn, typename ... Args>
constexpr inline auto optional_and_then(const Fn& fn, std::optional<Args>&& ... args) {
  static_assert(
    std::is_invocable_v<Fn, Args...>,
    "Function must be invocable with the provided optional arguments in the order they are given"
  );
  static_assert(
    IsOptional<std::invoke_result_t<Fn, Args...>>::value,
    "Function must return an optional type"
  );
  return (args.has_value() && ...) ? fn(*std::forward<std::optional<Args>>(args)...) : std::nullopt;
}
}  // namespace scorpio_utils
