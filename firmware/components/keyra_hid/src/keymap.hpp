#pragma once
// US-ANSI layout: printable ASCII → HID usage + Shift. Pure C++ (host-tested).
#include <cstdint>

namespace keyra::hid {

struct KeyStroke {
  uint8_t keycode;  // HID usage ID, Keyboard/Keypad page 0x07
  bool shift;
};

constexpr uint8_t KEY_CAPS_LOCK = 0x39;
constexpr uint8_t MOD_LEFT_SHIFT = 0x02;

// False for anything outside 0x20–0x7E (control chars, DEL, UTF-8 bytes).
bool keystrokeFor(char c, KeyStroke& out);

}  // namespace keyra::hid
