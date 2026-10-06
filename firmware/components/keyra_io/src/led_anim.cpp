#include "led_anim.hpp"

#include <cmath>

namespace keyra::io {
namespace {

constexpr float kPi = 3.14159265f;

struct Color {
  float r, g, b;  // 0..1, perceptual (pre-gamma)
};

constexpr Color kRed{1.0f, 0.0f, 0.0f};
constexpr Color kGreen{0.0f, 1.0f, 0.0f};
constexpr Color kBlue{0.0f, 0.25f, 1.0f};
constexpr Color kViolet{0.6f, 0.0f, 1.0f};
constexpr Color kWhite{1.0f, 1.0f, 1.0f};
constexpr Color kAmber{1.0f, 0.5f, 0.0f};
constexpr Color kCyan{0.0f, 0.8f, 1.0f};

// Smooth 0..1..0 over one period, starting dark.
float wave(uint32_t t, uint32_t periodMs) {
  const float phase = static_cast<float>(t % periodMs) / static_cast<float>(periodMs);
  return 0.5f - 0.5f * std::cos(2.0f * kPi * phase);
}

float flash(uint32_t t) { return (t % (LedAnimator::kFlashOnMs + LedAnimator::kFlashOffMs)) < LedAnimator::kFlashOnMs ? 1.0f : 0.0f; }

struct Level {
  Color c;
  float v;  // 0..1
};

Level levelFor(Pattern p, uint32_t t) {
  switch (p) {
    case Pattern::Off:           return {kWhite, 0.0f};
    case Pattern::Locked:        return {kRed, 0.04f + 0.26f * wave(t, 4000)};       // slow, dim
    case Pattern::Idle:          return {kGreen, (t % 4000) < 80 ? 0.3f : 0.0f};    // brief soft blink
    case Pattern::Pending:       return {kBlue, 0.1f + 0.9f * wave(t, 1200)};
    case Pattern::AwaitPresence: return {kViolet, 0.1f + 0.9f * wave(t, 1200)};
    case Pattern::Typing:        return {kWhite, 0.7f};
    case Pattern::Success:       return {kGreen, flash(t)};
    case Pattern::Error:         return {kRed, flash(t)};
    case Pattern::Setup:         return {kAmber, 0.08f + 0.62f * wave(t, 3000)};
    case Pattern::Pairing:       return {kCyan, 0.1f + 0.9f * wave(t, 1200)};  // Bluetooth pairing window
  }
  return {kWhite, 0.0f};
}

uint8_t to8(float x) {
  if (x <= 0.0f) return 0;
  if (x >= 1.0f) return 255;
  return static_cast<uint8_t>(std::lround(x * 255.0f));
}

}  // namespace

uint8_t gamma8(uint8_t linear) {
  return to8(std::pow(static_cast<float>(linear) / 255.0f, 2.2f));
}

void LedAnimator::set(Pattern p, uint32_t now) {
  if (p == Pattern::Success || p == Pattern::Error) {
    if (p == Pattern::Success) {
      base_ = Pattern::Idle;
      baseAt_ = now;
    }
    oneShot_ = p;
    oneShotAt_ = now;
    return;
  }
  oneShot_ = Pattern::Off;
  if (p != base_) baseAt_ = now;  // restart the animation phase on a real change
  base_ = p;
}

void LedAnimator::brightness(uint8_t pct) { pct_ = pct > 100 ? 100 : pct; }

Pattern LedAnimator::showing(uint32_t now) {
  if (oneShot_ != Pattern::Off) {
    const uint32_t len = oneShot_ == Pattern::Success ? kSuccessMs : kErrorMs;
    if (now - oneShotAt_ < len) return oneShot_;
    oneShot_ = Pattern::Off;
  }
  return base_;
}

Rgb LedAnimator::frame(uint32_t now) {
  const Pattern p = showing(now);
  const uint32_t t = now - (oneShot_ != Pattern::Off ? oneShotAt_ : baseAt_);
  const Level l = levelFor(p, t);
  const float v = l.v * static_cast<float>(pct_) / 100.0f;
  return {gamma8(to8(l.c.r * v)), gamma8(to8(l.c.g * v)), gamma8(to8(l.c.b * v))};
}

}  // namespace keyra::io
