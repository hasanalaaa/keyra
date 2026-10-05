#pragma once
// TinyUSB bring-up for Keyra: descriptors, optional dev CDC (log mirror +
// 1200-baud reboot into ROM download mode).
namespace keyra::hid {

// Returns whether the CDC interface is actually part of the enumerated
// device (false when requested but compiled out — logged as an error).
bool usbStart(bool devCdc);

// Defined in hid.cpp: forget the host's LED state when the host goes away.
void forgetHostLeds();

}  // namespace keyra::hid
