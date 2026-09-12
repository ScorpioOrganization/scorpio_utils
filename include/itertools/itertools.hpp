// Copyright 2025 Igor Zaworski

// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at

// http://www.apache.org/licenses/LICENSE-2.0

// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

#pragma once

#include <initializer_list>
#include <optional>
#include <type_traits>
#include <unordered_set>
#include <utility>

namespace itertools {

// Internal utility to implement defer semantics
namespace utility {
template<typename Fn>
class Defer {
  static_assert(std::is_invocable_v<Fn>, "Defer requires an invocable type");
  static_assert(std::is_same_v<std::invoke_result_t<Fn>, void>,
                "Defer function is required to return a void type");
  Fn _fn;

  // Disable copy and move semantics
  Defer(const Defer&) = delete;
  Defer& operator=(const Defer&) = delete;
  Defer(Defer&&) = delete;
  Defer& operator=(Defer&&) = delete;

public:
  [[nodiscard]] inline explicit Defer(Fn&& fn) noexcept(std::is_nothrow_move_constructible_v<Fn>)
  : _fn(std::move(fn)) { }

  inline ~Defer() noexcept(std::is_nothrow_invocable_v<Fn>) {
    _fn();
  }
};
}  // namespace utility

#ifdef __COUNTER__
#  define IT_COUNTER __COUNTER__
#else
#  define IT_COUNTER __LINE__
#endif

#define IT_CONCAT_HELPER(a, b) a ## b
#define IT_CONCAT(a, b) IT_CONCAT_HELPER(a, b)
#define IT_UNIQUE_NAME(prefix) IT_CONCAT(prefix, IT_COUNTER)
#define IT_DEFER(fn) \
  ::itertools::utility::Defer IT_UNIQUE_NAME(it_defer)(fn);


// Iterable implementation

template<typename T, typename Generator>
class Iterable {
public:
  class Iterator {
    struct Empty { };

public:
    using stored_type = std::conditional_t<
      std::is_lvalue_reference_v<T>,
      std::add_pointer_t<std::remove_reference_t<T>>,
      std::remove_reference_t<T>
    >;

    using value_type = T;
    struct iterator_category : public
      std::input_iterator_tag,
      std::conditional_t<
        !std::is_const_v<T>&& std::is_lvalue_reference_v<T>,
        std::output_iterator_tag,
        Empty
      >
    { };
    using difference_type = std::ptrdiff_t;
    using pointer = std::add_pointer_t<std::remove_reference_t<value_type>>;
    using reference = std::add_lvalue_reference_t<value_type>;

    static_assert(std::is_invocable_r_v<std::optional<stored_type>, Generator>,
      "Generator must be callable and return std::optional<T> - if T is a reference, it must return std::optional<T*>");

private:
    friend class Iterable<T, Generator>;

    std::optional<stored_type> _value;
    std::optional<Generator> _generator;


    Iterator() = default;

    explicit Iterator(Generator&& generator)
    noexcept(noexcept(generator()) && std::is_nothrow_move_constructible_v<Generator>)
    : _value(generator()),
      _generator(std::move(generator)) { }

public:
    Iterator& operator++() noexcept(noexcept(_value = _generator.value()())) {
      _value.reset();
      _value = _generator.value()();
      return *this;
    }
    Iterator operator++(int)
    noexcept(noexcept(_value = _generator.value()()) && std::is_nothrow_copy_constructible_v<Iterator>) {
      auto temp = *this;
      _value.reset();
      _value = _generator.value()();
      return temp;
    }
    value_type operator*() {
      if constexpr (std::is_lvalue_reference_v<T>) {
        return *(_value.value());
      } else {
        return _value.value();
      }
    }
    const value_type operator*() const {
      if constexpr (std::is_lvalue_reference_v<T>) {
        return *(_value.value());
      } else {
        return _value.value();
      }
    }
    bool operator!=(const Iterator& other) const noexcept {
      return _value.has_value() != other._value.has_value();
    }
    std::optional<stored_type> next()
    noexcept(std::is_nothrow_constructible_v<std::optional<stored_type>>&& noexcept(++(*this))) {
      IT_DEFER(([this, not_last = _value.has_value()]() noexcept(noexcept(++(*this))) {
          if (not_last) {
            ++(*this);
          }
        }));
      return std::move(_value);
    }
    static inline value_type get_val(std::optional<stored_type>& opt) {
      if constexpr (std::is_lvalue_reference_v<T>) {
        return *opt.value();
      } else {
        return opt.value();
      }
    }
  };

private:
  Iterator _begin;
  Iterator _end;

public:
  explicit Iterable(Generator&& generator) noexcept(noexcept(Iterator(std::move(generator))))
  : _begin(std::move(generator)) { }

