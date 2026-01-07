#pragma once

#include <functional>
#include <optional>
#include <utility>
#include <variant>
#include "scorpio_utils/assert.hpp"
#include "scorpio_utils/decorators.hpp"

namespace scorpio_utils {
using ExpectedPutError_t = std::in_place_index_t<1ul>;
constexpr static ExpectedPutError_t ExpectedPutError{ };

template<typename E>
class Unexpected final {
  template<typename T, typename Y>
  friend class Expected;

  E&& error;

  SCU_UNCOPYBLE(Unexpected);
  SCU_UNMOVABLE(Unexpected);

public:
  using error_type = E;
  SCU_ALWAYS_INLINE explicit Unexpected(E&& err)
  : error(std::forward<E>(err)) { }
};

template<typename T, typename E>
class Expected : public std::variant<T, E> {
  using ExpectedPutValue_t = std::in_place_index_t<0ul>;
  constexpr static ExpectedPutValue_t ExpectedPutValue{ };

public:
  using value_type = T;
  using error_type = E;
  using std::variant<T, E>::variant;

  constexpr Expected(T&& value)  // NOLINT
  : std::variant<T, E>(ExpectedPutValue, std::move(value)) { }
  constexpr Expected(const T& value)  // NOLINT
  : std::variant<T, E>(ExpectedPutValue, value) { }
  constexpr Expected(Unexpected<E>&& unexpected)  // NOLINT
  : std::variant<T, E>(ExpectedPutError, std::move(unexpected.error)) { }
  constexpr Expected(ExpectedPutError_t, error_type&& error)
  : std::variant<T, E>(ExpectedPutError, std::move(error)) { }
  constexpr Expected(ExpectedPutError_t, const error_type& error)
  : std::variant<T, E>(ExpectedPutError, error) { }

  SCU_ALWAYS_INLINE constexpr bool is_ok() const noexcept {
    return this->index() == 0;
  }

  SCU_ALWAYS_INLINE constexpr bool is_err() const noexcept {
    return this->index() == 1;
  }

  SCU_ALWAYS_INLINE constexpr operator bool() const noexcept {
    return is_ok();
  }

  constexpr std::optional<T> ok() && {
    if (SCU_UNLIKELY(is_err())) {
      return std::nullopt;
    }
    return std::get<T>(std::move(*this));
  }

  constexpr std::optional<std::reference_wrapper<const T>> ok() const& {
    if (SCU_UNLIKELY(is_err())) {
      return std::nullopt;
    }
    return std::cref(std::get<T>(*this));
  }

  constexpr std::optional<E> err() && {
    if (SCU_UNLIKELY(is_ok())) {
      return std::nullopt;
    }
    return std::get<error_type>(std::move(*this));
  }

  constexpr std::optional<std::reference_wrapper<const E>> err() const& {
    if (SCU_UNLIKELY(is_ok())) {
      return std::nullopt;
    }
    return std::cref(std::get<error_type>(*this));
  }

  constexpr const T& ok_value() const& {
    SCU_ASSERT(is_ok(), "Attempted to get expected value when error was stored");
    return std::get<0>(*this);
  }

  constexpr T ok_value() && {
    SCU_ASSERT(is_ok(), "Attempted to get expected value when error was stored");
    return std::get<0>(std::move(*this));
  }

  constexpr const E& err_value() const& {
    SCU_ASSERT(is_err(), "Attempted to get error value when expected was stored");
    return std::get<1>(*this);
  }

  constexpr E err_value() && {
    SCU_ASSERT(is_err(), "Attempted to get error value when expected was stored");
    return std::get<1>(std::move(*this));
  }
};
}  // namespace scorpio_utils
