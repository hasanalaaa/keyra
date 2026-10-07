#pragma once
// HID report map served over GATT: the USB keyboard's map
// (keyra_hid/src/usb_desc.hpp) plus Report ID 1 right after the collection
// opens — the host test checks exactly that difference. Apple hosts read a
// map without report IDs and Report References of ID 0, then never subscribe
// to the input report; keyboards that work with them declare an ID. Over GATT
// the ID lives only in the Report Reference descriptors: notifications still
// carry the same 8-byte boot-keyboard report, and the LED output report the
// same 1 byte.
#include <array>
#include <cstdint>

namespace keyra::ble {

constexpr uint8_t kReportId = 1;

constexpr std::array<uint8_t, 65> kReportMap = {
    0x05, 0x01, 0x09, 0x06, 0xA1, 0x01,                    // Generic Desktop / Keyboard / Application
    0x85, kReportId,                                       // Report ID 1
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

constexpr uint8_t kInputReportLen = 8;
constexpr uint8_t kLedNumLockBit = 0x01;
constexpr uint8_t kLedCapsLockBit = 0x02;

}  // namespace keyra::ble
