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

#include <atomic>
#include <exception>
#include <functional>
#include <memory>
#include <mutex>
#include <optional>
#include <tuple>
#include <type_traits>
#include <utility>
#include <vector>

#include "scorpio_utils/assert.hpp"
#include "thread_pool_declaration.hpp"

namespace scorpio_utils::threading {
template<typename T>
class Future;

class FutureAlreadyMovedException : public std::exception {
  SCU_COLD SCU_CONST_FUNC SCU_ALWAYS_INLINE const char * what() const noexcept override {
    return "Future has already been moved";
  }
};

/**
 * A class that extends the functionality of a class by allowing to call
 * functions with the results of previous calls.
 *
 * \tparam Self The type of the class that is extending the functionality.
 * \tparam Args The types of the arguments that will be passed to the functions.
 */
template<class Self, typename ... Args>
class AndThen {
  mutable std::vector<scorpio_utils::UniqueFunction<void(std::tuple<Args...>)>> _functions;
  std::optional<scorpio_utils::UniqueFunction<void(std::tuple<Args...>)>> _done_fn;
  std::atomic<bool> _done;
  std::atomic<bool> _moved;

  SCU_ALWAYS_INLINE std::tuple<std::reference_wrapper<const Args>...> get_value() const {
    return static_cast<const Self*>(this)->get_value();
  }

  SCU_ALWAYS_INLINE std::tuple<Args...> take_value() {
    return static_cast<Self*>(this)->take_value();
  }

  SCU_ALWAYS_INLINE std::lock_guard<std::mutex> lock_mutex() const {
    return static_cast<const Self*>(this)->lock_mutex();
  }

protected:
  constexpr inline void call() {
    SCU_ASSERT(!_done, "Cannot call call on a done AndThen object");
    for (auto&& fn : std::move(_functions)) {
      fn(get_value());
    }
    if (_done_fn.has_value()) {
      _moved = true;
      (*std::move(_done_fn))(take_value());
    }
    _done = true;
  }

  SCU_ALWAYS_INLINE constexpr void done() {
    SCU_ASSERT(!_done, "Cannot call done on a done AndThen object");
    _done = true;
  }

public:
  enum class State {
    NOTHING,
    THENS,
    MOVE,
    DONE,
    MOVED,
  };

  State get_state() const {
    if (_moved) {
      return State::MOVED;
    }
    if (_done) {
      return State::DONE;
    }
    if (_done_fn.has_value()) {
      return State::MOVE;
    }
    if (!_functions.empty()) {
      return State::THENS;
    }
    return State::NOTHING;
  }

  constexpr AndThen()
  : _done_fn(std::nullopt), _done(false), _moved(false) { }

  template<typename Fn>
  std::shared_ptr<Future<std::invoke_result_t<Fn, Args...>>> and_map(Fn fn, std::shared_ptr<ThreadPool> tp) const&;

  template<typename Fn>
  std::shared_ptr<Future<std::invoke_result_t<Fn, Args...>>> and_map(Fn fn, std::shared_ptr<ThreadPool> tp) &&;

  SCU_ALWAYS_INLINE constexpr bool is_done() const {
    return _done;
  }

  SCU_ALWAYS_INLINE bool has_done_fn() const {
    const auto lock = lock_mutex();
    return _done_fn.has_value();
  }

  SCU_ALWAYS_INLINE bool has_functions() const {
    const auto lock = lock_mutex();
    return !_functions.empty();
  }
};
}  // namespace scorpio_utils::threading
