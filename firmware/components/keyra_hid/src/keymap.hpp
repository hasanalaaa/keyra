#pragma once
// Keyboard layouts: character → HID key presses, per layout (SPEC §10.1).
// Pure C++ over the generated tables (host-tested).
#include <cstddef>
#include <cstdint>

#include "keyra/hid.hpp"

namespace keyra::hid {

struct KeyStroke {
  uint8_t keycode;   // HID usage ID, Keyboard/Keypad page 0x07
  uint8_t modifier;  // HID modifier byte: only Shift and the layout's AltGr/Option
};

constexpr uint8_t KEY_CAPS_LOCK = 0x39;
constexpr uint8_t KEY_SPACE = 0x2C;
constexpr uint8_t MOD_LEFT_SHIFT = 0x02;
constexpr uint8_t MOD_LEFT_ALT = 0x04;   // Option on a Mac
constexpr uint8_t MOD_RIGHT_ALT = 0x40;  // AltGr on Windows/Linux

// One character needs at most two strokes: a dead key, then Space.
constexpr int kMaxStrokes = 2;

// Strokes that type `cp` on `layout`; 0 when the layout cannot type it
// (control characters always give 0).
int strokesFor(Layout layout, uint32_t cp, KeyStroke out[kMaxStrokes]);

// Decodes one UTF-8 code point from [p, end) and advances `p`. False on malformed input.
bool nextCodePoint(const char*& p, const char* end, uint32_t& cp);

// What the host shows for one key press under `layout`; 0 for nothing or a dead key.
uint32_t charFor(Layout layout, KeyStroke s);

// The Layout Doctor probe (SPEC §10.3): plain key presses with at most Shift,
// chosen so every layout types a visible, different string and no key is dead.
struct ProbeKey {
  uint8_t keycode;
  bool shift;
};
extern const ProbeKey kProbe[];
extern const size_t kProbeLen;

}  // namespace keyra::hid
