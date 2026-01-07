#pragma once

#include <fstream>
#include <sstream>
#include <string>
#include <tuple>
#include <utility>
#include <variant>
#include <vector>

#include "scorpio_utils/decorators.hpp"
#include "scorpio_utils/type_traits.hpp"

namespace scorpio_utils {
/**
 * BufferedFileSaver allows manipulating file but actually opening it and applying changes during flush
 */
class BufferedFileSaver {
  std::string _filename;
  std::ios_base::openmode _file_openmode;
  std::fstream _file;
  std::vector<std::variant<
      std::string,
      std::streampos,
      std::pair<std::streamoff, std::ios_base::seekdir>
    >> _operations;
  bool _preopened;

public:
  SCU_ALWAYS_INLINE BufferedFileSaver(
    std::string filename,
    std::ios_base::openmode file_openmode = static_cast<std::ios_base::openmode>(0),
    bool preopened = false)
  : _filename(std::move(filename)),
    _file_openmode(std::move(file_openmode)),
    _preopened(preopened) {
    if (_preopened) {
      _file.open(_filename, std::ios_base::out | _file_openmode);
    }
  }

  SCU_ALWAYS_INLINE ~BufferedFileSaver() {
    std::ignore = flush();
  }

  template<typename T>
  BufferedFileSaver& operator<<(T v) {
    static_assert(HasBitShiftLeft<std::ostream, T>::value, "");
    std::ostringstream buffer;
    buffer << v;
    _operations.push_back(buffer.str());
    return *this;
  }

  bool flush();

  SCU_ALWAYS_INLINE void seekp(std::streampos pos) {
    _operations.push_back(std::move(pos));
  }

  SCU_ALWAYS_INLINE void seekp(std::streamoff offset, std::ios_base::seekdir dir) {
    _operations.push_back(std::make_pair(std::move(offset), std::move(dir)));
  }
};
}  // namespace scorpio_utils
