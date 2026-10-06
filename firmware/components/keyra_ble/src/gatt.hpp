#pragma once
// Keyra's GATT database: HID over GATT keyboard (0x1812), Device Information
// (0x180A) and Battery (0x180F). Every HID attribute requires an encrypted
// link, so a host must pair (and, outside the window, be bonded) to read the
// report map, subscribe to keystrokes or send LED reports.
#include <cstdint>

namespace keyra::ble::gatt {

int registerServices();  // after nimble_port_init(), before the host task starts

uint16_t inputHandle();      // Report characteristic (report protocol)
uint16_t bootInputHandle();  // Boot Keyboard Input (boot protocol)
bool bootProtocol();         // host selected boot protocol (BIOS-style hosts)
uint8_t leds();              // last LED output report from the connected host
void resetLink();            // new connection: report protocol, LEDs off

}  // namespace keyra::ble::gatt
