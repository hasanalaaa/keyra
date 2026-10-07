// LED animator: gamma, brightness, one-shot transitions.
#include <initializer_list>

#include "check.hpp"
#include "led_anim.hpp"

using namespace keyra::io;

namespace {

bool black(const Rgb& c) { return c.r == 0 && c.g == 0 && c.b == 0; }

void testGamma() {
  CHECK_EQ(gamma8(0), 0);
  CHECK_EQ(gamma8(255), 255);
  CHECK(gamma8(128) < 64);  // perceptual mid-grey is a low PWM duty
  for (int v = 1; v < 256; ++v) CHECK(gamma8(static_cast<uint8_t>(v)) >= gamma8(static_cast<uint8_t>(v - 1)));
}

void testOffAndBrightness() {
  LedAnimator a;
  a.set(Pattern::Off, 0);
  for (uint32_t t = 0; t < 5000; t += 20) CHECK(black(a.frame(t)));
  a.set(Pattern::Typing, 0);
  const Rgb full = a.frame(100);
  CHECK(!black(full));
  a.brightness(0);
  CHECK(black(a.frame(120)));
  a.brightness(50);
  const Rgb half = a.frame(140);
  CHECK(half.r < full.r && half.g < full.g && half.b < full.b);
  a.brightness(250);  // clamped to 100
  const Rgb clamped = a.frame(160);
  CHECK(clamped.r == full.r && clamped.g == full.g && clamped.b == full.b);
}

void testSuccessSettlesOnIdle() {
  LedAnimator a;
  a.set(Pattern::Pending, 0);
  a.set(Pattern::Success, 1000);
  CHECK(a.showing(1000) == Pattern::Success);
  const Rgb on = a.frame(1010);
  CHECK(on.g > 0 && on.r == 0);
  CHECK(black(a.frame(1000 + LedAnimator::kFlashOnMs + 10)));  // gap between flashes
  CHECK(a.showing(1000 + LedAnimator::kSuccessMs - 1) == Pattern::Success);
  CHECK(a.showing(1000 + LedAnimator::kSuccessMs) == Pattern::Idle);
}

void testErrorReturnsToPrevious() {
  LedAnimator a;
  a.set(Pattern::Pending, 0);
  a.set(Pattern::Error, 500);
  const Rgb on = a.frame(510);
  CHECK(on.r > 0 && on.g == 0);
  CHECK(a.showing(500 + LedAnimator::kErrorMs - 1) == Pattern::Error);
  CHECK(a.showing(500 + LedAnimator::kErrorMs) == Pattern::Pending);
  // Error after Success goes back to Idle (Success's destination).
  LedAnimator b;
  b.set(Pattern::Locked, 0);
  b.set(Pattern::Success, 100);
  b.set(Pattern::Error, 200);
  CHECK(b.showing(200 + LedAnimator::kErrorMs) == Pattern::Idle);
}

void testIdleIsMostlyDark() {
  LedAnimator a;
  a.set(Pattern::Idle, 0);
  int lit = 0, total = 0;
  for (uint32_t t = 0; t < 8000; t += 20, ++total) lit += !black(a.frame(t));
  CHECK(lit > 0);
  CHECK(lit * 20 < total);  // < 5 % of the time
}

void testPulsesVary() {
  for (Pattern p : {Pattern::Pending, Pattern::AwaitPresence, Pattern::Locked, Pattern::Setup, Pattern::Pairing}) {
    LedAnimator a;
    a.set(p, 0);
    int lo = 255, hi = 0;
    for (uint32_t t = 0; t < 4000; t += 20) {
      const Rgb c = a.frame(t);
      const int m = c.r > c.g ? (c.r > c.b ? c.r : c.b) : (c.g > c.b ? c.g : c.b);
      lo = m < lo ? m : lo;
      hi = m > hi ? m : hi;
    }
    CHECK(hi > lo);
    CHECK(hi > 0);
  }
  // Locked is dim: never brighter than a quarter.
  LedAnimator a;
  a.set(Pattern::Locked, 0);
  for (uint32_t t = 0; t < 4000; t += 20) CHECK(a.frame(t).r < 64);
}

// Pairing (Bluetooth window) is cyan: blue and green lit, never red, so it
// reads apart from the violet presence prompt and the blue pending pulse.
void testPairingIsCyan() {
  LedAnimator a;
  a.set(Pattern::Pairing, 0);
  bool lit = false;
  for (uint32_t t = 0; t < 2400; t += 20) {
    const Rgb c = a.frame(t);
    CHECK_EQ(c.r, 0);
    if (c.g > 0 && c.b > 0) lit = true;
    CHECK(c.g <= c.b);
  }
  CHECK(lit);
}

}  // namespace

int main() {
  testGamma();
  testOffAndBrightness();
  testSuccessSettlesOnIdle();
  testErrorReturnsToPrevious();
  testIdleIsMostlyDark();
  testPulsesVary();
  testPairingIsCyan();
  TEST_MAIN_END();
}
