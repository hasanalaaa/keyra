#pragma once
// LED status animations. Pure C++ (host-tested); the IO task renders one
// frame every 20 ms.
#include <cstdint>

namespace keyra::io {

enum class Pattern { Off, Locked, Idle, Pending, Typing, Success, Error, AwaitPresence, Setup };

struct Rgb {
  uint8_t r, g, b;
};

class LedAnimator {
 public:
  static constexpr uint32_t kFlashOnMs = 150;
  static constexpr uint32_t kFlashOffMs = 150;
  static constexpr uint32_t kSuccessMs = 2 * (kFlashOnMs + kFlashOffMs);
  static constexpr uint32_t kErrorMs = 3 * (kFlashOnMs + kFlashOffMs);

  // Success and Error are one-shots: Success then settles on Idle, Error
  // returns to whatever was showing before it.
  void set(Pattern p, uint32_t nowMs);
  void brightness(uint8_t pct);  // clamped to 0..100
  Rgb frame(uint32_t nowMs);     // gamma-corrected, brightness applied
  Pattern showing(uint32_t nowMs);

 private:
  Pattern base_ = Pattern::Off;
  Pattern oneShot_ = Pattern::Off;  // Off = none active
  uint32_t oneShotAt_ = 0;
  uint32_t baseAt_ = 0;
  uint8_t pct_ = 100;
};

// Perceptual → PWM: 255 * (v/255)^2.2, rounded. 0 stays 0.
uint8_t gamma8(uint8_t linear);

}  // namespace keyra::io
