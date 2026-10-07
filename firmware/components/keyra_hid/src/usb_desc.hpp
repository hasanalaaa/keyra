#pragma once
// Keyra USB descriptors as plain constexpr bytes (no TinyUSB macros) so the
// host tests can parse and cross-check them. Two variants exist and each one
// declares exactly the interfaces it carries: the previous product shipped a
// configuration that declared CDC while the CDC driver was compiled out, and
// SET_CONFIGURATION failed (host saw no interfaces). usb_dev.cpp additionally
// static_asserts these lengths against TinyUSB's own *_DESC_LEN macros.
//
// Both variants carry the FIDO security-key interface (usage page 0xF1D0,
// keyra_fido, docs/FIDO.md) as interface 1, right after the keyboard.
// Endpoint budget (ESP32-S3 OTG: EP0 + at most 4 IN endpoints in use):
// release uses IN 0x81 (keyboard), 0x84 (FIDO) + OUT 0x04; dev adds CDC
// IN 0x82, 0x83 + OUT 0x03, i.e. 4 IN endpoints besides EP0.
#include <array>
#include <cstddef>
#include <cstdint>
#include <initializer_list>

namespace keyra::hid::desc {

constexpr uint16_t kVid = 0x303A;  // Espressif
constexpr uint16_t kPid = 0x8000;  // "Espressif test PID" (espressif/usb-pids) — dev only
// Hosts cache descriptors per VID/PID/bcdDevice; distinct revisions keep the
// HID-only and composite layouts from being confused with each other.
// 0x011x: with the FIDO interface (0x010x had the keyboard alone).
constexpr uint16_t kBcdHidOnly = 0x0110;
constexpr uint16_t kBcdHidCdc = 0x0111;

enum StringIndex : uint8_t {
  kStrLang = 0, kStrManufacturer, kStrProduct, kStrSerial, kStrHidItf, kStrCdcItf, kStrFidoItf, kStrCount
};

constexpr uint8_t kItfHid = 0;
constexpr uint8_t kItfFido = 1;
constexpr uint8_t kItfCdcComm = 2;
constexpr uint8_t kItfCdcData = 3;

// TinyUSB HID instances follow the interface order.
constexpr uint8_t kInstKeyboard = 0;
constexpr uint8_t kInstFido = 1;

constexpr uint8_t kEpHidIn = 0x81;
constexpr uint8_t kEpFidoOut = 0x04;
constexpr uint8_t kEpFidoIn = 0x84;
constexpr uint8_t kEpCdcNotif = 0x82;
constexpr uint8_t kEpCdcOut = 0x03;
constexpr uint8_t kEpCdcIn = 0x83;

constexpr uint8_t kHidReportLen = 8;   // boot keyboard: modifiers, reserved, 6 keys
constexpr uint8_t kHidPollMs = 5;
constexpr uint8_t kCdcNotifLen = 8;
constexpr uint8_t kCdcDataLen = 64;    // full-speed bulk

// HID 1.11 Appendix B.1 boot keyboard (no report ID): 8-bit modifiers,
// reserved byte, 5 LED output bits (+3 pad), 6 key array.
constexpr std::array<uint8_t, 63> kHidReport = {
    0x05, 0x01, 0x09, 0x06, 0xA1, 0x01,                    // Generic Desktop / Keyboard / Application
    0x05, 0x07, 0x19, 0xE0, 0x29, 0xE7, 0x15, 0x00, 0x25, 0x01,
    0x75, 0x01, 0x95, 0x08, 0x81, 0x02,                    // 8 modifier bits (Input)
    0x95, 0x01, 0x75, 0x08, 0x81, 0x01,                    // reserved byte (Constant)
    0x95, 0x05, 0x75, 0x01, 0x05, 0x08, 0x19, 0x01, 0x29, 0x05,
    0x91, 0x02,                                            // 5 LEDs (Output) — Caps Lock = bit 1
    0x95, 0x01, 0x75, 0x03, 0x91, 0x01,                    // LED padding
    0x95, 0x06, 0x75, 0x08, 0x15, 0x00, 0x25, 0x65,
    0x05, 0x07, 0x19, 0x00, 0x29, 0x65, 0x81, 0x00,        // 6 keycodes (Input, array)
    0xC0,
};

constexpr uint8_t kLedNumLockBit = 0x01;
constexpr uint8_t kLedCapsLockBit = 0x02;

// FIDO Alliance usage page, CTAPHID usage: 64-byte input and output reports,
// no report ID (CTAP 2.1 §11.2.8.1).
constexpr uint8_t kFidoReportLen = 64;
constexpr uint8_t kFidoPollMs = 5;
constexpr std::array<uint8_t, 34> kFidoReport = {
    0x06, 0xD0, 0xF1,        // Usage Page (FIDO Alliance)
    0x09, 0x01,              // Usage (CTAPHID)
    0xA1, 0x01,              // Collection (Application)
    0x09, 0x20,              //   Usage (Input Report Data)
    0x15, 0x00, 0x26, 0xFF, 0x00, 0x75, 0x08, 0x95, kFidoReportLen,
    0x81, 0x02,              //   Input (Data, Variable, Absolute)
    0x09, 0x21,              //   Usage (Output Report Data)
    0x15, 0x00, 0x26, 0xFF, 0x00, 0x75, 0x08, 0x95, kFidoReportLen,
    0x91, 0x02,              //   Output (Data, Variable, Absolute)
    0xC0,                    // End Collection
};

constexpr size_t kConfigLen = 9;
constexpr size_t kHidBlockLen = 9 + 9 + 7;                          // itf + HID + EP
constexpr size_t kFidoBlockLen = 9 + 9 + 7 + 7;                     // itf + HID + EP out + EP in
constexpr size_t kCdcBlockLen = 8 + 9 + 5 + 5 + 4 + 5 + 7 + 9 + 7 + 7;  // IAD … data EPs
constexpr size_t kTotalHidOnly = kConfigLen + kHidBlockLen + kFidoBlockLen;
constexpr size_t kTotalHidCdc = kTotalHidOnly + kCdcBlockLen;
static_assert(kTotalHidOnly == 66, "HID (keyboard + FIDO) configuration length");
static_assert(kTotalHidCdc == 132, "HID + CDC configuration length");

namespace detail {

template <size_t N>
struct Writer {
  std::array<uint8_t, N> b{};
  size_t n = 0;
  constexpr void put(std::initializer_list<uint8_t> xs) {
    for (uint8_t x : xs) b[n++] = x;
  }
  constexpr void u16(uint16_t v) { put({static_cast<uint8_t>(v & 0xFF), static_cast<uint8_t>(v >> 8)}); }
};

template <size_t N>
constexpr void hidBlock(Writer<N>& w) {
  w.put({9, 0x04, kItfHid, 0, 1, 0x03 /*HID*/, 0x01 /*boot*/, 0x01 /*keyboard*/, kStrHidItf});
  w.put({9, 0x21, 0x11, 0x01 /*HID 1.11*/, 0x00, 1, 0x22 /*report*/});
  w.u16(static_cast<uint16_t>(kHidReport.size()));
  w.put({7, 0x05, kEpHidIn, 0x03 /*interrupt*/, kHidReportLen, 0x00, kHidPollMs});
}

template <size_t N>
constexpr void fidoBlock(Writer<N>& w) {
  w.put({9, 0x04, kItfFido, 0, 2, 0x03 /*HID*/, 0x00 /*no boot*/, 0x00, kStrFidoItf});
  w.put({9, 0x21, 0x11, 0x01, 0x00, 1, 0x22});
  w.u16(static_cast<uint16_t>(kFidoReport.size()));
  w.put({7, 0x05, kEpFidoOut, 0x03 /*interrupt*/, kFidoReportLen, 0x00, kFidoPollMs});
  w.put({7, 0x05, kEpFidoIn, 0x03, kFidoReportLen, 0x00, kFidoPollMs});
}

template <size_t N>
constexpr void cdcBlock(Writer<N>& w) {
  w.put({8, 0x0B, kItfCdcComm, 2, 0x02, 0x02, 0x00, 0});       // IAD: CDC / ACM
  w.put({9, 0x04, kItfCdcComm, 0, 1, 0x02, 0x02, 0x00, kStrCdcItf});
  w.put({5, 0x24, 0x00, 0x20, 0x01});                          // Header, CDC 1.20
  w.put({5, 0x24, 0x01, 0x00, kItfCdcData});                   // Call management
  w.put({4, 0x24, 0x02, 0x06});                                // ACM: line coding/state + break
  w.put({5, 0x24, 0x06, kItfCdcComm, kItfCdcData});            // Union
  w.put({7, 0x05, kEpCdcNotif, 0x03 /*interrupt*/, kCdcNotifLen, 0x00, 1});
  w.put({9, 0x04, kItfCdcData, 0, 2, 0x0A, 0x00, 0x00, 0});
  w.put({7, 0x05, kEpCdcOut, 0x02 /*bulk*/, kCdcDataLen, 0x00, 0});
  w.put({7, 0x05, kEpCdcIn, 0x02, kCdcDataLen, 0x00, 0});
}

template <size_t N>
constexpr std::array<uint8_t, N> config(uint8_t numItf, bool cdc) {
  Writer<N> w;
  w.put({9, 0x02});
  w.u16(static_cast<uint16_t>(N));
  w.put({numItf, 1 /*bConfigurationValue*/, 0, 0x80 /*bus powered*/, 50 /*100 mA*/});
  hidBlock(w);
  fidoBlock(w);
  if (cdc) cdcBlock(w);
  return w.b;
}

template <bool kCdc>
constexpr std::array<uint8_t, 18> device() {
  Writer<18> w;
  w.put({18, 0x01});
  w.u16(0x0200);
  if (kCdc) {
    w.put({0xEF, 0x02, 0x01});  // Misc / IAD: required for a composite with CDC
  } else {
    w.put({0x00, 0x00, 0x00});  // class defined per interface
  }
  w.put({64});
  w.u16(kVid);
  w.u16(kPid);
  w.u16(kCdc ? kBcdHidCdc : kBcdHidOnly);
  w.put({kStrManufacturer, kStrProduct, kStrSerial, 1});
  return w.b;
}

}  // namespace detail

constexpr auto kConfigHidOnly = detail::config<kTotalHidOnly>(2, false);
constexpr auto kConfigHidCdc = detail::config<kTotalHidCdc>(4, true);
constexpr auto kDeviceHidOnly = detail::device<false>();
constexpr auto kDeviceHidCdc = detail::device<true>();

constexpr const char* kManufacturer = "Keyra";
constexpr const char* kProduct = "Keyra Key";
constexpr const char* kHidItfName = "Keyra Keyboard";
constexpr const char* kCdcItfName = "Keyra Console";
constexpr const char* kFidoItfName = "Keyra Security Key";

}  // namespace keyra::hid::desc
