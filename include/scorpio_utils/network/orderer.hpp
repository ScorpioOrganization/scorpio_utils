#pragma once

#include <algorithm>
#include <functional>
#include <optional>
#include <type_traits>
#include <utility>
#include <vector>

#include "scorpio_utils/decorators.hpp"

namespace scorpio_utils::network {
enum class OrdererAddResult : uint8_t {
  SUCCESS,
  TOO_OLD,
  TOO_NEW,
  ALREADY_PRESENT,
};

template<typename T>
class Orderer {
  static_assert(std::is_copy_assignable_v<T>|| std::is_move_assignable_v<T>, "T must be move constructible");
  std::vector<std::optional<T>> _data;
  size_t _current_index;
  size_t _current_count;
  // Held index ranges [begin, end), sorted, disjoint, non-adjacent, all >= _current_index.
  // Maintained incrementally by add()/next() so get_contained() is O(#ranges) instead of
  // scanning the whole buffer - with large buffers and frequent heartbeats the scan
  // dominated a connection's processing budget.
  std::vector<std::pair<size_t, size_t>> _held_ranges;

  void insert_into_ranges(size_t index) {
    // First range that starts after index (its predecessor, if any, starts at or before).
    auto iter = std::upper_bound(_held_ranges.begin(), _held_ranges.end(), index,
        [](size_t value, const std::pair<size_t, size_t>& range) {
          return value < range.first;
      });
    bool merged = false;
    if (iter != _held_ranges.begin()) {
      const auto prev = std::prev(iter);
      if (prev->second == index) {
        prev->second = index + 1;
        iter = prev;
        merged = true;
      }
    }
    if (!merged) {
      iter = _held_ranges.insert(iter, { index, index + 1 });
    }
    const auto next_range = std::next(iter);
    if (next_range != _held_ranges.end() && iter->second == next_range->first) {
      iter->second = next_range->second;
      std::ignore = _held_ranges.erase(next_range);
    }
  }

public:
  explicit Orderer(size_t size)
  : _data(size, std::nullopt),
    _current_index(0),
    _current_count(0) { }

  SCU_ALWAYS_INLINE auto get_size() const noexcept {
    return _data.size();
  }

  SCU_ALWAYS_INLINE constexpr auto get_current_count() const noexcept {
    return _current_count;
  }

  SCU_ALWAYS_INLINE constexpr auto get_current_index() const noexcept {
    return _current_index;
  }

  bool set_size(size_t new_size) {
    if (_current_count != 0) {
      return false;
    }
    _data.resize(new_size, std::nullopt);
    return true;
  }

  template<typename U = T>
  std::enable_if_t<std::is_copy_assignable_v<U>, OrdererAddResult> add(size_t index, const T& data) {
    if (index < _current_index) {
      return OrdererAddResult::TOO_OLD;
    }
    if (_current_index + _data.size() <= index) {
      return OrdererAddResult::TOO_NEW;
    }
    const auto slot = index % _data.size();
    if (_data[slot].has_value()) {
      return OrdererAddResult::ALREADY_PRESENT;
    }
    _data[slot] = data;
    ++_current_count;
    insert_into_ranges(index);
    return OrdererAddResult::SUCCESS;
  }

  template<typename U = T>
  std::enable_if_t<std::is_move_assignable_v<U>, OrdererAddResult> add(size_t index, T&& data) {
    if (index < _current_index) {
      return OrdererAddResult::TOO_OLD;
    }
    if (_current_index + _data.size() <= index) {
      return OrdererAddResult::TOO_NEW;
    }
    const auto slot = index % _data.size();
    if (_data[slot].has_value()) {
      return OrdererAddResult::ALREADY_PRESENT;
    }
    _data[slot] = std::move(data);
    ++_current_count;
    insert_into_ranges(index);
    return OrdererAddResult::SUCCESS;
  }

  std::optional<T> next() {
    const auto index = _current_index % _data.size();
    if (_current_count == 0 || !_data[index].has_value()) {
      return std::nullopt;
    }
    auto ans = std::exchange(_data[index], std::nullopt);
    --_current_count;
    ++_current_index;
    // The consumed element is always the head of the first held range.
    if (!_held_ranges.empty() && _held_ranges.front().first < _current_index) {
      if (_held_ranges.front().second <= _current_index) {
        std::ignore = _held_ranges.erase(_held_ranges.begin());
      } else {
        _held_ranges.front().first = _current_index;
      }
    }
    return ans;
  }

  const std::optional<T>& peek() const noexcept {
    return _data[_current_index % _data.size()];
  }

  auto get_contained() const {
    std::vector<std::pair<size_t, size_t>> contained;
    contained.reserve(_held_ranges.size() + 1);
    contained.emplace_back(0, _current_index);
    for (const auto& range : _held_ranges) {
      if (contained.back().second == range.first) {
        contained.back().second = range.second;
      } else {
        contained.push_back(range);
      }
    }
    return contained;
  }
};
}  // namespace scorpio_utils::network
