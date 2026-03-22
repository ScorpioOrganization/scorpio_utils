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
#include <typeinfo>

#include "scorpio_utils/assert.hpp"
#include "scorpio_utils/decorators.hpp"
#include "scorpio_utils/type_traits.hpp"

namespace scorpio_utils {
template<typename T>
SCU_ALWAYS_INLINE auto clone(const T& v) {
  static_assert(std::is_copy_constructible_v<T>, "clone requires copy constructible types");
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

/**
 * A utility function to perform dynamic_cast on smart pointers and raw pointers.
 * This function can be used to cast a pointer of one type to another type,
 * while ensuring that the cast is valid at runtime. The function supports std::unique_ptr, std::shared_ptr,
 * and raw pointers. It will assert if the cast is not valid. The function also preserves
 * the const and volatile qualifiers of the input pointer in the output pointer.
 *
 * \note When `from == nullptr`, the function will return `nullptr` without performing any cast or assertion.
 * \note When using with std::unique_ptr, the input pointer must be passed by value to ensure the ownership transfer.
 *       The function will release the ownership of the input pointer and return a new std::unique_ptr with the casted pointer.
 * \note When using with std::unique_ptr with a custom deleter,
 *       the function will preserve the custom deleter in the returned std::unique_ptr.
 *
 * \tparam To The type to cast to.
 * \tparam From The type to cast from.
 *
 * \param from The pointer to cast.
 *
 * \return The casted pointer.
 */
template<typename To, typename From>
SCU_ALWAYS_INLINE auto dynamic_as(From&& from) {
  static_assert(!std::is_reference_v<To>, "dynamic_as does not support reference types");
  static_assert(!std::is_const_v<To>&& !std::is_volatile_v<To>, "dynamic_as takes cv qualification from the From type");
  using DecayedFrom = std::decay_t<From>;
  #define SCU_DYNAMIC_AS_TARGET_TYPE(SourceType) \
  std::conditional_t< \
    std::is_volatile_v<SourceType>, \
    std::add_volatile_t< \
      std::conditional_t< \
        std::is_const_v<SourceType>, \
        std::add_const_t<To>, \
        To \
      >>, \
    std::conditional_t< \
      std::is_const_v<SourceType>, \
      std::add_const_t<To>, \
      To \
    > \
  >
  if constexpr (is_unique_ptr_v<DecayedFrom>) {
    static_assert(IsUniquePtr<DecayedFrom>::is_polymorphic,
                  "dynamic_as requires polymorphic types when used with unique_ptr");
    static_assert(!std::is_lvalue_reference_v<From>,
                  "dynamic_as requires to pass unique_ptr by value to ensure the ownership transfer");
    static_assert(!std::is_const_v<From>,
                  "dynamic_as does not support const unique_ptr since it cannot transfer ownership");
    using TargetType = SCU_DYNAMIC_AS_TARGET_TYPE(typename IsUniquePtr<DecayedFrom>::ElementType);
    constexpr bool is_default_deleter = std::is_same_v<
      std::default_delete<typename IsUniquePtr<DecayedFrom>::ElementType>,
      typename IsUniquePtr<DecayedFrom>::DeleterType
    >;
    // Define is used here instead of `using ReturnType = ...;`to prevent lsp from displaying
    // it as a ReturnType in the function signature, which is not informative at all
    #define SCU_DYNAMIC_AS_RETURN_TYPE std::conditional_t< \
    is_default_deleter, \
    std::unique_ptr<TargetType>, \
    std::unique_ptr<TargetType, typename IsUniquePtr<DecayedFrom>::DeleterType>>
    if (from.get() == nullptr) {
      if constexpr (is_default_deleter) {
        return SCU_DYNAMIC_AS_RETURN_TYPE(nullptr);
      } else {
        return SCU_DYNAMIC_AS_RETURN_TYPE(nullptr, from.get_deleter());
      }
    }
    auto result = dynamic_cast<std::add_pointer_t<TargetType>>(from.get());
    SCU_ASSERT(result != nullptr,
        "dynamic_as failed to cast from " << typeid(From).name() << " to " << typeid(To).name());
    if (SCU_LIKELY(result != nullptr)) {
      from.release();
    }
    if constexpr (is_default_deleter) {
      return SCU_DYNAMIC_AS_RETURN_TYPE(result);
    } else {
      return SCU_DYNAMIC_AS_RETURN_TYPE(result, from.get_deleter());
    }
    #undef SCU_DYNAMIC_AS_RETURN_TYPE
  } else if constexpr (is_shared_ptr_v<DecayedFrom>) {
    static_assert(IsSharedPtr<DecayedFrom>::is_polymorphic,
                  "dynamic_as requires polymorphic types when used with shared_ptr");
    using TargetType = SCU_DYNAMIC_AS_TARGET_TYPE(typename IsSharedPtr<DecayedFrom>::ElementType);
    if (from.get() == nullptr) {
      return std::shared_ptr<TargetType>(nullptr);
    }
    auto result = std::dynamic_pointer_cast<TargetType>(std::forward<From>(from));
    SCU_ASSERT(result != nullptr,
        "dynamic_as failed to cast from " << typeid(From).name() << " to " << typeid(To).name());
    return result;
  } else if constexpr (std::is_pointer_v<DecayedFrom>) {
    static_assert(std::is_polymorphic_v<std::decay_t<std::remove_pointer_t<DecayedFrom>>>,
                  "dynamic_as requires polymorphic types when used with raw pointers");
    using TargetType = SCU_DYNAMIC_AS_TARGET_TYPE(std::remove_pointer_t<DecayedFrom>);
    if (from == nullptr) {
      return static_cast<std::add_pointer_t<TargetType>>(nullptr);
    }
    auto result = dynamic_cast<std::add_pointer_t<TargetType>>(from);
    SCU_ASSERT(result != nullptr,
        "dynamic_as failed to cast from " << typeid(From).name() << " to " << typeid(To).name());
    return result;
  } else {
    static_assert(
      is_unique_ptr_v<DecayedFrom>||
      is_shared_ptr_v<DecayedFrom>||
      std::is_pointer_v<DecayedFrom>,
      "dynamic_as can only be used with pointers std::unique_ptr, std::shared_ptr, or raw pointers");
  }
  #undef SCU_DYNAMIC_AS_TARGET_TYPE
}
}  // namespace scorpio_utils
