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

#include <memory>
#include <stdexcept>
#include <utility>

#include "scorpio_utils/assert.hpp"
#include "scorpio_utils/decorators.hpp"

namespace scorpio_utils {
/**
 * A unique function wrapper that can hold any callable object.
 * It is designed to be used in a thread-safe manner.
 * It can be used to store and invoke functions, lambdas, or any callable objects.
 * It is similar to ``std::function`` but it does not require callable to be copy constructable.
 */
template<typename T>
class UniqueFunction;

/**
 * A unique function wrapper that can hold any callable object.
 * It is designed to be used in a thread-safe manner.
 * It can be used to store and invoke functions, lambdas, or any callable objects.
 * It is similar to ``std::function`` but it does not require callable to be copy constructable.
 */
template<typename Ret, typename ... Args>
class UniqueFunction<Ret(Args...)> {
  struct FunctionBase {
    SCU_ALWAYS_INLINE virtual ~FunctionBase() = default;
    SCU_ALWAYS_INLINE virtual Ret operator()(Args&& ... args) = 0;
  };

  template<typename Fn>
  struct Function : FunctionBase {
    static_assert(std::is_invocable_r_v<Ret, Fn, Args...>,
                  "Function must be invocable with the provided arguments");
    Fn fn;
    SCU_ALWAYS_INLINE explicit Function(Fn&& fn)
    : fn(std::move(fn)) { }
    Function(const Function&) = delete;
    Function& operator=(const Function&) = delete;
    SCU_ALWAYS_INLINE Function(Function&&) = default;
    SCU_ALWAYS_INLINE Function& operator=(Function&&) = default;
    SCU_ALWAYS_INLINE ~Function() = default;
    SCU_ALWAYS_INLINE Ret operator()(Args&& ... args) override {
      return fn(std::forward<Args>(args)...);
    }
  };

  std::unique_ptr<FunctionBase> _function;

public:
  SCU_ALWAYS_INLINE UniqueFunction() = default;

  template<typename Fn>
  SCU_ALWAYS_INLINE UniqueFunction(Fn&& fn)
  : _function{std::make_unique<Function<Fn>>(std::move(fn))} { }

  UniqueFunction(const UniqueFunction&) = delete;
  UniqueFunction& operator=(const UniqueFunction&) = delete;

  SCU_ALWAYS_INLINE UniqueFunction(UniqueFunction&&) = default;
  SCU_ALWAYS_INLINE UniqueFunction& operator=(UniqueFunction&&) = default;

  /**
   * Invokes the stored function with the provided arguments.
   * \throws When function is not set.
   */
  SCU_ALWAYS_INLINE Ret operator()(Args&& ... args) {
    return call(std::forward<Args>(args)...);
  }

  /**
   * Invokes the stored function with the provided arguments.
   * \throws When function is not set.
   */
  SCU_ALWAYS_INLINE Ret call(Args&& ... args) {
    SCU_ASSERT(_function, "Unique function is not set");
    return (*_function)(std::forward<Args>(args)...);
  }

  /**
   * \returns Whether function is set
   */
  SCU_ALWAYS_INLINE bool has_value() const noexcept {
    return _function.get() != nullptr;
  }
  /**
   * \returns Whether function is set
   */
  SCU_ALWAYS_INLINE operator bool() const noexcept {
    return has_value();
  }

  /**
   * Resets the stored function.
   */
  SCU_ALWAYS_INLINE void reset() {
    _function.reset();
  }
};
}  // namespace scorpio_utils
