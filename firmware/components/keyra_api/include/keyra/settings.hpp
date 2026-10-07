#pragma once
// Device settings persisted in NVS namespace "keyra" (SPEC §5 /api/settings).
#include <cstdint>
#include <string>

#include "esp_err.h"
#include "keyra/net.hpp"

namespace keyra::settings {

enum class Separator : uint8_t { Tab, Enter };
// Where a new type action goes (SPEC §8.1): Auto = USB when plugged in, else
// the most recently used Bluetooth device.
enum class Output : uint8_t { Auto, Usb, Ble };
// OnDemand: Bluetooth links only while an action needs one (default; an
// iPhone/iPad hides its on-screen keyboard while a keyboard is connected).
enum class BleConnect : uint8_t { OnDemand, Always };

struct Settings {
  std::string deviceName = "Keyra";
  std::string wifiSsid;  // empty → defaultSsid()
  std::string wifiPassword = "keyra1234";
  uint8_t autoLockMin = 15;
  uint16_t keyDelayMs = 12;
  Separator bothSeparator = Separator::Tab;
  bool submitAfterBoth = false;
  uint8_t ledBrightness = 50;  // percent
  bool bleEnabled = true;
  Output output = Output::Auto;
  BleConnect bleConnect = BleConnect::OnDemand;
  // Home network (SPEC §8.2). The password is write-only: never returned by the
  // API and never logged.
  bool homeEnabled = false;
  std::string homeSsid;
  std::string homePassword;
  net::ApMode apMode = net::ApMode::Always;
  // Keyboard layout of the computer on each output (SPEC §10.1): a layout id
  // from keyra_hid's table ("us", "de-mac", "ar", …).
  std::string layoutUsb = "us";
  std::string layoutBle = "us";
  // Operating system of the USB computer and of each bonded Bluetooth host
  // (SPEC §10.5; the latter as "AA:BB:CC:DD:EE:FF=mac;…", see host_os.hpp):
  // decides how text survives the host's input language.
  std::string osUsb;
  std::string osBle;
  // Auto-type sequence for "Both" on entries without their own (SPEC §10.4);
  // empty = username, separator, password (+ Enter when submitAfterBoth).
  std::string bothSequence;
};

constexpr uint8_t kMinAutoLockMin = 1, kMaxAutoLockMin = 120;
constexpr uint16_t kMinKeyDelayMs = 1, kMaxKeyDelayMs = 100;
constexpr uint8_t kMaxLedBrightness = 100;

esp_err_t load();                  // reads NVS once at boot; invalid values fall back to defaults
Settings get();
esp_err_t save(const Settings&);   // persists every field and updates the cached copy
esp_err_t erase();                 // factory reset: back to defaults
std::string defaultSsid();         // "Keyra-XXXX" from the last two SoftAP MAC bytes
std::string ssid(const Settings&); // effective SSID
net::Home home(const Settings&);   // what keyra_net should join

}  // namespace keyra::settings
