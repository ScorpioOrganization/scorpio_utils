#pragma once

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <vector>
#include "scorpio_utils/decorators.hpp"

namespace scorpio_utils::network {
namespace {
SCU_NEVER_INLINE static bool is_big_endian_() {
  union {
    uint32_t i;
    char c[4];
  } test;
  test.i = 0x01020304;
  return test.c[0] == 1;
}
}  // namespace

inline const bool is_big_endian = is_big_endian_();

template<typename S, typename T>
void network_to_host(const S* src, T* target, size_t size) {
  if (is_big_endian) {
    std::memcpy(target, src, size);
  } else {
    const uint8_t* src_ptr = reinterpret_cast<const uint8_t*>(src);
    uint8_t* target_ptr = reinterpret_cast<uint8_t*>(target);
    for (size_t i = 0; i < size; ++i) {
      target_ptr[i] = src_ptr[size - i - 1];
    }
  }
}

template<typename S, typename T>
SCU_ALWAYS_INLINE void host_to_network(const S* src, T* target, size_t size) {
  network_to_host<S, T>(src, target, size);
}

template<typename T>
bool network_to_host(const std::vector<uint8_t>& src, T* target, size_t& pos) {
  if (pos + sizeof(T) > src.size()) {
    return false;
  }
  network_to_host(src.data() + pos, target, sizeof(T));
  pos += sizeof(T);
  return true;
}

template<typename T>
bool host_to_network(T src, std::vector<uint8_t>& target, size_t& pos) {
  if (pos + sizeof(T) > target.size()) {
    return false;
  }
  host_to_network<T>(&src, target.data() + pos, sizeof(T));
  pos += sizeof(T);
  return true;
}

template<typename T, typename P>
T network_to_host(const P* data) {
  static_assert(sizeof(T) > 1, "Type must be larger than 1 byte");
  static_assert(std::is_integral_v<T>|| std::is_floating_point_v<T>,
                "Type must be integral or floating point");
  T value{ };
  if (is_big_endian) {
    std::memcpy(&value, data, sizeof(T));
    return value;
  } else {
    uint8_t* value_ptr = reinterpret_cast<uint8_t*>(&value);
    const uint8_t* data_ptr = reinterpret_cast<const uint8_t*>(data);
    for (size_t i = 0; i < sizeof(T); ++i) {
      value_ptr[i] = data_ptr[sizeof(T) - i - 1];
    }
    return value;
  }
}

template<typename T, typename P>
SCU_ALWAYS_INLINE T host_to_network(const P* data) {
  return network_to_host<T>(data);
}
}  // namespace scorpio_utils::network
