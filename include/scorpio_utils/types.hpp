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
class Impossible final {
  Impossible() = delete;
  Impossible(const Impossible&) = delete;
  Impossible(Impossible&&) = delete;
  Impossible& operator=(const Impossible&) = delete;
  Impossible& operator=(Impossible&&) = delete;
};

class Success final {
public:
  constexpr Success() noexcept = default;
  constexpr Success(const Success&) noexcept = default;
  constexpr Success(Success&&) noexcept = default;
  constexpr Success& operator=(const Success&) noexcept = default;
  constexpr Success& operator=(Success&&) noexcept = default;

  static constexpr Success instance() noexcept {
    return Success();
  }

  constexpr bool operator==(const Success&) const noexcept {
    return true;
  }

  constexpr bool operator!=(const Success&) const noexcept {
    return false;
  }

  constexpr operator bool() const noexcept {
    return true;
  }
};

struct Empty { };

}  // namespace scorpio_utils
