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

#include "threading/waitable.hpp"

#include <algorithm>
#include "threading/wait_group.hpp"

void scorpio_utils::threading::WaitableBase::wait_with(WaitGroup* const wait_group) {
  if (_done) {
    wait_group->task_done();
  } else {
    _wait_groups.push_back(wait_group);
  }
}

void scorpio_utils::threading::WaitableBase::remove_wait_group(WaitGroup* wait_group) {
  _wait_groups.erase(std::remove(_wait_groups.begin(), _wait_groups.end(), wait_group), _wait_groups.end());
}

void scorpio_utils::threading::WaitableBase::done() {
  std::unique_lock<std::mutex> lock(_mutex);
  _done = true;
  for (auto _wait_group : _wait_groups) {
    std::lock_guard<std::mutex> lock(_wait_group->_mutex);
    _wait_group->task_done();
  }
  _wait_groups.clear();
}
