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

HomeError homeErrorFor(uint16_t reason) {
  switch (reason) {
    case 14:   // MIC_FAILURE
    case 15:   // 4WAY_HANDSHAKE_TIMEOUT: a WPA2/3-PSK network refusing the key
    case 202:  // AUTH_FAIL
    case 204:  // HANDSHAKE_TIMEOUT
      return HomeError::WrongPassword;
    case 201:  // NO_AP_FOUND
    case 210:  // NO_AP_FOUND_W_COMPATIBLE_SECURITY
    case 211:  // NO_AP_FOUND_IN_AUTHMODE_THRESHOLD
    case 212:  // NO_AP_FOUND_IN_RSSI_THRESHOLD
      return HomeError::NotFound;
    default:
      return HomeError::Failed;
  }
}

}  // namespace keyra::net
