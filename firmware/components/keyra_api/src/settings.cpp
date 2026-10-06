#include "keyra/settings.hpp"

#include <cstdio>
#include <mutex>

#include "esp_check.h"
#include "esp_log.h"
#include "esp_mac.h"
#include "nvs.h"
#include "validate.hpp"

namespace keyra::settings {
namespace {

const char* TAG = "settings";
constexpr const char* kNamespace = "keyra";

std::mutex g_mu;
Settings g_cache;

std::string readString(nvs_handle_t h, const char* key, const std::string& fallback) {
  size_t len = 0;
  if (nvs_get_str(h, key, nullptr, &len) != ESP_OK || len == 0) return fallback;
  std::string s(len, '\0');
  if (nvs_get_str(h, key, s.data(), &len) != ESP_OK) return fallback;
  s.resize(len - 1);  // drop the stored NUL
  return s;
}

template <typename T, typename Get>
T readInt(nvs_handle_t h, const char* key, T fallback, Get get) {
  T v;
  return get(h, key, &v) == ESP_OK ? v : fallback;
}

// A corrupt or out-of-range value must never brick the AP or the UI.
Settings sanitized(Settings s) {
  const Settings d;
  if (!api::validate::deviceName(s.deviceName)) s.deviceName = d.deviceName;
  if (!s.wifiSsid.empty() && !api::validate::ssid(s.wifiSsid)) s.wifiSsid.clear();
  if (s.wifiPassword != d.wifiPassword && !api::validate::wifiPassword(s.wifiPassword)) {
    ESP_LOGW(TAG, "stored Wi-Fi password invalid; using factory default");
    s.wifiPassword = d.wifiPassword;
  }
  if (s.autoLockMin < kMinAutoLockMin || s.autoLockMin > kMaxAutoLockMin) s.autoLockMin = d.autoLockMin;
  if (s.keyDelayMs < kMinKeyDelayMs || s.keyDelayMs > kMaxKeyDelayMs) s.keyDelayMs = d.keyDelayMs;
  if (s.bothSeparator != Separator::Tab && s.bothSeparator != Separator::Enter) s.bothSeparator = d.bothSeparator;
  if (s.ledBrightness > kMaxLedBrightness) s.ledBrightness = d.ledBrightness;
  if (s.apMode != net::ApMode::Always && s.apMode != net::ApMode::Fallback) s.apMode = d.apMode;
  if (s.homeEnabled && (!api::validate::ssid(s.homeSsid) || !api::validate::homePassword(s.homePassword))) {
    ESP_LOGW(TAG, "stored home Wi-Fi invalid; home Wi-Fi off");
    s.homeEnabled = false;
  }
  return s;
}

}  // namespace

esp_err_t load() {
  nvs_handle_t h;
  const esp_err_t err = nvs_open(kNamespace, NVS_READONLY, &h);
  Settings s;
  if (err == ESP_OK) {
    s.deviceName = readString(h, "name", s.deviceName);
    s.wifiSsid = readString(h, "wifiSsid", s.wifiSsid);
    s.wifiPassword = readString(h, "wifiPass", s.wifiPassword);
    s.autoLockMin = readInt<uint8_t>(h, "autoLock", s.autoLockMin, nvs_get_u8);
    s.keyDelayMs = readInt<uint16_t>(h, "keyDelay", s.keyDelayMs, nvs_get_u16);
    s.bothSeparator = static_cast<Separator>(
        readInt<uint8_t>(h, "bothSep", static_cast<uint8_t>(s.bothSeparator), nvs_get_u8));
    s.submitAfterBoth = readInt<uint8_t>(h, "submitBoth", s.submitAfterBoth, nvs_get_u8) != 0;
    s.ledBrightness = readInt<uint8_t>(h, "ledBright", s.ledBrightness, nvs_get_u8);
    s.homeEnabled = readInt<uint8_t>(h, "homeOn", s.homeEnabled, nvs_get_u8) != 0;
    s.homeSsid = readString(h, "homeSsid", s.homeSsid);
    s.homePassword = readString(h, "homePass", s.homePassword);
    s.apMode = static_cast<net::ApMode>(readInt<uint8_t>(h, "apMode", static_cast<uint8_t>(s.apMode), nvs_get_u8));
    nvs_close(h);
  } else if (err != ESP_ERR_NVS_NOT_FOUND) {
    ESP_LOGE(TAG, "nvs_open: %s", esp_err_to_name(err));
    return err;
  }
  std::lock_guard<std::mutex> lock(g_mu);
  g_cache = sanitized(s);
  return ESP_OK;
}

Settings get() {
  std::lock_guard<std::mutex> lock(g_mu);
  return g_cache;
}

esp_err_t save(const Settings& s) {
  std::lock_guard<std::mutex> lock(g_mu);
  nvs_handle_t h;
  ESP_RETURN_ON_ERROR(nvs_open(kNamespace, NVS_READWRITE, &h), TAG, "nvs_open");
  esp_err_t err = nvs_set_str(h, "name", s.deviceName.c_str());
  if (err == ESP_OK) err = nvs_set_str(h, "wifiSsid", s.wifiSsid.c_str());
  if (err == ESP_OK) err = nvs_set_str(h, "wifiPass", s.wifiPassword.c_str());
  if (err == ESP_OK) err = nvs_set_u8(h, "autoLock", s.autoLockMin);
  if (err == ESP_OK) err = nvs_set_u16(h, "keyDelay", s.keyDelayMs);
  if (err == ESP_OK) err = nvs_set_u8(h, "bothSep", static_cast<uint8_t>(s.bothSeparator));
  if (err == ESP_OK) err = nvs_set_u8(h, "submitBoth", s.submitAfterBoth ? 1 : 0);
  if (err == ESP_OK) err = nvs_set_u8(h, "ledBright", s.ledBrightness);
  if (err == ESP_OK) err = nvs_set_u8(h, "homeOn", s.homeEnabled ? 1 : 0);
  if (err == ESP_OK) err = nvs_set_str(h, "homeSsid", s.homeSsid.c_str());
  if (err == ESP_OK) err = nvs_set_str(h, "homePass", s.homePassword.c_str());
  if (err == ESP_OK) err = nvs_set_u8(h, "apMode", static_cast<uint8_t>(s.apMode));
  if (err == ESP_OK) err = nvs_commit(h);
  nvs_close(h);
  ESP_RETURN_ON_ERROR(err, TAG, "write");
  g_cache = s;
  return ESP_OK;
}

esp_err_t erase() {
  std::lock_guard<std::mutex> lock(g_mu);
  nvs_handle_t h;
  ESP_RETURN_ON_ERROR(nvs_open(kNamespace, NVS_READWRITE, &h), TAG, "nvs_open");
  esp_err_t err = nvs_erase_all(h);
  if (err == ESP_OK) err = nvs_commit(h);
  nvs_close(h);
  ESP_RETURN_ON_ERROR(err, TAG, "erase");
  g_cache = Settings{};
  return ESP_OK;
}

std::string defaultSsid() {
  uint8_t mac[6] = {};
  ESP_ERROR_CHECK(esp_read_mac(mac, ESP_MAC_WIFI_SOFTAP));
  char buf[16];
  std::snprintf(buf, sizeof buf, "Keyra-%02X%02X", mac[4], mac[5]);
  return buf;
}

std::string ssid(const Settings& s) { return s.wifiSsid.empty() ? defaultSsid() : s.wifiSsid; }

net::Home home(const Settings& s) { return {s.homeEnabled, s.homeSsid, s.homePassword, s.apMode}; }

}  // namespace keyra::settings
