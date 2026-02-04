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

#include <chrono>
#include <memory>
#include <optional>
#include <thread>
#include <tuple>
#include <utility>
#include <variant>
#include "scorpio_utils/assert.hpp"
#include "scorpio_utils/decorators.hpp"
#include "scorpio_utils/optional_utils.hpp"
#include "scorpio_utils/time_provider/time_provider.hpp"

#define SCU_EAGER_SELECT_IS_READY __SCU_is_eager_select_ready
#define SCU_EAGER_SELECT_GET_VALUE __SCU_get_eager_select_value

namespace scorpio_utils::threading {
namespace {
template<typename Signal, typename = void>
struct EagerSelectTraitCheck {
  constexpr static bool value =
    std::is_same_v<bool, decltype(std::declval<Signal>().SCU_EAGER_SELECT_IS_READY())>&&
    std::is_invocable<decltype(&Signal::SCU_EAGER_SELECT_GET_VALUE), Signal&>::value;
  constexpr static bool is_ready_returns_bool =
    std::is_same_v<bool, decltype(std::declval<Signal>().SCU_EAGER_SELECT_IS_READY())>;
  constexpr static bool get_value_invocable =
    std::is_invocable<decltype(&Signal::SCU_EAGER_SELECT_GET_VALUE), Signal&>::value;
  constexpr static bool ptr = false;
};

template<typename Signal>
struct EagerSelectTraitCheck<Signal,
  std::void_t<decltype(std::declval<Signal>()->SCU_EAGER_SELECT_IS_READY()),
  decltype(std::declval<Signal>()->SCU_EAGER_SELECT_GET_VALUE())>>: std::true_type  {
  constexpr static bool is_ready_returns_bool =
    std::is_same_v<bool, decltype(std::declval<Signal>()->SCU_EAGER_SELECT_IS_READY())>;
  constexpr static bool get_value_invocable =
    std::is_invocable<decltype(&std::remove_pointer_t<Signal>::SCU_EAGER_SELECT_GET_VALUE), Signal&>::value;
  constexpr static bool ptr = true;
};

template<typename Signal, typename = void>
struct EagerSelectTraitCheckExtended : EagerSelectTraitCheck<Signal> {
  constexpr static bool iterable = false;
};

template<typename T>
struct EagerSelectTraitCheckExtended<std::pair<T, T>,
  std::void_t<std::enable_if_t<EagerSelectTraitCheck<typename std::iterator_traits<T>::value_type>::value>>>
  : EagerSelectTraitCheck<typename std::iterator_traits<T>::value_type> {
  constexpr static bool iterable = true;
};

template<typename Signal, typename = void>
struct EagerSelectTraitHelper {
  using return_type = Empty;
  constexpr static bool ptr = false;
  constexpr static bool iterable = false;
};

template<typename Signal>
struct EagerSelectTraitHelper<Signal,
  std::enable_if_t<EagerSelectTraitCheckExtended<Signal>::value && !EagerSelectTraitCheckExtended<Signal>::ptr>> {
  constexpr static bool ptr = false;
  constexpr static bool iterable = false;
  using return_type = RemoveVoidT<decltype(std::declval<Signal>().SCU_EAGER_SELECT_GET_VALUE())>;
  constexpr static bool isReady(Signal& signal) {
    return signal.SCU_EAGER_SELECT_IS_READY();
  }
  constexpr static auto getValue(Signal& signal) {
    return SCU_FORWARD_REMOVE_VOID(signal.SCU_EAGER_SELECT_GET_VALUE());
  }
};

template<typename Signal>
struct EagerSelectTraitHelper<Signal, std::void_t<decltype(std::declval<Signal>()->SCU_EAGER_SELECT_IS_READY()),
  decltype(std::declval<Signal>()->SCU_EAGER_SELECT_GET_VALUE())>> {
  constexpr static bool ptr = true;
  constexpr static bool iterable = false;
  using return_type = RemoveVoidT<decltype(std::declval<Signal>()->SCU_EAGER_SELECT_GET_VALUE())>;
  constexpr static bool isReady(Signal& signal) {
    return signal->SCU_EAGER_SELECT_IS_READY();
  }
  constexpr static auto getValue(Signal& signal) {
    return SCU_FORWARD_REMOVE_VOID(signal->SCU_EAGER_SELECT_GET_VALUE());
  }
};

template<typename Signal, typename = void>
struct EagerSelectTraitHelperExtended : EagerSelectTraitHelper<Signal> { };

template<typename T>
struct EagerSelectTraitHelperExtended<std::pair<T, T>,
  std::void_t<std::enable_if_t<EagerSelectTraitCheck<typename std::iterator_traits<T>::value_type>::value>>>
  : EagerSelectTraitHelper<typename std::iterator_traits<T>::value_type> {
  constexpr static bool iterable = true;
  using return_type = std::pair<
    typename EagerSelectTraitHelper<typename std::iterator_traits<T>::value_type>::return_type,
    size_t>;
};

template<size_t Index, typename Signal, typename ... T>
struct _EagerSelectGetVal {
  SCU_ALWAYS_INLINE static bool get(
    Signal& signal, std::optional<std::variant<T...>>& selected) {
    using type_helper = EagerSelectTraitHelperExtended<Signal>;
    if (type_helper::isReady(signal)) {
      selected.emplace(std::in_place_index_t<Index>{ }, type_helper::getValue(signal));
      return true;
    }
    return false;
  }
};

template<size_t Index, typename Iter, typename ... T>
struct _EagerSelectGetVal<Index, std::pair<Iter, Iter>, T...> {
  SCU_ALWAYS_INLINE static bool get(std::pair<Iter, Iter>& pair, std::optional<std::variant<T...>>& selected) {
    using type_helper = EagerSelectTraitHelperExtended<typename std::iterator_traits<Iter>::value_type>;
    size_t i = 0;
    for (auto it = pair.first; it != pair.second; ++it) {
      if (type_helper::isReady(*it)) {
        selected.emplace(std::in_place_index_t<Index>{ }, type_helper::getValue(*it), i);
        return true;
      }
      ++i;
    }
    return false;
  }
};

template<typename ... T, size_t Index, size_t ... Indexes, typename ... Signals>
bool _eager_select_predicate(
  std::optional<std::variant<T...>>& selected,
  std::integer_sequence<size_t, Index, Indexes...>,
  Signals&... signals) {
  auto signals_tuple = std::forward_as_tuple(signals ...);
  using Type = std::decay_t<decltype(std::get<Index>(signals_tuple))>;
  if (_EagerSelectGetVal<Index, Type, T...>::get(std::get<Index>(signals_tuple), selected)) {
    return true;
  }
  if constexpr (sizeof...(Indexes) == 0) {
    return false;
  } else {
    return _eager_select_predicate(selected, std::integer_sequence<size_t, Indexes...>{ }, signals ...);
  }
}
}  // namespace

/**
 * Wait until one of the signals is notified or closed.
 * This function will block the calling thread until one of the signals is notified or closed.
 * It will return the index of the signal that was notified or closed.
 *
 * \tparam Sleep The sleep duration in nanoseconds between checks. Default is 10000 (10 microseconds).
 * \tparam Signals The types of the signals to wait on. All types must be derived from `Signal`.
 * \param signals The signals to wait on.
 * \return The index of the signal that was notified or closed.
 * \warning This function uses a simple polling mechanism with a sleep duration.
 *          This may not be the most efficient way to wait for multiple signals.
 *          However, it is simple and works well for a small number of signals.
 *          If you need to wait for a large number of signals, consider using a different approach.
 * \note This function will be improved in the future to use a more efficient mechanism.
 */
template<size_t Sleep = 10000, typename ... Signals>
std::conditional_t<sizeof...(Signals) == 1,
  std::tuple_element_t<0, std::tuple<typename EagerSelectTraitHelperExtended<Signals>::return_type...>>,
  std::variant<typename EagerSelectTraitHelperExtended<Signals>::return_type...>>
eager_select(
  Signals&... signals) {
  static_assert(sizeof...(Signals) > 0, "Select requires at least one signal to wait on");
  static_assert(
    (EagerSelectTraitCheckExtended<Signals>::is_ready_returns_bool && ...),
    "All types must have SCU_EAGER_SELECT_IS_READY() method returning bool");
  static_assert(
    (EagerSelectTraitCheckExtended<Signals>::get_value_invocable && ...),
    "All types must have SCU_EAGER_SELECT_GET_VALUE() method invocable");
  using ReturnType = decltype(eager_select<Sleep, Signals...>(signals ...));
  if constexpr (sizeof...(Signals) == 1) {
    auto& signal = std::get<0>(std::tie(signals ...));
    using signal_type = std::decay_t<decltype(signal)>;
    using type_helper = EagerSelectTraitHelperExtended<signal_type>;
    if constexpr (type_helper::iterable) {
      while (true) {
        size_t i = 0;
        for (auto iter = signal.first; iter != signal.second; ++iter) {
          if (type_helper::isReady(*iter)) {
            return { type_helper::getValue(*iter), i };
          }
          ++i;
        }
      }
    } else {
      while (!type_helper::isReady(signal)) {
        std::this_thread::sleep_for(std::chrono::nanoseconds(Sleep));
      }
      return type_helper::getValue(signal);
    }
  } else {
    std::optional<ReturnType> selected;
    while (!_eager_select_predicate(selected, std::index_sequence_for<Signals...>{ }, signals ...)) {
      std::this_thread::sleep_for(std::chrono::nanoseconds(Sleep));
    }
    SCU_ASSUME(selected.has_value());
    return std::move(selected).value();
  }
}

template<typename TimeProvider>
class EagerSelectTimeout {
  static_assert(std::is_base_of<scorpio_utils::time_provider::TimeProvider, TimeProvider>::value,
    "TimeProvider must be derived from scorpio_utils::time_provider::TimeProvider");

