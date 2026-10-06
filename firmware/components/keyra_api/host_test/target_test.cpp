// Target choice for type actions (SPEC §8.1): USB first in auto, then the
// most recently used Bluetooth host.
#include "keyra_test.hpp"
#include "target.hpp"

using namespace keyra::api;
using keyra::settings::Output;
using K = Target::Kind;

namespace {

const BtAddr kIpad = {0xA4, 0xC1, 0x38, 0x0B, 0x7F, 0x3A};
const BtAddr kMac = {0xF0, 0x2B, 0x7C, 0x41, 0x9A, 0xD3};
const std::vector<Bond> kBonds = {{kMac, 100}, {kIpad, 300}};

void autoPrefersUsb() {
  CHECK(pickTarget(Output::Auto, true, true, kBonds, kMac).kind == K::Usb);  // plugged in wins
  const Target t = pickTarget(Output::Auto, true, false, kBonds, std::nullopt);
  CHECK(t.kind == K::Ble && t.addr == kIpad);  // most recently used
  CHECK(pickTarget(Output::Auto, true, false, kBonds, kMac).addr == kMac);  // connected beats recent
  CHECK(pickTarget(Output::Auto, false, false, kBonds, std::nullopt).kind == K::None);  // Bluetooth off
  CHECK(pickTarget(Output::Auto, true, false, {}, std::nullopt).kind == K::None);
}

void forcedOutputs() {
  CHECK(pickTarget(Output::Usb, true, false, kBonds, kIpad).kind == K::Usb);  // even unplugged
  CHECK(pickTarget(Output::Ble, true, true, kBonds, std::nullopt).kind == K::Ble);  // even plugged in
  CHECK(pickTarget(Output::Ble, false, true, kBonds, std::nullopt).kind == K::None);
  CHECK(pickTarget(Output::Ble, true, true, {}, std::nullopt).kind == K::None);
  // Never-seen bonds still count (clock unknown): the first one is used.
  const Target t = pickTarget(Output::Ble, true, false, {{kMac, 0}, {kIpad, 0}}, std::nullopt);
  CHECK(t.kind == K::Ble && t.addr == kMac);
}

void parsing() {
  CHECK(parseTarget("usb")->kind == K::Usb);
  const auto t = parseTarget("a4:c1:38:0b:7f:3a");
  CHECK(t && t->kind == K::Ble && t->addr == kIpad);
  CHECK(!parseTarget("USB").has_value());
  CHECK(!parseTarget("").has_value());
  CHECK(!parseTarget("A4:C1:38:0B:7F").has_value());
  CHECK((Target{K::Usb, kIpad} == Target{K::Usb, kMac}));  // address only matters for Bluetooth
}

}  // namespace

int main() {
  autoPrefersUsb();
  forcedOutputs();
  parsing();
  return KEYRA_TEST_RESULT();
}
