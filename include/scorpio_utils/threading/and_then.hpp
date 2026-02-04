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
#include <tuple>
#include <utility>
#include "declaration/and_then_declaration.hpp"
#include "future.hpp"
#include "thread_pool.hpp"

template<class Self, typename ... Args>
template<typename Fn>
std::shared_ptr<scorpio_utils::threading::Future<std::invoke_result_t<Fn,
  Args...>>> scorpio_utils::threading::AndThen<Self, Args...>::and_map(
  Fn fn, std::shared_ptr<ThreadPool> tp) const& {
  static_assert(std::is_invocable_v<Fn, Args...>,
    "Function must be invocable with the provided arguments in the order they are given");
  using ReturnType = std::invoke_result_t<Fn, Args...>;
  const auto lock = lock_mutex();
  SCU_UNLIKELY_THROW_IF(_moved, FutureAlreadyMovedException, );
  if (_done) {
    auto helper = [fn = std::move(fn), args = get_value()]() mutable -> ReturnType { return std::apply(fn, args); };
    return tp->async(helper);
  }

  typename Future<ReturnType>::shared_ptr result = typename Future<ReturnType>::shared_ptr(new Future<ReturnType>());
  _functions.emplace_back([result, fn = std::move(fn), tp](auto args) mutable {
      auto helper = [fn = std::move(fn), args = std::move(args)]() mutable -> ReturnType {
        return std::apply(fn, args);
      };
      tp->async_call(helper, result);
    });
  return result;
}

template<class Self, typename ... Args>
template<typename Fn>
std::shared_ptr<scorpio_utils::threading::Future<std::invoke_result_t<Fn,
  Args...>>> scorpio_utils::threading::AndThen<Self, Args...>::and_map(
  Fn fn, std::shared_ptr<ThreadPool> tp) && {
  using ReturnType = std::invoke_result_t<Fn, Args...>;
  const auto lock = lock_mutex();
  SCU_UNLIKELY_THROW_IF(_moved, FutureAlreadyMovedException, );
  if (_done) {
    _moved = true;
    return tp->async([f = std::move(fn), args = take_value()]() mutable {
               return std::apply(f, std::move(args));
      });
  }

  typename Future<ReturnType>::shared_ptr result = typename Future<ReturnType>::shared_ptr(new Future<ReturnType>());
  _done_fn = [result, f = std::move(fn), tp](auto&& args) mutable {
      return tp->async_call([h = std::move(f), a = std::move(args)]() mutable {
                 return std::apply(h, std::move(a));
        }, result);
    };
  return result;
}