  Iterator begin() const& noexcept(std::is_nothrow_copy_assignable_v<Iterator>) {
    return _begin;
  }
  Iterator begin() && noexcept(noexcept(std::is_nothrow_move_assignable_v<Iterator>)) {
    return std::move(_begin);
  }
  Iterator end() const& noexcept(std::is_nothrow_copy_assignable_v<Iterator>) {
    return _end;
  }
  Iterator end() && noexcept(noexcept(std::is_nothrow_move_assignable_v<Iterator>)) {
    return std::move(_end);
  }

  template<typename E, typename ... Args>
  auto to(Args&& ... args) const& noexcept(std::is_nothrow_constructible_v<E, const Iterable&, Args&&...>) {
    return E{ }(*this, std::forward<Args>(args)...);
  }

  template<typename E, typename ... Args>
  auto to(Args&& ... args) && noexcept(std::is_nothrow_constructible_v<E, Iterable&&, Args&&...>) {
    return E{ }(std::move(*this), std::forward<Args>(args)...);
  }
};

namespace generators {
template<typename Iter>
struct FromIter {
  using prev_iter = decltype(std::declval<Iter>().begin());
  using prev_value_type = decltype(*std::declval<prev_iter>());
  constexpr static bool is_lvalue_ref = std::is_lvalue_reference_v<prev_value_type>;
  using value_type = std::conditional_t<
    is_lvalue_ref,
    std::add_pointer_t<std::remove_reference_t<prev_value_type>>,
    prev_value_type
  >;
  prev_iter begin;
  const prev_iter end;
  std::optional<value_type> operator()() {
    if (begin != end) {
      IT_DEFER([this]() noexcept(noexcept(++begin)) { ++begin; });
      if constexpr (is_lvalue_ref) {
        return std::optional<value_type>(&*begin);
      } else {
        return std::optional<value_type>(*begin);
      }
    }
    return std::nullopt;
  }
};
template<typename T>
struct FromIter<std::initializer_list<T>> {
  constexpr static bool is_lvalue_ref = std::is_lvalue_reference_v<T>;
  using value_type = const T*;
  const T* begin;
  const T* const end;
  std::optional<value_type> operator()() {
    if (begin != end) {
      return std::optional<value_type>(begin++);
    }
    return std::nullopt;
  }
};

template<typename T>
struct RangeInf {
  T current;
  const T step;
  std::optional<T> operator()() noexcept(noexcept(current += step) &&
  std::is_nothrow_copy_constructible_v<T>&&
  std::is_nothrow_constructible_v<std::optional<T>, T>) {
    IT_DEFER([this]() noexcept(noexcept(current += step)) {
          current += step;
    });
    return std::optional<T>(current);
  }
};

template<typename T>
struct RangeGenerator {
  T current;
  const T end;
  const T step;
  std::optional<T> operator()() noexcept(noexcept(current >= end) && noexcept(current += step) &&
  std::is_nothrow_copy_constructible_v<T>&&
  std::is_nothrow_constructible_v<std::optional<T>>&&
  std::is_nothrow_constructible_v<std::optional<T>, T>) {
    if ((current >= end && step > 0 ) || (current <= end && step < 0)) {
      return std::optional<T>{ };
    }
    IT_DEFER([this]() noexcept(noexcept(current += step)) {
          current += step;
    });
    return std::optional<T>(current);
  }
};
}  // namespace generators

// Utility transformations

template<typename Iter>
auto iter(const Iter& iter) noexcept(noexcept(iter.begin()) && noexcept(iter.end()) &&
std::is_nothrow_constructible_v<generators::FromIter<Iter>, Iter, Iter>&&
std::is_nothrow_constructible_v<Iterable<
  decltype(*iter.begin()),
  generators::FromIter<Iter>>,
generators::FromIter<Iter>>) {
  using prev_iter = decltype(std::declval<const Iter&>().begin());
  using prev_value_type = decltype(*std::declval<prev_iter>());
  return Iterable<prev_value_type, generators::FromIter<const Iter&>>{
    { iter.begin(), iter.end() }
  };
}

template<typename Iter>
auto iter(Iter&& iter) noexcept(noexcept(iter.begin()) && noexcept(iter.end()) &&
std::is_nothrow_constructible_v<generators::FromIter<Iter>, Iter, Iter>&&
std::is_nothrow_constructible_v<Iterable<
  decltype(*iter.begin()),
  generators::FromIter<Iter>>,
generators::FromIter<Iter>>) {
  using prev_iter = decltype(std::declval<Iter>().begin());
  using prev_value_type = decltype(*std::declval<prev_iter>());
  static_assert(std::is_lvalue_reference_v<Iter>, "iter() requires an lvalue or const lvalue reference");
  return Iterable<prev_value_type, generators::FromIter<Iter>>{
    { iter.begin(), iter.end() }
  };
}

template<typename Iter>
auto iter(Iter&& begin, Iter&& end) noexcept(noexcept(std::move(begin)) && noexcept(std::move(end)) &&
std::is_nothrow_constructible_v<generators::FromIter<Iter>, Iter, Iter>&&
std::is_nothrow_constructible_v<Iterable<
  decltype(*begin),
  generators::FromIter<Iter>>,
generators::FromIter<Iter>>) {
  using prev_iter = decltype(begin);
  using prev_value_type = decltype(*std::declval<prev_iter>());
  return Iterable<prev_value_type, generators::FromIter<Iter>>{
    { std::move(begin), std::move(end) }
  };
}

template<typename T>
auto iter(std::initializer_list<T> iter) noexcept(
  std::is_nothrow_constructible_v<
    generators::FromIter<std::initializer_list<T>>,
    decltype(iter.begin()),
    decltype(iter.end())
  >&&
  std::is_nothrow_constructible_v<
    Iterable<const T&, generators::FromIter<std::initializer_list<T>>>,
    generators::FromIter<std::initializer_list<T>>
  >) {
  return Iterable<const T&, generators::FromIter<std::initializer_list<T>>>{
    { iter.begin(), iter.end() }
  };
}

template<typename T>
auto rangeInf(T&& start, T&& step = 1) noexcept(noexcept(Iterable<std::decay_t<T>,
generators::RangeInf<std::decay_t<T>>> {
    {
      std::forward<T>(start), std::forward<T>(step)
    }
  })) {
  using DecayedT = std::decay_t<T>;
  return Iterable<DecayedT, generators::RangeInf<DecayedT>>{
    { std::forward<T>(start), std::forward<T>(step) }
  };
}

template<typename T>
auto range(T&& start, T&& end, std::decay_t<T>&& step = 1) noexcept(noexcept(Iterable<std::decay_t<T>,
generators::RangeGenerator<std::decay_t<T>>> {
    { std::forward<T>(start), std::forward<T>(end), std::forward<T>(step) }
  })) {
  using DecayedT = std::decay_t<T>;
  return Iterable<DecayedT, generators::RangeGenerator<DecayedT>>{
    { std::forward<T>(start), std::forward<T>(end), std::forward<T>(step) }
  };
}

// Transformation implementations

namespace transformations {
struct Count {
  template<typename T, typename Iter>
  auto operator()(Iterable<T, Iter> iter) const {
    size_t count = 0;
    auto it = iter.begin();
    while (it.next().has_value()) {
      ++count;
    }
    return count;
  }
};

struct Sum {
  template<typename T, typename Iter>
  auto operator()(Iterable<T, Iter> iter) const {
    std::remove_reference_t<typename Iterable<T, Iter>::Iterator::value_type> total = 0;
    auto it = iter.begin();
    while (auto val = it.next()) {
      total += it.get_val(val);
    }
    return total;
  }
};

struct Map {
  template<typename T, typename Iter, typename Func>
  auto operator()(Iterable<T, Iter> it, Func&& func) const {
    using new_value_type = std::invoke_result_t<Func, decltype(*it.begin())>;
    static_assert(!std::is_reference_v<new_value_type>,
      "Mapping function must not return a reference type");

    struct Mapper {
      decltype(it.begin()) iter;
      std::remove_reference_t<Func> func;

      auto operator()() {
        auto next_val = iter.next();
        if (next_val.has_value()) {
          return std::optional{ func(iter.get_val(next_val)) };
        }
        return std::optional<decltype(func(iter.get_val(next_val)))>{ };
      }
    };
    return Iterable<typename std::invoke_result_t<Mapper>::value_type, Mapper>{
      { it.begin(), std::move(func) }
    };
  }
};

struct Filter {
  template<typename T, typename Iter, typename Func>
  auto operator()(Iterable<T, Iter> it, Func&& func) const {
    struct Filterer {
      decltype(it.begin()) iter;
      std::remove_reference_t<Func> func;

      auto operator()() {
        auto next_val = iter.next();
        while (next_val.has_value() && !func(iter.get_val(next_val))) {
          next_val = iter.next();
        }
        return next_val;
      }
    };
    return Iterable<T, Filterer>{
      { it.begin(), std::move(func) }
    };
  }
};

template<typename Target>
struct Collect {
  template<typename T, typename Iter>
  auto operator()(Iterable<T, Iter> iter) const {
    return Target(iter.begin(), iter.end());
  }
};

struct ForEach {
  template<typename T, typename Iter, typename Func>
  void operator()(Iterable<T, Iter> iter, Func&& func) const {
    auto it = iter.begin();
    while (auto val = it.next()) {
      func(it.get_val(val));
    }
  }
};

struct Zip {
  template<typename T, typename Iter, typename Y, typename OtherIter>
  auto operator()(Iterable<T, Iter> iter, Iterable<Y, OtherIter> other) const {
    using iter_type = decltype(iter.begin());
    using other_type = decltype(other.begin());
    using pair_type = std::pair<typename iter_type::value_type, typename other_type::value_type>;
    struct Zipper {
      iter_type iter1;
      iter_type iter15;
      other_type iter2;
      auto operator()() {
        auto val1 = iter1.next();
        auto val2 = iter2.next();
        if (val1.has_value() && val2.has_value()) {
          return std::optional<pair_type>{
            { iter1.get_val(val1), iter2.get_val(val2) }
          };
        }
        return std::optional<pair_type>{ };
      }
    };
    return Iterable<pair_type, Zipper>{
      { iter.begin(), iter.begin(), other.begin() }
    };
  }
};

struct Unique {
  template<typename T, typename Iter>
  auto operator()(Iterable<T, Iter> iter) const {
    using iter_type = decltype(iter.begin());
    using stored_type = typename iter_type::stored_type;
    static_assert(std::is_copy_constructible_v<stored_type>,
      "Unique transformation requires copy-constructible value type");

    struct Uniquifier {
      iter_type iter;
      std::unordered_set<std::decay_t<T>> seen_values;

      auto operator()() {
        auto next_val = iter.next();
        while (next_val.has_value()) {
          auto val = iter.get_val(next_val);
          if (seen_values.insert(val).second) {
            return next_val;
          }
          next_val = iter.next();
        }
        return std::optional<stored_type>{ };
      }
    };
    return Iterable<T, Uniquifier>{
      { iter.begin(), { } }
    };
  }
};

struct Chain {
  template<typename T, typename Iter, typename Iter2>
  auto operator()(Iterable<T, Iter> iter, Iterable<T, Iter2> iter2) const {
    using iter_type = decltype(iter.begin());
    using iter2_type = decltype(iter2.begin());

    struct Chainer {
      iter_type iter1;
      iter2_type iter2;
      bool using_first = true;

      auto operator()() {
        if (using_first) {
          auto val1 = iter1.next();
          if (val1.has_value()) {
            return val1;
          }
          using_first = false;
        }
        return iter2.next();
      }
    };
    return Iterable<T, Chainer>{
      { iter.begin(), iter2.begin() }
    };
  }
};

struct FirstN {
  template<typename T, typename Iter>
  auto operator()(Iterable<T, Iter> iter, size_t n) const {
    using iter_type = decltype(iter.begin());

    struct Firsterer {
      iter_type iter;
      size_t remaining;

      auto operator()() {
        if (remaining == 0) {
          return decltype(iter.next()) { };
        }
        --remaining;
        return iter.next();
      }
    };
    return Iterable<T, Firsterer>{
      { iter.begin(), n }
    };
  }
};

template<bool is_eager = true>
struct SkipN {
  template<typename T, typename Iter>
  auto operator()(Iterable<T, Iter> iter, size_t n) const {
    using iter_type = decltype(iter.begin());

    struct Skipper {
      iter_type iter;
      size_t remaining;

      auto operator()() {
        if constexpr (!is_eager) {
          while (remaining-- > 0) {
            ++iter;
          }
        }
        return iter.next();
      }
    };
    if constexpr (is_eager) {
      iter_type it = iter.begin();
      while (n-- > 0) {
        ++it;
      }
      return Iterable<T, Skipper>{
        { std::move(it), n }
      };
    } else {
      return Iterable<T, Skipper>{
        { iter.begin(), n }
      };
    }
  }
};

struct Dedup {
  template<typename T, typename Iter>
  auto operator()(Iterable<T, Iter> iter) const {
    using iter_type = decltype(iter.begin());
    using stored_type = typename iter_type::stored_type;
    static_assert(std::is_copy_constructible_v<stored_type>,
      "Dedup transformation requires copy-constructible value type");
    // static_assert(std::is_eq<T>,
    // "Dedup transformation requires equality-comparable value type");

    struct Deduplicator {
      iter_type iter;
      std::optional<stored_type> last_value;

      auto operator()() {
        auto next_val = iter.next();
        while (next_val.has_value()) {
          if (!last_value.has_value()) {
            last_value = next_val;
            return next_val;
          }
          if constexpr (std::is_lvalue_reference_v<T>) {
            if (*last_value.value() != *next_val.value()) {
              last_value = next_val;
              return next_val;
            }
          } else if (last_value != next_val) {
            last_value = next_val;
            return next_val;
          }
          next_val = iter.next();
        }
        return std::optional<stored_type>{ };
      }
    };

    return Iterable<T, Deduplicator>{
      { iter.begin(), std::nullopt }
    };
  }
};


// Placeholder for future transformations
struct SkipWhile;
struct TakeWhile;
struct SlidingWindow;
struct LastN;
struct Reverse;
struct Sorted;
struct JumpEvery;
struct Cycle;
}  // namespace transformations

}  // namespace itertools
