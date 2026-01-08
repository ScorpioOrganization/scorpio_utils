#include "scorpio_utils/network/ip.hpp"

extern "C" {
  #include <netinet/in.h>
  #include <arpa/inet.h>
}

uint32_t scorpio_utils::network::Ipv4::ip_network() const noexcept {
  return htonl(_ip);
}

scorpio_utils::network::Ipv4 scorpio_utils::network::Ipv4::from_network(uint32_t ip_network) noexcept {
  return scorpio_utils::network::Ipv4(ntohl(ip_network));
}

scorpio_utils::network::Ipv4 scorpio_utils::network::Ipv4::from_string(const std::string& ip_str) noexcept {
  struct in_addr addr;
  if (inet_pton(AF_INET, ip_str.c_str(), &addr) != 1) {
    return scorpio_utils::network::Ipv4();
  }
  return scorpio_utils::network::Ipv4(ntohl(addr.s_addr));
}

scorpio_utils::network::Ipv4::operator std::string() const {
  return std::to_string(first()) + "." +
         std::to_string(second()) + "." +
         std::to_string(third()) + "." +
         std::to_string(fourth());
}

std::ostream& operator<<(std::ostream& os, const scorpio_utils::network::Ipv4& ip) {
  os << ip.str();
  return os;
}
