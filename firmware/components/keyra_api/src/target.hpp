#pragma once
// Which computer a type action goes to (SPEC §8.1). Pure, host-tested.
#include <array>
#include <cstdint>
#include <optional>
#include <string_view>
#include <vector>

#include "keyra/settings.hpp"

namespace keyra::api {

using BtAddr = std::array<uint8_t, 6>;  // Bluetooth identity address, as displayed

struct Target {
  enum class Kind : uint8_t { None, Usb, Ble };
  Kind kind = Kind::None;
  BtAddr addr{};  // Kind::Ble
  bool operator==(const Target& o) const { return kind == o.kind && (kind != Kind::Ble || addr == o.addr); }
};

struct Bond {
  BtAddr addr{};
  int64_t lastSeen = 0;  // unix seconds, 0 = unknown
};

// The host a new action types into when the request names none:
//   usb  → USB (even unplugged: the result is then no_usb)
//   ble  → the most recently used bonded host (the connected one first)
//   auto → USB when plugged in, otherwise as ble
// None when nothing could take it (the result is then no_host).
Target pickTarget(settings::Output out, bool bleEnabled, bool usbMounted, const std::vector<Bond>& bonds,
                  const std::optional<BtAddr>& linked);

// The request's `target`: "usb" or a bond address "XX:XX:XX:XX:XX:XX".
std::optional<Target> parseTarget(std::string_view s);

}  // namespace keyra::api
