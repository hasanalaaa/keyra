#pragma once
// Typing engine. Pure C++ over a Transport (USB or BLE) so the safety rules (release on
// every exit, abort on first failure, Caps Lock wrap, busy) are host-tested.
#include <atomic>
#include <cstdint>

#include "keyra/hid.hpp"

namespace keyra::hid {

class Transport {
 public:
  virtual ~Transport() = default;
  virtual bool ready() = 0;                                   // mounted & not suspended
  virtual bool capsLock() = 0;                                // last host LED report
  virtual bool numLock() { return true; }                     // last host LED report
  virtual bool send(uint8_t modifier, uint8_t keycode) = 0;   // one boot-keyboard report; bounded wait
  virtual void delayMs(uint32_t ms) = 0;
};

class Typer {
 public:
  // How long to wait for the host to confirm Caps Lock went off before
  // typing. Typing with Caps still on would invert letter case — a wrong
  // password — so we refuse instead.
  static constexpr uint32_t kCapsSettleMs = 250;
  static constexpr uint32_t kCapsPollMs = 5;
  // macOS ignores Caps Lock taps shorter than ~100 ms (accidental-press guard).
  static constexpr uint32_t kCapsHoldMs = 150;
  static constexpr int kReleaseAttempts = 3;
  // Lets the OS finish switching input language before the next key.
  static constexpr uint32_t kChordSettleMs = 250;

  // The transport is per call (USB or BLE); `busy` spans both, so only one
  // typing operation runs at a time whichever host it targets.
  Result type(Transport& t, const char* text, const Options& opt);
  Result tap(Transport& t, uint8_t keycode, const Options& opt);
  Result probe(Transport& t, const Options& opt);
  Result chord(Transport& t, uint8_t modifier, uint8_t keycode, const Options& opt);

 private:
  enum class Job { Text, Tap, Probe, Chord };
  Result run(Transport& t, Job job, const char* text, uint8_t tapKeycode, uint8_t chordMod, const Options& opt);
  static bool stroke(Transport& t, uint8_t modifier, uint8_t keycode, uint32_t holdMs, uint32_t gapMs);
  static bool altCode(Transport& t, uint32_t cp, uint32_t gapMs);
  static bool lockKey(Transport& t, uint8_t keycode, bool (Transport::*state)(), bool want, uint32_t gapMs,
                      bool& toggled);
  static bool releaseAll(Transport& t);

  std::atomic<bool> busy_{false};
};

}  // namespace keyra::hid
