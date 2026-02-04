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

#include "scorpio_utils/buffered_file_saver.hpp"

#include "scorpio_utils/misc.hpp"

bool scorpio_utils::BufferedFileSaver::flush() {
  if (_operations.empty()) {
    return true;
  }
  if (!_preopened) {
    _file.open(_filename, std::ios_base::out | _file_openmode);
  }
  if (_file.fail() || !_file.is_open()) {
    _file.clear();
    return false;
  }
  for (auto& x : _operations) {
    std::visit(VisitorOverloadingHelper{
      [this](std::string&& v) -> void {
        _file << std::forward<std::string>(v);
      },
      [this](std::streampos&& v) -> void {
        _file.seekp(std::forward<std::streampos>(v));
      },
      [this](std::pair<std::streamoff, std::ios_base::seekdir>&& v) -> void {
        auto [offset, dir] = v;
        _file.seekp(
          std::forward<std::streamoff>(offset),
          std::forward<std::ios_base::seekdir>(dir));
      },
    }, std::move(x));
  }
  _file.flush();
  if (!_preopened) {
    _file.close();
    if (_file.fail()) {
      _file.clear();
      return false;
    }
  }
  _operations.clear();
  return true;
}
