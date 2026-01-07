#pragma once

#include <cstdint>
#include <optional>
#include <utility>
#include <vector>

namespace scorpio_utils::geometry {
template<typename T, typename UnitType = int64_t>
class TwoSidedVector {
  UnitType _starting_point;
  std::vector<T> _non_negative;
  std::vector<T> _negative;

  inline size_t negative_to_positive(const UnitType n) const {
    return static_cast<size_t>(-(n - _starting_point + 1));
  }

public:
  inline TwoSidedVector()
  : _starting_point(0) { }

  inline explicit TwoSidedVector(const UnitType negative_end)
  : _starting_point(static_cast<UnitType>(negative_end + 1)) { }

  inline TwoSidedVector(const UnitType negative_end, const UnitType positive_end, const T& v)
  : _starting_point(static_cast<UnitType>(negative_end + 1)),
    _non_negative(static_cast<size_t>(positive_end - negative_end - 1), v) { }

  inline TwoSidedVector(const UnitType negative_end, std::vector<T> values)
  : _starting_point(static_cast<UnitType>(negative_end + 1)),
    _non_negative(std::move(values)) { }

  inline UnitType get_negative_end() const {
    return static_cast<UnitType>(_starting_point - static_cast<UnitType>(_negative.size()) - 1);
  }

  inline UnitType get_positive_end() const {
    return static_cast<UnitType>(static_cast<UnitType>(_non_negative.size()) + _starting_point);
  }

  inline bool is_contained(const UnitType n) const {
    return n < get_positive_end() && n > get_negative_end();
  }

  inline std::optional<T> get_val(const UnitType n) const {
    if (!is_contained(n)) {
      return std::nullopt;
    }
    return n < _starting_point ?
           std::optional<T>(_negative[negative_to_positive(n)]) :
           std::optional<T>(_non_negative[static_cast<size_t>(n - _starting_point)]);
  }

  inline std::enable_if_t<!std::is_same_v<T, bool>, std::optional<std::reference_wrapper<const T>>> get_ptr(
    const UnitType n) const {
    if (!is_contained(n)) {
      return std::nullopt;
    }
    return n < _starting_point ?
           std::optional<std::reference_wrapper<const T>>(std::cref(_negative[negative_to_positive(n)])) :
           std::optional<std::reference_wrapper<const T>>(std::cref(_non_negative[static_cast<size_t>(n -
             _starting_point)]));
  }

  inline std::enable_if_t<!std::is_same_v<T, bool>, std::optional<std::reference_wrapper<T>>> get_mut_ptr(
    const UnitType n) {
    if (!is_contained(n)) {
      return std::nullopt;
    }
    return n < _starting_point ?
           std::optional<std::reference_wrapper<T>>(std::ref(_negative[negative_to_positive(n)])) :
           std::optional<std::reference_wrapper<T>>(std::ref(_non_negative[static_cast<size_t>(n -
             _starting_point)]));
  }

  inline void set_value(const UnitType n, T v) {
    if (n < _starting_point) {
      _negative[negative_to_positive(n)] = std::move(v);
    } else {
      _non_negative[static_cast<size_t>(n - _starting_point)] = std::move(v);
    }
  }

  /**
   * @brief Resizes the vector to contain the specified index.
   *
   * This method resizes the internal vectors to ensure that the specified index `n` is within the bounds of the vector.
   * If `n` is greater than or equal to the current positive end, the non-negative vector is resized.
   * If `n` is less than or equal to the current negative end, the negative vector is resized.
   *
   * @param n The index to be contained within the vector.
   * @param v The value to initialize the new elements with if resizing occurs.
   * @return true if the vector was resized, false otherwise.
   */
  inline bool resize_to_contain(const UnitType n, const T& v) {
    if (n >= get_positive_end()) {
      _non_negative.resize(static_cast<size_t>(n - _starting_point) + 1, v);
      return true;
    }
    if (n <= get_negative_end()) {
      _negative.resize(negative_to_positive(n) + 1, v);
      return true;
    }
    return false;
  }

  inline size_t size() const {
    return _negative.size() + _non_negative.size();
  }

  inline bool empty() const {
    return _negative.empty() && _non_negative.empty();
  }

  inline void clear() {
    _negative.clear();
    _non_negative.clear();
  }

  inline void push_back(const T v) {
    _non_negative.push_back(v);
  }

  inline void push_front(const T v) {
    _negative.push_back(v);
  }

  inline UnitType get_starting_point() const {
    return _starting_point;
  }
};
}  // namespace scorpio_utils::geometry
