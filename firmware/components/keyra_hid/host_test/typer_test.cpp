// Typing engine against a fake host: release always sent, abort on first
// failure, Caps Lock wrap, busy, not-mounted.
#include <functional>
#include <vector>

#include "check.hpp"
#include "keymap.hpp"
#include "typer.hpp"

using namespace keyra::hid;

namespace {

struct Report {
  uint8_t mod, key;
  bool operator==(const Report& o) const { return mod == o.mod && key == o.key; }
};
constexpr Report kRelease{0, 0};

class FakeHost : public Transport {
 public:
  bool mounted = true;
  bool caps = false;
  bool num = true;
  bool hostHonoursCaps = true;  // false: host ignores the Caps Lock tap
  bool hostHonoursNum = true;
  int failFromSend = -1;        // 0-based index of the first failing send; -1 = never
  std::vector<Report> sent;     // successfully delivered reports
  int attempts = 0;
  std::vector<uint32_t> delays;
  std::function<void()> onSend;

  bool ready() override { return mounted; }
  bool capsLock() override { return caps; }
  bool numLock() override { return num; }
  void delayMs(uint32_t ms) override { delays.push_back(ms); }
  bool send(uint8_t mod, uint8_t key) override {
    const int idx = attempts++;
    if (onSend) onSend();
    if (failFromSend >= 0 && idx >= failFromSend) return false;
    sent.push_back({mod, key});
    // Hosts toggle Caps Lock on key-down and report it back via the LED report.
    if (key == KEY_CAPS_LOCK && hostHonoursCaps) caps = !caps;
    if (key == KEY_NUM_LOCK && hostHonoursNum) num = !num;
    return true;
  }
  bool anyKeyHeldAtEnd() const { return !sent.empty() && !(sent.back() == kRelease); }
};

Options opt(uint16_t d = 12) {
  Options o;
  o.keyDelayMs = d;
  return o;
}

void testPlainTyping() {
  FakeHost h;
  Typer t;
  CHECK(t.type(h, "aB!", opt()) == Result::Ok);
  const std::vector<Report> want = {{0, 0x04}, kRelease, {MOD_LEFT_SHIFT, 0x05}, kRelease,
                                    {MOD_LEFT_SHIFT, 0x1E}, kRelease, kRelease};
  CHECK(h.sent == want);
  // press, hold keyDelay, release, gap keyDelay — for each char
  CHECK_EQ(h.delays.size(), size_t{6});
  for (uint32_t d : h.delays) CHECK_EQ(d, 12u);
}

Options optLayout(const char* id) {
  Options o = opt();
  CHECK(findLayout(id, o.layout));
  return o;
}

// AltGr on Windows (Right Alt), Option on a Mac (Left Alt), dead keys + Space.
void testLayoutModifiersAndDeadKeys() {
  {
    FakeHost h;
    Typer t;
    CHECK(t.type(h, "@", optLayout("de")) == Result::Ok);  // AltGr+Q
    CHECK(h.sent == (std::vector<Report>{{MOD_RIGHT_ALT, 0x14}, kRelease, kRelease}));
  }
  {
    FakeHost h;
    Typer t;
    CHECK(t.type(h, "@", optLayout("de-mac")) == Result::Ok);  // Option+L
    CHECK(h.sent == (std::vector<Report>{{MOD_LEFT_ALT, 0x0F}, kRelease, kRelease}));
  }
  {
    FakeHost h;
    Typer t;
    CHECK(t.type(h, "^", optLayout("de")) == Result::Ok);  // dead ^, then Space
    CHECK(h.sent == (std::vector<Report>{{0, 0x35}, kRelease, {0, 0x2C}, kRelease, kRelease}));
  }
  {
    FakeHost h;
    Typer t;
    CHECK(t.type(h, "\xD8\xB4", optLayout("ar")) == Result::Ok);  // Arabic sheen: the A key
    CHECK(h.sent == (std::vector<Report>{{0, 0x04}, kRelease, kRelease}));
  }
  {
    FakeHost h;
    Typer t;
    CHECK(t.type(h, "abc", optLayout("ar")) == Result::Unsupported);  // no Latin letters on Arabic
    CHECK(h.sent.empty());
  }
}

void testProbe() {
  FakeHost h;
  h.caps = true;
  Typer t;
  CHECK(t.probe(h, opt(1)) == Result::Ok);
  // Caps off, the probe keys (each released, only Shift), release, Caps back on.
  size_t keys = 0;
  for (const Report& r : h.sent) {
    CHECK(r.key != 0x28);                               // never Enter
    CHECK((r.mod & ~MOD_LEFT_SHIFT) == 0);              // no chords
    if (r.key != 0 && r.key != KEY_CAPS_LOCK) ++keys;
  }
  CHECK_EQ(keys, kProbeLen);
  CHECK(!h.anyKeyHeldAtEnd());
  CHECK(h.caps);
}

void testRepeatedCharIsReleasedBetween() {
  FakeHost h;
  Typer t;
  CHECK(t.type(h, "aa", opt(1)) == Result::Ok);
  const std::vector<Report> want = {{0, 0x04}, kRelease, {0, 0x04}, kRelease, kRelease};
  CHECK(h.sent == want);
}

void testEmptyText() {
  FakeHost h;
  Typer t;
  CHECK(t.type(h, "", opt()) == Result::Ok);
  CHECK(h.sent == std::vector<Report>{kRelease});
}

void testUnsupportedTouchesNothing() {
  FakeHost h;
  Typer t;
  CHECK(t.type(h, "abc\n", opt()) == Result::Unsupported);
  CHECK(t.type(h, "كلمة", opt()) == Result::Unsupported);
  CHECK(t.type(h, nullptr, opt()) == Result::Unsupported);
  CHECK_EQ(h.attempts, 0);
}

void testNotMounted() {
  FakeHost h;
  h.mounted = false;
  Typer t;
  CHECK(t.type(h, "abc", opt()) == Result::NotMounted);
  CHECK(t.tap(h, KEY_TAB, opt()) == Result::NotMounted);
  CHECK_EQ(h.attempts, 0);
}

void testAbortOnFirstFailureThenRelease() {
  // Fail exactly one send — each press and each in-stroke release of "abc"
  // in turn. Typing must stop there and a release must still go out.
  for (int failAt = 0; failAt < 6; ++failAt) {
    FakeHost h;
    struct OneShot : Transport {
      FakeHost& h;
      int failAt;
      OneShot(FakeHost& hh, int f) : h(hh), failAt(f) {}
      bool ready() override { return true; }
      bool capsLock() override { return false; }
      void delayMs(uint32_t ms) override { h.delayMs(ms); }
      bool send(uint8_t m, uint8_t k) override {
        if (h.attempts == failAt) {
          ++h.attempts;
          return false;
        }
        return h.send(m, k);
      }
    } tr(h, failAt);
    Typer t;
    CHECK(t.type(tr, "abc", opt()) == Result::Failed);
    CHECK(!h.anyKeyHeldAtEnd());
    CHECK(!h.sent.empty() && h.sent.back() == kRelease);
    // Nothing typed after the failure point other than releases.
    for (size_t i = static_cast<size_t>(failAt); i < h.sent.size(); ++i) CHECK(h.sent[i] == kRelease);
  }
}

void testReleaseRetriedThreeTimes() {
  FakeHost h;
  h.failFromSend = 1;  // press 'a' succeeds, everything after fails
  Typer t;
  CHECK(t.type(h, "ab", opt()) == Result::Failed);
  // 1 press + 1 failed release (in the stroke) + 3 release-all attempts.
  CHECK_EQ(h.attempts, 1 + 1 + Typer::kReleaseAttempts);
}

void testReleaseRecoversOnRetry() {
  FakeHost h;
  struct Flaky : Transport {
    FakeHost& h;
    explicit Flaky(FakeHost& hh) : h(hh) {}
    bool ready() override { return true; }
    bool capsLock() override { return false; }
    void delayMs(uint32_t) override {}
    bool send(uint8_t m, uint8_t k) override {
      // attempts: 0 press a, 1 release a FAILS, 2 release-all FAILS, 3 release-all ok
      const int i = h.attempts;
      if (i == 1 || i == 2) {
        ++h.attempts;
        return false;
      }
      return h.send(m, k);
    }
  } tr(h);
  Typer t;
  CHECK(t.type(tr, "ab", opt()) == Result::Failed);  // aborted mid-text even though release recovered
  CHECK(!h.anyKeyHeldAtEnd());
  CHECK_EQ(h.attempts, 4);
}

void testCapsLockWrap() {
  FakeHost h;
  h.caps = true;
  Typer t;
  CHECK(t.type(h, "aB", opt(5)) == Result::Ok);
  const std::vector<Report> want = {{0, KEY_CAPS_LOCK}, kRelease,                         // caps off
                                    {0, 0x04},          kRelease, {MOD_LEFT_SHIFT, 0x05}, kRelease,
                                    kRelease,                                             // release-all
                                    {0, KEY_CAPS_LOCK}, kRelease,                         // caps back on
                                    kRelease};
  CHECK(h.sent == want);
  CHECK(h.caps);  // user's state restored
  // Caps Lock is held long enough for macOS to accept it.
  CHECK_EQ(h.delays.front(), Typer::kCapsHoldMs);
}

void testCapsIgnoredByHostAborts() {
  FakeHost h;
  h.caps = true;
  h.hostHonoursCaps = false;
  Typer t;
  CHECK(t.type(h, "abc", opt()) == Result::Failed);
  // Only the caps tap and releases: no letters with inverted case, no restore tap.
  int capsTaps = 0;
  for (const auto& r : h.sent) {
    CHECK(r.key == 0 || r.key == KEY_CAPS_LOCK);
    if (r.key == KEY_CAPS_LOCK) ++capsTaps;
  }
  CHECK_EQ(capsTaps, 1);
  CHECK(h.sent.back() == kRelease);
  CHECK(h.caps);
}

void testCapsRestoredAfterMidTextFailure() {
  FakeHost h;
  h.caps = true;
  struct FailB : Transport {
    FakeHost& h;
    explicit FailB(FakeHost& hh) : h(hh) {}
    bool ready() override { return true; }
    bool capsLock() override { return h.caps; }
    void delayMs(uint32_t) override {}
    bool send(uint8_t m, uint8_t k) override {
      if (k == 0x05) {  // 'b'
        ++h.attempts;
        return false;
      }
      return h.send(m, k);
    }
  } tr(h);
  Typer t;
  CHECK(t.type(tr, "abc", opt()) == Result::Failed);
  CHECK(h.caps);  // turned off for typing, back on after the abort
  CHECK(h.sent.back() == kRelease);
  for (const auto& r : h.sent) CHECK(r.key != 0x06);  // 'c' never typed
}

void testCapsOffNoWrap() {
  FakeHost h;
  Typer t;
  CHECK(t.type(h, "a", opt()) == Result::Ok);
  for (const auto& r : h.sent) CHECK(r.key != KEY_CAPS_LOCK);
}

void testTapKey() {
  FakeHost h;
  h.caps = true;  // taps never touch Caps Lock
  Typer t;
  CHECK(t.tap(h, KEY_TAB, opt()) == Result::Ok);
  CHECK(t.tap(h, KEY_ENTER, opt()) == Result::Ok);
  const std::vector<Report> want = {{0, 0x2B}, kRelease, kRelease, {0, 0x28}, kRelease, kRelease};
  CHECK(h.sent == want);
  CHECK(t.tap(h, 0, opt()) == Result::Unsupported);
  CHECK(t.tap(h, 0xE1, opt()) == Result::Unsupported);  // modifier, not a key
}

void testBusy() {
  FakeHost h;
  Typer t;
  Result inner = Result::Ok;
  bool once = false;
  h.onSend = [&] {
    if (!once) {
      once = true;
      inner = t.type(h, "x", opt());
    }
  };
  CHECK(t.type(h, "ab", opt()) == Result::Ok);
  CHECK(inner == Result::Busy);
  // After finishing, the engine is free again.
  h.onSend = nullptr;
  CHECK(t.tap(h, KEY_ENTER, opt()) == Result::Ok);
}

// A BLE host: keystrokes arrive as notifications and its LED report comes
// back one connection event later, not instantly as over USB.
class FakeBleHost : public FakeHost {
 public:
  uint32_t ledLatencyMs = 45;  // a 30 ms connection interval plus processing
  uint32_t sinceToggleMs = 0;
  bool pending = false;
  bool capsLock() override { return caps; }
  void delayMs(uint32_t ms) override {
    FakeHost::delayMs(ms);
    if (pending && (sinceToggleMs += ms) >= ledLatencyMs) {
      caps = !caps;
      pending = false;
    }
  }
  bool send(uint8_t mod, uint8_t key) override {
    const bool wasCaps = caps;
    const bool honours = hostHonoursCaps;
    hostHonoursCaps = false;  // toggle later, from delayMs()
    const bool ok = FakeHost::send(mod, key);
    hostHonoursCaps = honours;
    caps = wasCaps;
    if (ok && key == KEY_CAPS_LOCK && honours) {
      pending = true;
      sinceToggleMs = 0;
    }
    return ok;
  }
};

void testBleTypesOnlyToBle() {
  FakeHost usb;
  FakeBleHost ble;
  Typer t;
  CHECK(t.type(ble, "aB", opt()) == Result::Ok);
  CHECK(usb.attempts == 0);
  const std::vector<Report> want = {{0, 0x04}, kRelease, {MOD_LEFT_SHIFT, 0x05}, kRelease, kRelease};
  CHECK(ble.sent == want);
}

void testBleCapsWrapWithLatency() {
  FakeBleHost ble;
  ble.caps = true;
  Typer t;
  CHECK(t.type(ble, "ab", opt(5)) == Result::Ok);
  // Caps off, letters lowercase, everything released, Caps back on.
  CHECK(ble.sent.front() == (Report{0, KEY_CAPS_LOCK}));
  for (const auto& r : ble.sent) CHECK(r.mod == 0);
  CHECK(ble.sent.back() == kRelease);
  // The restore tap is sent; its LED echo lands after one more interval.
  ble.delayMs(ble.ledLatencyMs);
  CHECK(ble.caps);
}

void testBleSlowLedReportAborts() {
  FakeBleHost ble;
  ble.caps = true;
  // The host confirms only after the hold, the key delay and the whole settle window.
  ble.ledLatencyMs = Typer::kCapsHoldMs + 12 + Typer::kCapsSettleMs + 100;
  Typer t;
  CHECK(t.type(ble, "abc", opt()) == Result::Failed);
  for (const auto& r : ble.sent) CHECK(r.key == 0 || r.key == KEY_CAPS_LOCK);  // no inverted letters
  CHECK(ble.sent.back() == kRelease);
}

void testBleLinkLostMidText() {
  FakeBleHost ble;
  ble.failFromSend = 3;  // link drops after 'a' and the press of 'b'
  Typer t;
  CHECK(t.type(ble, "abc", opt()) == Result::Failed);
  for (const auto& r : ble.sent) CHECK(r.key != 0x06);
}

void testBusySpansTransports() {
  FakeHost usb;
  FakeBleHost ble;
  Typer t;
  Result inner = Result::Ok;
  bool once = false;
  usb.onSend = [&] {
    if (once) return;
    once = true;
    inner = t.type(ble, "x", opt());
  };
  CHECK(t.type(usb, "ab", opt()) == Result::Ok);
  CHECK(inner == Result::Busy);
  CHECK(ble.attempts == 0);
  CHECK(t.type(ble, "x", opt()) == Result::Ok);
}

Options altOpt() {
  Options o = opt();
  o.altCodes = true;
  return o;
}

// "A" (65) and "~" (126): Alt down, keypad digits under Alt, Alt up.
void testAltCodes() {
  FakeHost h;
  Typer t;
  CHECK(t.type(h, "A~", altOpt()) == Result::Ok);
  const std::vector<Report> want = {
      {MOD_LEFT_ALT, 0}, {MOD_LEFT_ALT, KEY_KP_1 + 5}, {MOD_LEFT_ALT, 0}, {MOD_LEFT_ALT, KEY_KP_1 + 4},
      {MOD_LEFT_ALT, 0}, kRelease,
      {MOD_LEFT_ALT, 0}, {MOD_LEFT_ALT, KEY_KP_1}, {MOD_LEFT_ALT, 0}, {MOD_LEFT_ALT, KEY_KP_1 + 1},
      {MOD_LEFT_ALT, 0}, {MOD_LEFT_ALT, KEY_KP_1 + 5}, {MOD_LEFT_ALT, 0}, kRelease,
      kRelease};
  CHECK(h.sent == want);
}

void testAltCodeZeroDigit() {
  FakeHost h;
  Typer t;
  CHECK(t.type(h, "d", altOpt()) == Result::Ok);  // 100
  const std::vector<Report> want = {{MOD_LEFT_ALT, 0}, {MOD_LEFT_ALT, KEY_KP_1}, {MOD_LEFT_ALT, 0},
                                    {MOD_LEFT_ALT, KEY_KP_0}, {MOD_LEFT_ALT, 0}, {MOD_LEFT_ALT, KEY_KP_0},
                                    {MOD_LEFT_ALT, 0}, kRelease, kRelease};
  CHECK(h.sent == want);
}

// Num Lock off: turned on first (keypad digits would move the cursor), back off after.
void testAltCodesNumLockWrap() {
  FakeHost h;
  h.num = false;
  Typer t;
  CHECK(t.type(h, "1", altOpt()) == Result::Ok);
  CHECK(h.sent.front() == (Report{0, KEY_NUM_LOCK}));
  CHECK(h.sent[h.sent.size() - 3] == (Report{0, KEY_NUM_LOCK}));  // restore tap, its release, final release
  CHECK(!h.num);
  CHECK(h.sent.back() == kRelease);
}

void testAltCodesNumLockIgnoredAborts() {
  FakeHost h;
  h.num = false;
  h.hostHonoursNum = false;
  Typer t;
  CHECK(t.type(h, "abc", altOpt()) == Result::Failed);
  for (const auto& r : h.sent) CHECK(r.mod == 0 && (r.key == 0 || r.key == KEY_NUM_LOCK));
}

// Plain typing never touches Num Lock.
void testNumLockOnlyForAltCodes() {
  FakeHost h;
  h.num = false;
  Typer t;
  CHECK(t.type(h, "a", opt()) == Result::Ok);
  for (const auto& r : h.sent) CHECK(r.key != KEY_NUM_LOCK);
}

void testAltCodesTypeableAnyLayout() {
  Options o = altOpt();
  CHECK(typeable(std::string_view("Pa$$w0rd!"), o));
  CHECK(!typeable(std::string_view("tab\there"), o));
}

void testChord() {
  FakeHost h;
  Typer t;
  CHECK(t.chord(h, MOD_LEFT_CTRL, KEY_SPACE, opt()) == Result::Ok);
  const std::vector<Report> want = {{MOD_LEFT_CTRL, 0}, {MOD_LEFT_CTRL, KEY_SPACE}, kRelease, kRelease};
  CHECK(h.sent == want);
  CHECK(h.delays.back() == Typer::kChordSettleMs);
  CHECK(t.chord(h, 0, KEY_SPACE, opt()) == Result::Unsupported);
}

}  // namespace

int main() {
  testPlainTyping();
  testRepeatedCharIsReleasedBetween();
  testEmptyText();
  testUnsupportedTouchesNothing();
  testLayoutModifiersAndDeadKeys();
  testProbe();
  testNotMounted();
  testAbortOnFirstFailureThenRelease();
  testReleaseRetriedThreeTimes();
  testReleaseRecoversOnRetry();
  testCapsLockWrap();
  testCapsIgnoredByHostAborts();
  testCapsRestoredAfterMidTextFailure();
  testCapsOffNoWrap();
  testTapKey();
  testBusy();
  testBleTypesOnlyToBle();
  testBleCapsWrapWithLatency();
  testBleSlowLedReportAborts();
  testBleLinkLostMidText();
  testBusySpansTransports();
  testAltCodes();
  testAltCodeZeroDigit();
  testAltCodesNumLockWrap();
  testAltCodesNumLockIgnoredAborts();
  testNumLockOnlyForAltCodes();
  testAltCodesTypeableAnyLayout();
  testChord();
  TEST_MAIN_END();
}
