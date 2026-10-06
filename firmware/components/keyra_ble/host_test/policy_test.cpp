// keyra_ble decisions: advertising mode, pairing gate, window, names, addresses,
// and the report map shared with USB.
#include <initializer_list>
#include <string>

#include "keyra_test.hpp"
#include "policy.hpp"
#include "report_map.hpp"
#include "usb_desc.hpp"

using namespace keyra::ble;

namespace {

constexpr auto kAlways = Connect::Always;
constexpr auto kOnDemand = Connect::OnDemand;
const Addr kIpad = {0xA4, 0xC1, 0x38, 0x0B, 0x7F, 0x3A};
const Addr kMac = {0xF0, 0x2B, 0x7C, 0x41, 0x9A, 0xD3};

void advertisingModes() {
  for (Connect m : {kAlways, kOnDemand}) {
    CHECK(advertising(false, true, 2, false, m, true) == Adv::Off);  // Bluetooth off wins
    CHECK(advertising(true, true, 0, false, m, false) == Adv::Open);
    CHECK(advertising(true, true, 3, false, m, true) == Adv::Open);  // the window is open to all
    CHECK(advertising(true, true, 3, true, m, false) == Adv::Off);   // one host at a time
    CHECK(advertising(true, false, 2, true, m, true) == Adv::Off);
    CHECK(advertising(true, false, 1, false, m, true) == Adv::BondedOnly);  // an action waits for a host
    CHECK(advertising(true, false, 0, false, m, false) == Adv::Off);
  }
  // Idle with bonds: Always lets them reconnect, OnDemand stays silent.
  CHECK(advertising(true, false, 2, false, kAlways, false) == Adv::BondedOnly);
  CHECK(advertising(true, false, 2, false, kOnDemand, false) == Adv::Off);
}

void linkKeeping() {
  // Untrusted links live only inside the pairing window.
  CHECK(keepLink(kOnDemand, true, false, std::nullopt, kIpad));
  CHECK(!keepLink(kAlways, false, false, std::nullopt, kIpad));
  // A waiting action wants one host: any other bonded host makes room.
  CHECK(keepLink(kOnDemand, false, true, kIpad, kIpad));
  CHECK(!keepLink(kAlways, false, true, kIpad, kMac));
  // Idle: OnDemand lets go, Always holds on.
  CHECK(!keepLink(kOnDemand, false, true, std::nullopt, kIpad));
  CHECK(keepLink(kAlways, false, true, std::nullopt, kIpad));
}

// The on-demand life of one action: arm → advertise → connect → press → type
// → linger → disconnect, and the cancel / expiry paths.
void onDemandLifecycle() {
  Demand d;
  int64_t now = 1000;
  CHECK(!d.target(now));
  CHECK(advertising(true, false, 2, false, kOnDemand, d.target(now).has_value()) == Adv::Off);

  d.want(kIpad);  // armed for the iPad
  CHECK(d.target(now) == kIpad);
  CHECK(advertising(true, false, 2, false, kOnDemand, true) == Adv::BondedOnly);
  // The Mac (also bonded) is not wanted: if it were connected it would be dropped.
  CHECK(!keepLink(kOnDemand, false, true, d.target(now), kMac));
  // The iPad connects: advertising stops, the link stays for the press.
  CHECK(advertising(true, false, 2, true, kOnDemand, true) == Adv::Off);
  CHECK(keepLink(kOnDemand, false, true, d.target(now), kIpad));

  now += 4000;
  d.done(now);  // typed
  CHECK(d.target(now + Demand::kLingerMs - 1) == kIpad);  // a quick second action reuses the link
  CHECK_EQ(d.lingerLeftMs(now), Demand::kLingerMs);
  CHECK(keepLink(kOnDemand, false, true, d.target(now + 5000), kIpad));
  CHECK(!d.target(now + Demand::kLingerMs));  // then Keyra lets go
  CHECK(!keepLink(kOnDemand, false, true, d.target(now + Demand::kLingerMs), kIpad));

  // A second action inside the linger keeps the same link.
  d.want(kIpad);
  CHECK(d.target(now + 10 * Demand::kLingerMs) == kIpad);  // armed: no time limit here
  // Cancelled or expired: no linger, the radio goes quiet at once.
  d.drop();
  CHECK(!d.target(now));
  CHECK_EQ(d.lingerLeftMs(now), 0);
  d.done(now);  // a late "done" after a drop does not resurrect it
  CHECK(!d.target(now));
}

void pairingGate() {
  CHECK(!mayPair(false, false, 0));  // no window, no pairing
  CHECK(!mayPair(false, true, 1));   // not even re-pairing a known host
  CHECK(mayPair(true, false, 0));
  CHECK(mayPair(true, false, kMaxBonds - 1));
  CHECK(!mayPair(true, false, kMaxBonds));  // full: the user forgets one first
  CHECK(mayPair(true, true, kMaxBonds));    // a known host replaces its own bond

  CHECK(!mayConnect(false, true, true));
  CHECK(!mayConnect(true, false, false));   // stranger outside the window
  CHECK(mayConnect(true, false, true));
  CHECK(mayConnect(true, true, false));
}

void window() {
  Window w;
  CHECK(!w.active(0));
  CHECK_EQ(w.leftMs(0), 0);
  w.open(1000);
  CHECK(w.active(1000));
  CHECK_EQ(w.leftMs(1000), int64_t{kPairingWindowMs});
  CHECK(w.active(1000 + kPairingWindowMs - 1));
  CHECK_EQ(w.leftMs(1000 + kPairingWindowMs - 1), 1);
  CHECK(!w.active(1000 + kPairingWindowMs));
  CHECK_EQ(w.leftMs(1000 + kPairingWindowMs + 5), 0);
  w.open(5000);
  w.close();
  CHECK(!w.active(5001));
}

void names() {
  CHECK(fitName("Keyra", 18) == "Keyra");
  CHECK(fitName("abcdefghijklmnopqrstuvwxyz", 18) == "abcdefghijklmnopqr");
  // "مفتاح حسن" — two-byte letters must not be split.
  const std::string ar = "\xD9\x85\xD9\x81\xD8\xAA\xD8\xA7\xD8\xAD";
  CHECK(fitName(ar, 3) == "\xD9\x85");
  CHECK(fitName(ar, 4) == "\xD9\x85\xD9\x81");
  CHECK(fitName(ar, 1).empty());
  CHECK(fitName("\xF0\x9F\x94\x91key", 3).empty());  // 🔑 is four bytes

  CHECK(cleanName("Hasan's iPad", 32) == "Hasan's iPad");
  CHECK(cleanName("  MacBook\n Pro\x01  ", 32) == "MacBook Pro");
  CHECK(cleanName("bad\xFF\xFEname", 32) == "badname");
  CHECK(cleanName("\xC0\xAF", 32).empty());       // overlong '/'
  CHECK(cleanName("\xED\xA0\x80x", 32) == "x");   // surrogate
  CHECK(cleanName(ar, 4) == "\xD9\x85\xD9\x81");
  CHECK(cleanName(std::string("a\0b", 3), 32) == "ab");
  CHECK(cleanName("   ", 32).empty());
}

void addresses() {
  const Addr a = {0xA4, 0xC1, 0x38, 0x0B, 0x7F, 0x3A};
  CHECK(formatAddr(a) == "A4:C1:38:0B:7F:3A");
  Addr b{};
  CHECK(parseAddr("a4:c1:38:0b:7f:3a", b) && b == a);
  CHECK(parseAddr("A4:C1:38:0B:7F:3A", b) && b == a);
  CHECK(!parseAddr("A4C1380B7F3A", b));
  CHECK(!parseAddr("A4:C1:38:0B:7F", b));
  CHECK(!parseAddr("A4:C1:38:0B:7F:3G", b));
  CHECK(!parseAddr("A4-C1-38-0B-7F-3A", b));
  CHECK(!parseAddr("A4:C1:38:0B:7F:3A:", b));
}

// Same keyboard on both transports: the GATT report map is the USB one.
void reportMapMatchesUsb() {
  CHECK(kReportMap == keyra::hid::desc::kHidReport);
  CHECK_EQ(kInputReportLen, keyra::hid::desc::kHidReportLen);
  CHECK_EQ(kLedCapsLockBit, keyra::hid::desc::kLedCapsLockBit);
}

}  // namespace

int main() {
  advertisingModes();
  linkKeeping();
  onDemandLifecycle();
  pairingGate();
  window();
  names();
  addresses();
  reportMapMatchesUsb();
  return KEYRA_TEST_RESULT();
}
