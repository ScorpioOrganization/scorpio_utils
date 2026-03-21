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
#include <new>
#include <utility>

#include "scorpio_utils/assert.hpp"
#include "scorpio_utils/decorators.hpp"
#include "scorpio_utils/type_traits.hpp"

namespace scorpio_utils {
template<typename T>
SCU_ALWAYS_INLINE SCU_CONST_FUNC T clone(T v) {
  return v;
}

/**
 * A utility structure to help with visitor overloading.
 * This is useful for creating a visitor that can handle multiple types
 * without needing to define a separate visitor for each type.
 *
 * \tparam T The types to be overloaded.
 */
template<class ... T>
struct VisitorOverloadingHelper : T ... { using T::operator() ...; };
template<class ... T>
VisitorOverloadingHelper(T ...)->VisitorOverloadingHelper<T...>;

// Define hardware interference size if not defined by the standard library
#if defined(__cpp_lib_hardware_interference_size) && false  // Disable for now
# define SCU_HARDWARE_DESTRUCTIVE_INTERFERENCE_SIZE (std::hardware_destructive_interference_size)
#else
# define SCU_HARDWARE_DESTRUCTIVE_INTERFERENCE_SIZE 64
#endif

template<typename To, typename From>
SCU_PURE SCU_ALWAYS_INLINE auto dynamic_as(From&& from) {
  static_assert(!std::is_reference_v<To>, "dynamic_as does not support reference types");
  if constexpr (is_unique_ptr_v<std::decay_t<From>>) {
    static_assert(IsUniquePtr<std::decay_t<From>>::is_polymorphic,
                  "dynamic_as requires polymorphic types when used with unique_ptr");
    auto result = dynamic_cast<To*>(from.get());
    SCU_ASSERT(result != nullptr,
        "dynamic_as failed to cast from " << typeid(From).name() << " to " << typeid(To).name());
    from.release();
    return std::unique_ptr<To>(result);
  } else if constexpr (is_shared_ptr_v<std::decay_t<From>>) {
    static_assert(IsSharedPtr<std::decay_t<From>>::is_polymorphic,
                  "dynamic_as requires polymorphic types when used with shared_ptr");
    auto result = std::dynamic_pointer_cast<To>(std::forward<From>(from));
    SCU_ASSERT(result != nullptr,
        "dynamic_as failed to cast from " << typeid(From).name() << " to " << typeid(To).name());
    return result;
  } else if constexpr (std::is_pointer_v<std::decay_t<From>>) {
    static_assert(std::is_polymorphic_v<std::decay_t<std::remove_pointer_t<std::decay_t<From>>>>,
                  "dynamic_as requires polymorphic types when used with raw pointers");
    auto result = dynamic_cast<To*>(from);
    SCU_ASSERT(result != nullptr,
        "dynamic_as failed to cast from " << typeid(From).name() << " to " << typeid(To).name());
    return result;
  } else {
    static_assert(
      is_unique_ptr_v<std::decay_t<From>>||
      is_shared_ptr_v<std::decay_t<From>>||
      std::is_pointer_v<std::decay_t<From>>||
      std::is_same_v<To, From>,
      "dynamic_as can only be used for pointer (raw or shared) types or when To and From are the same type");
    return from;
  }
}
}  // namespace scorpio_utils
