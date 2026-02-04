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

namespace scorpio_utils {

/**
 * A utility structure to create a named pointer to member.
 * This is useful for reflection or serialization purposes where
 * you want to have a name associated with a pointer to member.
 *
 * \tparam T The type of the class containing the member.
 * \tparam Y The type of the member.
 * \tparam Ptr The pointer to member.
 * \tparam Name The name of the member as a string literal.
 */
template<typename T, typename Y, Y T::* Ptr, const char* Name>
struct NamedPointerToMember {
  static_assert(Ptr != nullptr, "Pointer to member cannot be null");
  static_assert(std::is_member_object_pointer_v<Y T::*>, "Ptr must be a member object pointer");
  using type = T;
  using field_type = Y;
  static constexpr Y T::* ptr = Ptr;
  static constexpr const char* name = Name;
};
}  // namespace scorpio_utils

#define SCU_CREATE_NAMED_POINTER_TO_MEMBER(Type, Field, NAME_ALIAS) \
  scorpio_utils::NamedPointerToMember<Type, decltype(Type::Field), &Type::Field, NAME_ALIAS>
