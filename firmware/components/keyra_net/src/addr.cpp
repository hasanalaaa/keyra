#include "addr.hpp"

#include <cstdio>

namespace keyra::net {

uint32_t fromV4Mapped(const uint8_t a[16]) {
  for (int i = 0; i < 10; ++i) {
    if (a[i] != 0) return 0;
  }
  if (a[10] != 0xFF || a[11] != 0xFF) return 0;
  return ipv4(a[12], a[13], a[14], a[15]);
}

Via classify(uint32_t localIp) { return localIp == kApIp ? Via::Ap : Via::Home; }

bool inApSubnet(uint32_t ip) { return (ip & kApMask) == (kApIp & kApMask) && ip != kApIp; }

std::string toString(uint32_t ip) {
  char buf[16];
  std::snprintf(buf, sizeof buf, "%u.%u.%u.%u", unsigned(ip >> 24), unsigned((ip >> 16) & 0xFF),
                unsigned((ip >> 8) & 0xFF), unsigned(ip & 0xFF));
  return buf;
}

}  // namespace keyra::net
