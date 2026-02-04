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

#include <type_traits>
#include <utility>
#include "scorpio_utils/decorators.hpp"

namespace scorpio_utils {
template<typename Fn>
class Defer {
  static_assert(std::is_invocable_v<Fn>, "Defer requires an invocable type");
  static_assert(std::is_same_v<std::invoke_result_t<Fn>, void>,
                "Defer function is required to return a void type");
  Fn _fn;

  // Disable copy and move semantics
  SCU_UNCOPYBLE(Defer);
  SCU_UNMOVABLE(Defer);

public:
  [[nodiscard]] SCU_ALWAYS_INLINE explicit Defer(Fn&& fn) noexcept(std::is_nothrow_move_constructible_v<Fn>)
  : _fn(std::move(fn)) { }

  SCU_ALWAYS_INLINE ~Defer() noexcept(std::is_nothrow_invocable_v<Fn>) {
    _fn();
  }
};

template<typename T>
class Restorer {
  static_assert(std::is_copy_assignable_v<T>|| std::is_move_assignable_v<T>,
                "Restorer requires T to be copy or move assignable");
  static_assert(std::is_copy_constructible_v<T>, "Restorer requires T to be copy constructible");

  Restorer(const Restorer&) = delete;
  Restorer& operator=(const Restorer&) = delete;
  Restorer(Restorer&&) = delete;
  Restorer& operator=(Restorer&&) = delete;
  T& _value;
  T _old_value;

public:
  [[nodiscard]] SCU_ALWAYS_INLINE explicit Restorer(T& value) noexcept(std::is_nothrow_copy_constructible_v<T>)
  : _value(value), _old_value(value) { }

  SCU_ALWAYS_INLINE ~Restorer() noexcept(
    (std::is_move_assignable_v<T>&& std::is_nothrow_move_assignable_v<T>) ||
    (!std::is_move_assignable_v<T>&& std::is_nothrow_copy_assignable_v<T>)
  ) {
    if constexpr (std::is_move_assignable_v<T>) {
      _value = std::move(_old_value);
    } else {
      _value = _old_value;
    }
  }
};

template<typename T>
class MoveRestorer {
  static_assert(std::is_copy_assignable_v<T>|| std::is_move_assignable_v<T>,
                "MoveRestorer requires T to be copy or move assignable");
  static_assert(std::is_move_constructible_v<T>, "MoveRestorer requires T to be move constructible");

  MoveRestorer(const MoveRestorer&) = delete;
  MoveRestorer& operator=(const MoveRestorer&) = delete;
  MoveRestorer(MoveRestorer&&) = delete;
  MoveRestorer& operator=(MoveRestorer&&) = delete;
  T& _value;
  T _old_value;

public:
  [[nodiscard]] SCU_ALWAYS_INLINE explicit MoveRestorer(T& value) noexcept(std::is_nothrow_move_constructible_v<T>)
  : _value(value), _old_value(std::move(value)) { }

  SCU_ALWAYS_INLINE ~MoveRestorer() noexcept(
    (std::is_move_assignable_v<T>&& std::is_nothrow_move_assignable_v<T>) ||
    (!std::is_move_assignable_v<T>&& std::is_nothrow_copy_assignable_v<T>)
  ) {
    if constexpr (std::is_move_assignable_v<T>) {
      _value = std::move(_old_value);
    } else {
      _value = _old_value;
    }
  }
};
}  // namespace scorpio_utils

#define SCU_RESTORER(value) \
  scorpio_utils::Restorer SCU_UNIQUE_NAME(scu_restorer)(value);

#define SCU_MOVE_RESTORER(value) \
  scorpio_utils::MoveRestorer SCU_UNIQUE_NAME(scu_move_restorer)(value);

#define SCU_DEFER(fn) \
  scorpio_utils::Defer SCU_UNIQUE_NAME(scu_defer)(fn);
