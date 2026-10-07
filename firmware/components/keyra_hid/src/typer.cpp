#include "typer.hpp"

#include <cstring>

#include "keymap.hpp"

namespace keyra::hid {

Result Typer::type(Transport& t, const char* text, const Options& opt) {
  if (text == nullptr || !typeable(text, opt)) return Result::Unsupported;
  return run(t, Job::Text, text, 0, 0, opt);
}

Result Typer::tap(Transport& t, uint8_t keycode, const Options& opt) {
  // 0 is "no event" and >= 0xE0 are modifiers; neither is a tappable key.
  if (keycode == 0 || keycode >= 0xE0) return Result::Unsupported;
  return run(t, Job::Tap, nullptr, keycode, 0, opt);
}

Result Typer::probe(Transport& t, const Options& opt) { return run(t, Job::Probe, nullptr, 0, 0, opt); }

Result Typer::chord(Transport& t, uint8_t modifier, uint8_t keycode, const Options& opt) {
  if (modifier == 0 || keycode == 0 || keycode >= 0xE0) return Result::Unsupported;
  return run(t, Job::Chord, nullptr, keycode, modifier, opt);
}

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

// Alt held, the decimal code on the keypad, Alt released: Windows inserts the
// character on the release. Without a leading 0 the code is read in the OEM
// code page, which is ASCII for 32..126 in every locale.
bool Typer::altCode(Transport& t, uint32_t cp, uint32_t gapMs) {
  char digits[4];
  int n = 0;
  for (uint32_t v = cp; v != 0 || n == 0; v /= 10) digits[n++] = static_cast<char>('0' + v % 10);
  if (!t.send(MOD_LEFT_ALT, 0)) return false;
  t.delayMs(gapMs);
  for (int i = n - 1; i >= 0; --i) {
    const uint8_t kp = digits[i] == '0' ? KEY_KP_0 : static_cast<uint8_t>(KEY_KP_1 + (digits[i] - '1'));
    if (!t.send(MOD_LEFT_ALT, kp)) return false;
    t.delayMs(gapMs);
    if (!t.send(MOD_LEFT_ALT, 0)) return false;
    t.delayMs(gapMs);
  }
  if (!t.send(0, 0)) return false;
  t.delayMs(gapMs);
  return true;
}

// Brings a lock key (Caps, Num) to `want` and waits for the host's LED report
// to confirm. `toggled` tells the caller to tap it back afterwards. False when
// the host ignored the tap: typing now would come out wrong.
bool Typer::lockKey(Transport& t, uint8_t keycode, bool (Transport::*state)(), bool want, uint32_t gapMs,
                    bool& toggled) {
  if ((t.*state)() == want) return true;
  if (!t.send(0, keycode)) return false;
  toggled = true;  // hosts toggle on key-down: restore from here on
  t.delayMs(kCapsHoldMs);
  if (!t.send(0, 0)) return false;
  t.delayMs(gapMs);
  uint32_t waited = 0;
  while ((t.*state)() != want && waited < kCapsSettleMs) {
    t.delayMs(kCapsPollMs);
    waited += kCapsPollMs;
  }
  if ((t.*state)() != want) {
    // Host ignored the tap (or never reports LEDs): its state is unchanged,
    // so there is nothing to restore.
    toggled = false;
    return false;
  }
  return true;
}

Result Typer::run(Transport& t, Job job, const char* text, uint8_t tapKeycode, uint8_t chordMod, const Options& opt) {
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
  bool numTurnedOn = false;

  const bool printing = job == Job::Probe || (job == Job::Text && *text != '\0');
  // Alt codes need the keypad to type digits, not move the cursor.
  if (printing && job == Job::Text && opt.altCodes)
    ok = lockKey(t, KEY_NUM_LOCK, &Transport::numLock, true, delay, numTurnedOn);
  if (ok && printing) ok = lockKey(t, KEY_CAPS_LOCK, &Transport::capsLock, false, delay, capsTurnedOff);

  if (ok) {
    if (job == Job::Text) {
      const char* end = text + std::strlen(text);
      for (const char* p = text; ok && p < end;) {
        uint32_t cp = 0;
        KeyStroke ks[kMaxStrokes];
        nextCodePoint(p, end, cp);  // cannot fail: typeable() was checked
        if (opt.altCodes && cp >= 0x20 && cp <= 0x7E) {
          ok = altCode(t, cp, delay);
          continue;
        }
        const int n = strokesFor(opt.layout, cp, ks);
        for (int i = 0; ok && i < n; ++i) ok = stroke(t, ks[i].modifier, ks[i].keycode, delay, delay);
      }
    } else if (job == Job::Probe) {
      for (size_t i = 0; ok && i < kProbeLen; ++i)
        ok = stroke(t, kProbe[i].shift ? MOD_LEFT_SHIFT : 0, kProbe[i].keycode, delay, delay);
    } else if (job == Job::Chord) {
      // Modifier first, as a person presses it: some hosts ignore a chord
      // whose modifier and key arrive in the same report.
      ok = t.send(chordMod, 0);
      if (ok) {
        t.delayMs(delay);
        ok = stroke(t, chordMod, tapKeycode, delay, delay);
      }
      if (ok) t.delayMs(kChordSettleMs);
    } else {
      ok = stroke(t, 0, tapKeycode, delay, delay);
    }
  }

  // Every exit after we own the bus: nothing may stay pressed, then put the
  // user's lock keys back exactly as we found them.
  if (!releaseAll(t)) ok = false;
  if (capsTurnedOff) {
    if (!stroke(t, 0, KEY_CAPS_LOCK, kCapsHoldMs, delay)) ok = false;
    if (!releaseAll(t)) ok = false;
  }
  if (numTurnedOn) {
    if (!stroke(t, 0, KEY_NUM_LOCK, kCapsHoldMs, delay)) ok = false;
    if (!releaseAll(t)) ok = false;
  }
  return ok ? Result::Ok : Result::Failed;
}

}  // namespace keyra::hid
