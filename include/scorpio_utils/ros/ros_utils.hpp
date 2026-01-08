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

#include <type_traits>

namespace scorpio_utils::ros {
template<typename, typename = std::void_t<>>
struct IsMessageType : std::false_type { };
template<typename T>
struct IsMessageType<T, std::void_t<typename T::template ConstUniquePtrWithDeleter<>>>: std::true_type { };

template<typename T, typename = std::void_t<>>
struct IsServiceType : std::false_type { };
template<typename T>
struct IsServiceType<T, std::void_t<typename T::Request, typename T::Response>>: std::true_type { };
}  // namespace scorpio_utils::ros
