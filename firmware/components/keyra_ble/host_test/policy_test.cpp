// keyra_ble decisions: advertising mode, pairing gate, window, names, addresses,
// and the report map shared with USB.
#include <string>

#include "keyra_test.hpp"
#include "policy.hpp"
#include "report_map.hpp"
#include "usb_desc.hpp"

using namespace keyra::ble;

namespace {

void advertisingModes() {
  CHECK(advertising(false, true, 2, false) == Adv::Off);   // Bluetooth off wins
  CHECK(advertising(true, false, 0, false) == Adv::Off);   // nobody to reconnect, no window
  CHECK(advertising(true, false, 1, false) == Adv::BondedOnly);
  CHECK(advertising(true, true, 0, false) == Adv::Open);
  CHECK(advertising(true, true, 3, false) == Adv::Open);
  CHECK(advertising(true, true, 3, true) == Adv::Off);     // one host at a time
  CHECK(advertising(true, false, 2, true) == Adv::Off);
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
  pairingGate();
  window();
  names();
  addresses();
  reportMapMatchesUsb();
  return KEYRA_TEST_RESULT();
}
