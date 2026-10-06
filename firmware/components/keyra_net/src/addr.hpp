#pragma once
// Which interface a packet or connection belongs to (SPEC §8.2). Pure; IPv4
// addresses are host byte order here, the glue converts with ntohl().
#include <cstdint>
#include <string>

#include "keyra/net_types.hpp"

namespace keyra::net {

constexpr uint32_t ipv4(uint8_t a, uint8_t b, uint8_t c, uint8_t d) {
  return (uint32_t{a} << 24) | (uint32_t{b} << 16) | (uint32_t{c} << 8) | d;
}
// ESP-IDF's default SoftAP netif: 192.168.4.1/24.
constexpr uint32_t kApIp = ipv4(192, 168, 4, 1);
constexpr uint32_t kApMask = ipv4(255, 255, 255, 0);

// The IPv4 address inside an IPv4-mapped IPv6 address (::ffff:a.b.c.d), else 0.
// httpd listens on a dual-stack IPv6 socket, so IPv4 clients look like this.
uint32_t fromV4Mapped(const uint8_t addr[16]);

// A connection whose local address is the AP's arrived over Keyra's own Wi-Fi;
// everything else (the home IP, unknown, 0) is treated as the home network,
// which is the side with more checks.
Via classify(uint32_t localIp);

// The captive DNS answers only clients on the AP subnet.
bool inApSubnet(uint32_t ip);

std::string toString(uint32_t ip);

}  // namespace keyra::net
