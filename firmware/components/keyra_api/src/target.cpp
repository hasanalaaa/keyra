#include "target.hpp"

#include "keyra/ble.hpp"

namespace keyra::api {
namespace {

std::optional<BtAddr> mostRecent(const std::vector<Bond>& bonds, const std::optional<BtAddr>& linked) {
  const Bond* best = nullptr;
  for (const Bond& b : bonds) {
    if (linked && b.addr == *linked) return b.addr;  // already connected: no wait at all
    if (best == nullptr || b.lastSeen > best->lastSeen) best = &b;
  }
  if (best == nullptr) return std::nullopt;
  return best->addr;
}

}  // namespace

Target pickTarget(settings::Output out, bool bleEnabled, bool usbMounted, const std::vector<Bond>& bonds,
                  const std::optional<BtAddr>& linked) {
  Target t;
  if (out == settings::Output::Usb || (out == settings::Output::Auto && usbMounted)) {
    t.kind = Target::Kind::Usb;
    return t;
  }
  if (!bleEnabled) return t;
  if (const auto a = mostRecent(bonds, linked)) {
    t.kind = Target::Kind::Ble;
    t.addr = *a;
  }
  return t;
}

std::optional<Target> parseTarget(std::string_view s) {
  Target t;
  if (s == "usb") {
    t.kind = Target::Kind::Usb;
    return t;
  }
  if (!ble::parseAddr(s, t.addr)) return std::nullopt;
  t.kind = Target::Kind::Ble;
  return t;
}

}  // namespace keyra::api
