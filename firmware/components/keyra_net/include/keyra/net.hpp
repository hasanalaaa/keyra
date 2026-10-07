#pragma once
// Keyra's own Wi-Fi (SoftAP, captive DNS) plus the optional home network
// (station), mDNS keyra.local on both, and SNTP (SPEC §4.1, §5, §8.2).
//
// One task ("net") owns every Wi-Fi driver call after start(): joining with
// backoff, turning the AP on/off for apMode "fallback", AP credential changes
// and scans. The functions below only hand it work, so callers never race it.
#include <cstdint>
#include <string>
#include <vector>

#include "esp_err.h"
#include "keyra/net_types.hpp"

namespace keyra::net {

struct Config { std::string ssid, password; uint8_t channel = 6; };

struct Home {
  bool enabled = false;
  std::string ssid, password;  // password: WPA2/WPA3 passphrase; never logged
  ApMode apMode = ApMode::Always;
};

esp_err_t start(const Config& ap, const Home& home);  // AP (+ station), DNS, mDNS keyra.local
esp_err_t reconfigure(const Config&);  // new AP credentials; applied by the net task
esp_err_t setHome(const Home&);        // join/leave/change; resets the retry backoff
int stations();

struct Status {
  bool apOn = false;
  int apClients = 0;
  bool homeEnabled = false, homeConnected = false;
  std::string homeSsid;
  std::string homeIp;  // dotted quad while connected, else empty
  int rssi = 0;        // dBm while connected
  HomeError homeError = HomeError::None;
};
Status status();
std::string homeIp();  // Status::homeIp without the rest

struct Network {
  std::string ssid;
  int rssi = 0;
  bool secure = false;  // WPA2/WPA3 with a password; false = open (Keyra won't join those)
  uint8_t channel = 0;
};
// Blocks ≈2–4 s (longer while a join attempt finishes). The AP keeps beaconing
// between scanned channels, so phones on it may only see a short stall.
esp_err_t scan(std::vector<Network>& out);

// How an HTTP request reached the device (Via), from its socket's local address.
Via viaForSocket(int fd);

// True while SNTP has set the clock recently; client clocks are then ignored.
bool timeSynced();

}  // namespace keyra::net
