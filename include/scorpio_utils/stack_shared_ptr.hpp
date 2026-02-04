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
#include <optional>
#include <utility>

#include "scorpio_utils/assert.hpp"
#include "scorpio_utils/decorators.hpp"

namespace scorpio_utils {
template<typename T>
class StackSharedPtr {
  T _value;
  std::shared_ptr<T> _self_ptr;

public:
  template<typename ... Args>
  SCU_ALWAYS_INLINE explicit StackSharedPtr(Args&&... args)
  : _value(std::forward<Args>(args)...),
    _self_ptr(&_value, [](auto) { }) { }
  ~StackSharedPtr() {
    SCU_ASSERT(_self_ptr.use_count() == 1, "There are still shared_ptrs alive");
  }
  SCU_UNCOPYBLE(StackSharedPtr);
  SCU_UNMOVABLE(StackSharedPtr);
  T& operator*() {
    return _value;
  }
  const T& operator*() const {
    return _value;
  }
  T* operator->() {
    return &_value;
  }
  const T* operator->() const {
    return &_value;
  }
  T value() && {
    return std::move(_value);
  }
  T& value()& {
    return _value;
  }
  const T& value() const& {
    return _value;
  }
  std::shared_ptr<T> get_shared() {
    return _self_ptr;
  }
  operator std::shared_ptr<T>() {
    return _self_ptr;
  }
};
}  // namespace scorpio_utils
