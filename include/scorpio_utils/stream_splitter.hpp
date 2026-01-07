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