  std::optional<int64_t> _start_time;
  int64_t _timeout;
  std::shared_ptr<TimeProvider> _time_provider;

public:
  SCU_ALWAYS_INLINE bool SCU_EAGER_SELECT_IS_READY() {
    return is_elapsed();
  }

  SCU_ALWAYS_INLINE void SCU_EAGER_SELECT_GET_VALUE() noexcept { }
  SCU_ALWAYS_INLINE EagerSelectTimeout(int64_t timeout, std::shared_ptr<TimeProvider> time_provider)
  : _start_time(std::nullopt),
    _timeout(timeout),
    _time_provider(std::move(time_provider)) {
    SCU_ASSERT(timeout > 0, "Timeout must be positive");
    SCU_ASSERT(_time_provider != nullptr, "TimeProvider must not be null");
  }

  SCU_ALWAYS_INLINE EagerSelectTimeout& start() noexcept {
    _start_time = _time_provider->get_time();
    return *this;
  }

  SCU_ALWAYS_INLINE void reset() noexcept {
    _start_time = std::nullopt;
  }

  SCU_ALWAYS_INLINE auto get_timeout() const noexcept {
    return _timeout;
  }

  SCU_ALWAYS_INLINE void set_timeout(int64_t timeout) noexcept {
    SCU_ASSERT(timeout > 0, "Timeout must be positive");
    _timeout = timeout;
  }

  SCU_ALWAYS_INLINE auto is_started() const noexcept {
    return _start_time.has_value();
  }

  SCU_ALWAYS_INLINE auto time_provider() const noexcept {
    return _time_provider;
  }

  SCU_ALWAYS_INLINE auto is_elapsed() const noexcept {
    return optional_map([this](auto start_time) {
               return _time_provider->get_time() - start_time >= _timeout;
    }, _start_time).value_or(false);
  }
};
}  // namespace scorpio_utils::threading
