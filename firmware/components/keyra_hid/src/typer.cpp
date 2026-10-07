#include "typer.hpp"

#include "keymap.hpp"

namespace keyra::hid {

Result Typer::type(Transport& t, const char* text, const Options& opt) {
  if (!typeable(text, opt.layout)) return Result::Unsupported;
  return run(t, Job::Text, text, 0, opt);
}

Result Typer::tap(Transport& t, uint8_t keycode, const Options& opt) {
  // 0 is "no event" and >= 0xE0 are modifiers; neither is a tappable key.
  if (keycode == 0 || keycode >= 0xE0) return Result::Unsupported;
  return run(t, Job::Tap, nullptr, keycode, opt);
}

Result Typer::probe(Transport& t, const Options& opt) { return run(t, Job::Probe, nullptr, 0, opt); }

bool Typer::stroke(Transport& t, uint8_t modifier, uint8_t keycode, uint32_t holdMs, uint32_t gapMs) {
  if (!t.send(modifier, keycode)) return false;
  t.delayMs(holdMs);
  if (!t.send(0, 0)) return false;
  t.delayMs(gapMs);
  return true;
}

bool Typer::releaseAll(Transport& t) {
  for (int i = 0; i < kReleaseAttempts; ++i) {
    if (t.send(0, 0)) return true;
  }
  return false;
}

Result Typer::run(Transport& t, Job job, const char* text, uint8_t tapKeycode, const Options& opt) {
  bool expected = false;
  if (!busy_.compare_exchange_strong(expected, true)) return Result::Busy;
  struct Unbusy {
    std::atomic<bool>& b;
    ~Unbusy() { b.store(false); }
  } unbusy{busy_};

  if (!t.ready()) return Result::NotMounted;

  const uint32_t delay = opt.keyDelayMs;
  bool ok = true;
  bool capsTurnedOff = false;

  const bool printing = job == Job::Probe || (job == Job::Text && *text != '\0');
  if (printing && t.capsLock()) {
    if (t.send(0, KEY_CAPS_LOCK)) {
      capsTurnedOff = true;  // hosts toggle on key-down: restore from here on
      t.delayMs(kCapsHoldMs);
      ok = t.send(0, 0);
      if (ok) {
        t.delayMs(delay);
        uint32_t waited = 0;
        while (t.capsLock() && waited < kCapsSettleMs) {
          t.delayMs(kCapsPollMs);
          waited += kCapsPollMs;
        }
        if (t.capsLock()) {
          // Host ignored the tap (or never reports LEDs): its state is
          // unchanged, so there is nothing to restore — and typing now would
          // invert letter case.
          capsTurnedOff = false;
          ok = false;
        }
      }
    } else {
      ok = false;
    }
  }

  if (ok) {
    if (job == Job::Text) {
      for (const char* p = text; ok && *p != '\0';) {
        uint32_t cp = 0;
        KeyStroke ks[kMaxStrokes];
        nextCodePoint(p, cp);  // cannot fail: typeable() was checked
        const int n = strokesFor(opt.layout, cp, ks);
        for (int i = 0; ok && i < n; ++i) ok = stroke(t, ks[i].modifier, ks[i].keycode, delay, delay);
      }
    } else if (job == Job::Probe) {
      for (size_t i = 0; ok && i < kProbeLen; ++i)
        ok = stroke(t, kProbe[i].shift ? MOD_LEFT_SHIFT : 0, kProbe[i].keycode, delay, delay);
    } else {
      ok = stroke(t, 0, tapKeycode, delay, delay);
    }
  }

  // Every exit after we own the bus: nothing may stay pressed, then put the
  // user's Caps Lock back exactly as we found it.
  if (!releaseAll(t)) ok = false;
  if (capsTurnedOff) {
    if (!stroke(t, 0, KEY_CAPS_LOCK, kCapsHoldMs, delay)) ok = false;
    if (!releaseAll(t)) ok = false;
  }
  return ok ? Result::Ok : Result::Failed;
}

}  // namespace keyra::hid
