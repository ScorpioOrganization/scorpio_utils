#pragma once

#include <algorithm>
#include <cstdint>
#include <optional>
#include <utility>
#include <vector>
#include "scorpio_utils/optional_utils.hpp"
#include "point.hpp"
#include "two_sided_vector.hpp"

namespace scorpio_utils::geometry {
template<typename T, typename UT = int64_t>
class Plane {
public:
  typedef UT UnitType;
  typedef Point<UnitType> PointOnPlane;

private:
  TwoSidedVector<TwoSidedVector<T, UnitType>, UnitType> _plane;

public:
  inline Plane() = default;

  inline explicit Plane(const UnitType negative_end)
  : _plane(negative_end) { }

  inline Plane(const PointOnPlane& negative_end, std::vector<std::vector<T>> values)
  : _plane(negative_end.x) {
    for (auto&& value : std::move(values)) {
      _plane.push_back(TwoSidedVector<T, UnitType>(negative_end.y, std::move(value)));
    }
  }

  inline PointOnPlane get_negative_end() const {
    if (_plane.empty()) {
      return PointOnPlane { -1, -1 };
    }
    const auto last_row = _plane.get_negative_end();
    const auto y = optional_map([](const std::reference_wrapper<const TwoSidedVector<T,
        UnitType>> v) -> UnitType {
          return v.get().get_negative_end();
        }, _plane.get_ptr(last_row + 1));
    return PointOnPlane {
      last_row,
      y.value_or(-1),
    };
  }

  inline PointOnPlane get_positive_end() const {
    if (_plane.empty()) {
      return PointOnPlane { 0, 0 };
    }
    const auto first_row = _plane.get_positive_end();
    const auto y = optional_map([](const std::reference_wrapper<const TwoSidedVector<T,
        UnitType>> v) -> UnitType {
          return v.get().get_positive_end();
    }, _plane.get_ptr(first_row - 1));
    return PointOnPlane {
      first_row,
      y.value_or(0),
    };
  }

  inline bool is_contained(const PointOnPlane& point) const {
    return point.x < get_positive_end().x && point.x > get_negative_end().x &&
           point.y < get_positive_end().y && point.y > get_negative_end().y;
  }

  inline std::optional<T> get_val(const PointOnPlane point) const {
    if (!is_contained(point)) {
      return std::nullopt;
    }
    return _plane.get_ptr(point.x).value().get().get_val(point.y);
  }

  inline std::optional<std::reference_wrapper<const T>> get_ptr(const PointOnPlane point) const {
    if (!is_contained(point)) {
      return std::nullopt;
    }
    return _plane.get_ptr(point.x).value().get().get_ptr(point.y);
  }

  inline std::optional<std::reference_wrapper<T>> get_mut_ptr(const PointOnPlane point) {
    if (!is_contained(point)) {
      return std::nullopt;
    }
    return _plane.get_mut_ptr(point.x).value().get().get_mut_ptr(point.y);
  }

  inline void set_value(const PointOnPlane& point, const T v) {
    return _plane.get_mut_ptr(point.x).value().get().set_value(point.y, v);
  }

  inline bool resize_to_contain(const PointOnPlane& point, const T& v) {
    bool ans = false;
    for (UnitType i = get_negative_end().x + 1; i != get_positive_end().x; ++i) {
      ans |= _plane.get_mut_ptr(i).value().get().resize_to_contain(point.y, v);
    }
    ans |= _plane.resize_to_contain(point.x,
          TwoSidedVector<T, UnitType>(
            std::min(static_cast<UnitType>(point.y - 1), get_negative_end().y),
            std::max(static_cast<UnitType>(point.y + 1), get_positive_end().y),
            v));
    return ans;
  }

  inline UnitType get_starting_point() const {
    return _plane.get_starting_point();
  }

  inline size_t get_surface() const {
    const auto negative_end = get_negative_end();
    const auto positive_end = get_positive_end();
    return static_cast<size_t>(positive_end.x - negative_end.x - 1) *
           static_cast<size_t>(positive_end.y - negative_end.y - 1);
  }
};
}  // namespace scorpio_utils::geometry
