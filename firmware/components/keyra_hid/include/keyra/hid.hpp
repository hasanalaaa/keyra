#pragma once
// Keyboard typing for Keyra over USB HID or Bluetooth LE (SPEC §4.1, §8.1).
// One typing engine (keymap, Caps Lock wrap, always-release) drives either
// transport. Thread-safe: one typing operation at a time; a concurrent call
// returns Result::Busy.
#include <cstddef>
#include <cstdint>

namespace keyra::hid {

// NotMounted: the chosen host is not connected (USB unplugged / no BLE host).
enum class Result { Ok, NotMounted, Busy, Unsupported /*char not on US layout*/, Failed };

enum class Host : uint8_t { Usb, Ble };  // which transport carries the keystrokes

struct Options {
  uint16_t keyDelayMs = 12;
  // Chosen by the caller once per job (keyra_api picks the target when the
  // action is armed), so a cable plugged in halfway can never send the
  // password to a different computer.
  Host via = Host::Usb;
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
bool   typeable(const char* text);                   // printable US-ASCII only

// The FIDO security-key interface (usage page 0xF1D0, 64-byte reports) that
// keyra_fido speaks CTAPHID over. The receiver runs in the USB task: copy and return.
constexpr size_t kFidoReportLen = 64;
using FidoReceiver = void (*)(const uint8_t* report, size_t len);
void setFidoReceiver(FidoReceiver rx);
// Queues one input report; false if the host does not take it within timeoutMs
// (unplugged, suspended, or not reading).
bool fidoSend(const uint8_t report[kFidoReportLen], uint32_t timeoutMs);

}  // namespace keyra::hid
