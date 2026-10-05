#include "typer.hpp"

#include "keymap.hpp"

namespace keyra::hid {

Result Typer::type(const char* text, const Options& opt) {
  if (!typeable(text)) return Result::Unsupported;
  return run(text, 0, opt);
}

Result Typer::tap(uint8_t keycode, const Options& opt) {
  // 0 is "no event" and >= 0xE0 are modifiers; neither is a tappable key.
  if (keycode == 0 || keycode >= 0xE0) return Result::Unsupported;
  return run(nullptr, keycode, opt);
}

bool Typer::stroke(uint8_t modifier, uint8_t keycode, uint32_t holdMs, uint32_t gapMs) {
  if (!t_.send(modifier, keycode)) return false;
  t_.delayMs(holdMs);
  if (!t_.send(0, 0)) return false;
  t_.delayMs(gapMs);
  return true;
}

bool Typer::releaseAll() {
  for (int i = 0; i < kReleaseAttempts; ++i) {
    if (t_.send(0, 0)) return true;
  }
  return false;
}

Result Typer::run(const char* text, uint8_t tapKeycode, const Options& opt) {
  bool expected = false;
  if (!busy_.compare_exchange_strong(expected, true)) return Result::Busy;
  struct Unbusy {
    std::atomic<bool>& b;
    ~Unbusy() { b.store(false); }
  } unbusy{busy_};

  if (!t_.ready()) return Result::NotMounted;

  const uint32_t delay = opt.keyDelayMs;
  bool ok = true;
  bool capsTurnedOff = false;

  if (text != nullptr && *text != '\0' && t_.capsLock()) {
    if (t_.send(0, KEY_CAPS_LOCK)) {
      capsTurnedOff = true;  // hosts toggle on key-down: restore from here on
      t_.delayMs(kCapsHoldMs);
      ok = t_.send(0, 0);
      if (ok) {
        t_.delayMs(delay);
        uint32_t waited = 0;
        while (t_.capsLock() && waited < kCapsSettleMs) {
          t_.delayMs(kCapsPollMs);
          waited += kCapsPollMs;
        }
        if (t_.capsLock()) {
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
    if (text != nullptr) {
      KeyStroke ks{};
      for (const char* p = text; ok && *p != '\0'; ++p) {
        keystrokeFor(*p, ks);  // cannot fail: typeable() was checked
        ok = stroke(ks.shift ? MOD_LEFT_SHIFT : 0, ks.keycode, delay, delay);
      }
    } else {
      ok = stroke(0, tapKeycode, delay, delay);
    }
  }

  // Every exit after we own the bus: nothing may stay pressed, then put the
  // user's Caps Lock back exactly as we found it.
  if (!releaseAll()) ok = false;
  if (capsTurnedOff) {
    if (!stroke(0, KEY_CAPS_LOCK, kCapsHoldMs, delay)) ok = false;
    if (!releaseAll()) ok = false;
  }
  return ok ? Result::Ok : Result::Failed;
}

}  // namespace keyra::hid
