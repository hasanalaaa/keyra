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
  bool hostHonoursCaps = true;  // false: host ignores the Caps Lock tap
  int failFromSend = -1;        // 0-based index of the first failing send; -1 = never
  std::vector<Report> sent;     // successfully delivered reports
  int attempts = 0;
  std::vector<uint32_t> delays;
  std::function<void()> onSend;

  bool ready() override { return mounted; }
  bool capsLock() override { return caps; }
  void delayMs(uint32_t ms) override { delays.push_back(ms); }
  bool send(uint8_t mod, uint8_t key) override {
    const int idx = attempts++;
    if (onSend) onSend();
    if (failFromSend >= 0 && idx >= failFromSend) return false;
    sent.push_back({mod, key});
    // Hosts toggle Caps Lock on key-down and report it back via the LED report.
    if (key == KEY_CAPS_LOCK && hostHonoursCaps) caps = !caps;
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
  Typer t(h);
  CHECK(t.type("aB!", opt()) == Result::Ok);
  const std::vector<Report> want = {{0, 0x04}, kRelease, {MOD_LEFT_SHIFT, 0x05}, kRelease,
                                    {MOD_LEFT_SHIFT, 0x1E}, kRelease, kRelease};
  CHECK(h.sent == want);
  // press, hold keyDelay, release, gap keyDelay — for each char
  CHECK_EQ(h.delays.size(), size_t{6});
  for (uint32_t d : h.delays) CHECK_EQ(d, 12u);
}

void testRepeatedCharIsReleasedBetween() {
  FakeHost h;
  Typer t(h);
  CHECK(t.type("aa", opt(1)) == Result::Ok);
  const std::vector<Report> want = {{0, 0x04}, kRelease, {0, 0x04}, kRelease, kRelease};
  CHECK(h.sent == want);
}

void testEmptyText() {
  FakeHost h;
  Typer t(h);
  CHECK(t.type("", opt()) == Result::Ok);
  CHECK(h.sent == std::vector<Report>{kRelease});
}

void testUnsupportedTouchesNothing() {
  FakeHost h;
  Typer t(h);
  CHECK(t.type("abc\n", opt()) == Result::Unsupported);
  CHECK(t.type("كلمة", opt()) == Result::Unsupported);
  CHECK(t.type(nullptr, opt()) == Result::Unsupported);
  CHECK_EQ(h.attempts, 0);
}

void testNotMounted() {
  FakeHost h;
  h.mounted = false;
  Typer t(h);
  CHECK(t.type("abc", opt()) == Result::NotMounted);
  CHECK(t.tap(KEY_TAB, opt()) == Result::NotMounted);
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
    Typer t(tr);
    CHECK(t.type("abc", opt()) == Result::Failed);
    CHECK(!h.anyKeyHeldAtEnd());
    CHECK(!h.sent.empty() && h.sent.back() == kRelease);
    // Nothing typed after the failure point other than releases.
    for (size_t i = static_cast<size_t>(failAt); i < h.sent.size(); ++i) CHECK(h.sent[i] == kRelease);
  }
}

void testReleaseRetriedThreeTimes() {
  FakeHost h;
  h.failFromSend = 1;  // press 'a' succeeds, everything after fails
  Typer t(h);
  CHECK(t.type("ab", opt()) == Result::Failed);
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
  Typer t(tr);
  CHECK(t.type("ab", opt()) == Result::Failed);  // aborted mid-text even though release recovered
  CHECK(!h.anyKeyHeldAtEnd());
  CHECK_EQ(h.attempts, 4);
}

void testCapsLockWrap() {
  FakeHost h;
  h.caps = true;
  Typer t(h);
  CHECK(t.type("aB", opt(5)) == Result::Ok);
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
  Typer t(h);
  CHECK(t.type("abc", opt()) == Result::Failed);
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
  Typer t(tr);
  CHECK(t.type("abc", opt()) == Result::Failed);
  CHECK(h.caps);  // turned off for typing, back on after the abort
  CHECK(h.sent.back() == kRelease);
  for (const auto& r : h.sent) CHECK(r.key != 0x06);  // 'c' never typed
}

void testCapsOffNoWrap() {
  FakeHost h;
  Typer t(h);
  CHECK(t.type("a", opt()) == Result::Ok);
  for (const auto& r : h.sent) CHECK(r.key != KEY_CAPS_LOCK);
}

void testTapKey() {
  FakeHost h;
  h.caps = true;  // taps never touch Caps Lock
  Typer t(h);
  CHECK(t.tap(KEY_TAB, opt()) == Result::Ok);
  CHECK(t.tap(KEY_ENTER, opt()) == Result::Ok);
  const std::vector<Report> want = {{0, 0x2B}, kRelease, kRelease, {0, 0x28}, kRelease, kRelease};
  CHECK(h.sent == want);
  CHECK(t.tap(0, opt()) == Result::Unsupported);
  CHECK(t.tap(0xE1, opt()) == Result::Unsupported);  // modifier, not a key
}

void testBusy() {
  FakeHost h;
  Typer t(h);
  Result inner = Result::Ok;
  bool once = false;
  h.onSend = [&] {
    if (!once) {
      once = true;
      inner = t.type("x", opt());
    }
  };
  CHECK(t.type("ab", opt()) == Result::Ok);
  CHECK(inner == Result::Busy);
  // After finishing, the engine is free again.
  h.onSend = nullptr;
  CHECK(t.tap(KEY_ENTER, opt()) == Result::Ok);
}

}  // namespace

int main() {
  testPlainTyping();
  testRepeatedCharIsReleasedBetween();
  testEmptyText();
  testUnsupportedTouchesNothing();
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
  TEST_MAIN_END();
}
