#pragma once
// Output routing rule of SPEC §8.1 (pure, host-tested).
#include "keyra/hid.hpp"

namespace keyra::hid {

constexpr Host route(Output o, bool usbReady, bool bleReady) {
  switch (o) {
    case Output::Usb: return usbReady ? Host::Usb : Host::None;
    case Output::Ble: return bleReady ? Host::Ble : Host::None;
    case Output::Auto: break;
  }
  // USB wins when both are up: plugging Keyra in is the most explicit choice.
  if (usbReady) return Host::Usb;
  return bleReady ? Host::Ble : Host::None;
}

}  // namespace keyra::hid
