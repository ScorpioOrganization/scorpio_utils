#pragma once

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
    index %= _data.size();
    if (_data[index].has_value()) {
      return OrdererAddResult::ALREADY_PRESENT;
    }
    _data[index] = data;
    ++_current_count;
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
    index %= _data.size();
    if (_data[index].has_value()) {
      return OrdererAddResult::ALREADY_PRESENT;
    }
    _data[index] = std::move(data);
    ++_current_count;
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
    return ans;
  }

  const std::optional<T>& peek() const noexcept {
    return _data[_current_index % _data.size()];
  }

  auto get_contained() const {
    std::vector<std::pair<size_t, size_t>> contained;
    contained.emplace_back(0, _current_index);
    for (size_t i = 0; i < _data.size(); ++i) {
      const auto idx = i + _current_index;
      if (_data[idx % _data.size()].has_value()) {
        if (contained.back().second == idx) {
          ++contained.back().second;
        } else {
          contained.emplace_back(idx, idx + 1);
        }
      }
    }
    return contained;
  }
};
}  // namespace scorpio_utils::network
