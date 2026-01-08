#pragma once

#include <cstdint>
#include <functional>
#include <iostream>
#include <string>
#include <utility>
#include "scorpio_utils/compare.hpp"
#include "scorpio_utils/decorators.hpp"

namespace scorpio_utils::network {
class Ipv4 {
  uint32_t _ip;

public:
  constexpr SCU_ALWAYS_INLINE Ipv4()
  : _ip(0) { }
  constexpr SCU_ALWAYS_INLINE explicit Ipv4(uint32_t ip)
  : _ip(ip) { }
  constexpr SCU_ALWAYS_INLINE explicit Ipv4(uint8_t first, uint8_t second, uint8_t third, uint8_t fourth)
  : _ip((static_cast<uint32_t>(first) << 24) |
      (static_cast<uint32_t>(second) << 16) |
      (static_cast<uint32_t>(third) << 8) |
      static_cast<uint32_t>(fourth)) { }

  constexpr SCU_ALWAYS_INLINE explicit Ipv4(uint8_t ip[4])
  : Ipv4(ip[0], ip[1], ip[2], ip[3]) { }

  constexpr SCU_ALWAYS_INLINE auto first() const noexcept {
    return static_cast<uint8_t>(0xff & (_ip >> 24));
  }

  constexpr SCU_ALWAYS_INLINE auto second() const noexcept {
    return static_cast<uint8_t>(0xff & (_ip >> 16));
  }

  constexpr SCU_ALWAYS_INLINE auto third() const noexcept {
    return static_cast<uint8_t>(0xff & (_ip >> 8));
  }

  constexpr SCU_ALWAYS_INLINE auto fourth() const noexcept {
    return static_cast<uint8_t>(0xff & _ip);
  }

  constexpr SCU_ALWAYS_INLINE auto ip() const noexcept {
    return _ip;
  }

  uint32_t ip_network() const noexcept;

  static Ipv4 from_network(uint32_t ip_network) noexcept;
  static Ipv4 from_string(const std::string& ip_str) noexcept;

  SCU_ALWAYS_INLINE constexpr bool operator==(const Ipv4& other) const noexcept {
    return _ip == other._ip;
  }

  SCU_ALWAYS_INLINE constexpr SCU_NEQ_FROM_EQ(Ipv4)

  operator std::string() const;

  SCU_ALWAYS_INLINE std::string str() const {
    return static_cast<std::string>(*this);
  }
};
static inline constexpr Ipv4 localhost{ 127, 0, 0, 1 };
using Port = uint16_t;
}  // namespace scorpio_utils::network

std::ostream& operator<<(std::ostream& os, const scorpio_utils::network::Ipv4& ip);

template<>
struct std::hash<scorpio_utils::network::Ipv4> {
  std::size_t operator()(const scorpio_utils::network::Ipv4& ip) const noexcept {
    return std::hash<uint32_t>()(ip.ip());
  }
};

template<>
struct std::hash<std::pair<scorpio_utils::network::Ipv4, scorpio_utils::network::Port>> {
  std::size_t operator()(
    const std::pair<scorpio_utils::network::Ipv4,
    scorpio_utils::network::Port>& p) const noexcept {
    return std::hash<uint64_t>()((static_cast<uint64_t>(p.first.ip()) << 32) | p.second);
  }
};
