#pragma once
// Keyboard typing for Keyra over USB HID or Bluetooth LE (SPEC §4.1, §8.1).
// One typing engine (keymap, Caps Lock wrap, always-release) drives either
// transport. Thread-safe: one typing operation at a time; a concurrent call
// returns Result::Busy.
#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>

namespace keyra::hid {

// NotMounted: the chosen host is not connected (USB unplugged / no BLE host).
// Unsupported: a character the chosen keyboard layout cannot type.
enum class Result { Ok, NotMounted, Busy, Unsupported, Failed };

// The keyboard layout the host computer uses (SPEC §10.1): an index into the
// table built from layouts/layouts.txt. 0 is "us", the default.
using Layout = uint8_t;
constexpr Layout kLayoutUs = 0;

struct LayoutInfo {
  const char* id;        // stable, stored in settings: "us", "de-mac", "ar", …
  const char* name;      // English display name
  const char* platform;  // "any", "windows" (also the usual Linux layouts) or "mac"
};

enum class Host : uint8_t { Usb, Ble };  // which transport carries the keystrokes

struct Options {
  uint16_t keyDelayMs = 12;
  // Chosen by the caller once per job (keyra_api picks the target when the
  // action is armed), so a cable plugged in halfway can never send the
  // password to a different computer.
  Host via = Host::Usb;
  Layout layout = kLayoutUs;
};

// HID usage IDs (USB HID Usage Tables, Keyboard page 0x07) for tapKey().
constexpr uint8_t KEY_ENTER = 0x28;
constexpr uint8_t KEY_TAB   = 0x2B;

void   init(bool devCdc);        // devCdc: composite HID+CDC with 1200-baud → ROM download hook + log mirror
bool   mounted();                // USB host enumerated & not suspended
bool   bleConnected();           // bonded BLE host connected, encrypted, subscribed
bool   capsLock();               // USB host's Caps Lock, from its LED report
Result typeText(const char* text, const Options&);   // handles CapsLock (toggle off/restore), always releases keys
Result tapKey(uint8_t hidKeycode, const Options&);   // e.g. KEY_TAB, KEY_ENTER
Result typeProbe(const Options&);                    // the Layout Doctor probe (no Enter, only Shift)
// UTF-8 text whose every character `layout` can type (control characters never).
bool   typeable(const char* text, Layout layout);

size_t     layoutCount();
LayoutInfo layoutInfo(Layout);
bool       findLayout(std::string_view id, Layout& out);
// Layout-proof characters (SPEC §10.2): `cp` is typed by the very same key
// press (no dead key) on every one of `layouts`, so it comes out right whichever
// of them the computer really uses.
bool        sameOnAll(uint32_t cp, const Layout* layouts, size_t n);
std::string probeText(Layout);  // what the probe types on a computer with that layout

}  // namespace keyra::hid
