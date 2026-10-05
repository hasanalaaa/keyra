#pragma once
// USB HID keyboard typing for Keyra (SPEC §4.1). Thread-safe: one typing
// operation at a time; a concurrent call returns Result::Busy.
#include <cstdint>

namespace keyra::hid {

enum class Result { Ok, NotMounted, Busy, Unsupported /*char not on US layout*/, Failed };

struct Options { uint16_t keyDelayMs = 12; };

// HID usage IDs (USB HID Usage Tables, Keyboard page 0x07) for tapKey().
constexpr uint8_t KEY_ENTER = 0x28;
constexpr uint8_t KEY_TAB   = 0x2B;

void   init(bool devCdc);        // devCdc: composite HID+CDC with 1200-baud → ROM download hook + log mirror
bool   mounted();                // host enumerated & not suspended
bool   capsLock();               // from host LED report
Result typeText(const char* text, const Options&);   // handles CapsLock (toggle off/restore), always releases keys
Result tapKey(uint8_t hidKeycode, const Options&);   // e.g. KEY_TAB, KEY_ENTER
bool   typeable(const char* text);                   // printable US-ASCII only

}  // namespace keyra::hid
