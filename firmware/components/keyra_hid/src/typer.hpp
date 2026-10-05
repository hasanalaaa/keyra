#pragma once
// Typing engine. Pure C++ over a Transport so the safety rules (release on
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

  explicit Typer(Transport& t) : t_(t) {}

  Result type(const char* text, const Options& opt);
  Result tap(uint8_t keycode, const Options& opt);

 private:
  Result run(const char* text, uint8_t tapKeycode, const Options& opt);
  bool stroke(uint8_t modifier, uint8_t keycode, uint32_t holdMs, uint32_t gapMs);
  bool releaseAll();

  Transport& t_;
  std::atomic<bool> busy_{false};
};

}  // namespace keyra::hid
