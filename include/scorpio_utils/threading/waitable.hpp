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

#include <mutex>
#include <vector>

namespace scorpio_utils::threading {
class WaitGroup;
class WaitableBase {
  friend class WaitGroup;

  std::vector<WaitGroup*> _wait_groups;
  std::mutex _mutex;
  bool _done;
  void wait_with(WaitGroup* wait_group);
  void remove_wait_group(WaitGroup* wait_group);

protected:
  void done();
  WaitableBase()
  : _done(false) { }
  WaitableBase(const WaitableBase&) = delete;
  WaitableBase& operator=(const WaitableBase&) = delete;

public:
  virtual ~WaitableBase() = default;
};
}  // namespace scorpio_utils::threading
