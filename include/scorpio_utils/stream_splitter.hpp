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

#include <ios>
#include <tuple>
#include <type_traits>

#include "scorpio_utils/decorators.hpp"
#include "scorpio_utils/type_traits.hpp"

namespace scorpio_utils {
/**
 * Utility class to split output to multiple streams.
 * It can be used to write the same data to multiple output streams.
 * It supports flushing and seeking operations if the streams support them.
 */
template<typename ... Streams>
class StreamSplitter {
  static_assert(sizeof...(Streams) > 0, "StreamSplitter requires at least one stream");

  std::tuple<Streams& ...> _streams;

public:
  inline explicit StreamSplitter(Streams& ... streams)
  : _streams(std::tie(streams ...)) { }

  template<typename T>
  inline void operator()(const T& data) {
    static_assert((HasBitShiftLeft<Streams, T>::value && ...),
      "All streams must support bitwise left shift operator for the given data type");
    std::apply([&data](auto& ... stream) {
        ((stream << data), ...);
    }, _streams);
  }

  template<typename T>
  SCU_ALWAYS_INLINE StreamSplitter& operator<<(const T& data) {
    operator()(data);
    return *this;
  }

  inline std::enable_if_t<(HasFlush<Streams>::value || ...), void> flush() {
    std::apply([](auto& ... streams) {
        (HasFlush<Streams>{ }(streams), ...);
    }, _streams);
  }

  inline std::enable_if_t<(HasSeekpPos<Streams>::value && ...), void> seekp(std::streampos pos) {
    std::apply([pos, this](auto& ... streams) {
        (streams.seekp(pos), ...);
    }, _streams);
  }

  inline std::enable_if_t<(HasSeekpOffset<Streams>::value && ...), void> seekp(
    std::streamoff offset,
    std::ios_base::seekdir dir) {
    std::apply([offset, dir, this](auto& ... streams) {
        (streams.seekp(offset, dir), ...);
    }, _streams);
  }
};
}  // namespace scorpio_utils
