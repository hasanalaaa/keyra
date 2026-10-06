#pragma once
// Turns raw scan records into the list the picker shows (SPEC §8.2 /api/wifi/scan). Pure.
#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace keyra::net::scanlist {

enum class Security { Open, Psk /* WPA2/WPA3 personal */, Other /* WEP, WPA1, enterprise */ };

struct Record {
  std::string ssid;
  int rssi = 0;
  Security security = Security::Open;
  uint8_t channel = 0;
};

struct Item {
  std::string ssid;
  int rssi = 0;
  bool secure = false;
  uint8_t channel = 0;
};

constexpr size_t kMaxItems = 20;

// Drops hidden/empty or control-character SSIDs and networks Keyra can never
// join with a passphrase (Other), keeps the strongest record per SSID (mesh
// and multi-AP homes repeat names), sorts by signal, strongest first.
std::vector<Item> tidy(const std::vector<Record>& raw, size_t max = kMaxItems);

}  // namespace keyra::net::scanlist
