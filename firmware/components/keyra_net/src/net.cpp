#include "keyra/net.hpp"

#include <cstring>

#include "dns_server.hpp"
#include "esp_check.h"
#include "esp_event.h"
#include "esp_log.h"
#include "esp_netif.h"
#include "esp_wifi.h"
#include "mdns.h"

namespace keyra::net {
namespace {

const char* TAG = "net";
bool g_started = false;

esp_err_t toWifiConfig(const Config& c, wifi_config_t& out) {
  const size_t ssidLen = c.ssid.size(), passLen = c.password.size();
  ESP_RETURN_ON_FALSE(ssidLen >= 1 && ssidLen <= sizeof out.ap.ssid, ESP_ERR_INVALID_ARG, TAG, "bad ssid length");
  // WPA2-PSK passphrase rule; an open AP is never acceptable for a password vault.
  ESP_RETURN_ON_FALSE(passLen >= 8 && passLen <= 63, ESP_ERR_INVALID_ARG, TAG, "bad password length");
  ESP_RETURN_ON_FALSE(c.channel >= 1 && c.channel <= 13, ESP_ERR_INVALID_ARG, TAG, "bad channel");
  out = {};
  std::memcpy(out.ap.ssid, c.ssid.data(), ssidLen);
  out.ap.ssid_len = static_cast<uint8_t>(ssidLen);
  std::memcpy(out.ap.password, c.password.data(), passLen);
  out.ap.channel = c.channel;
  out.ap.authmode = WIFI_AUTH_WPA2_PSK;
  out.ap.pairwise_cipher = WIFI_CIPHER_TYPE_CCMP;
  out.ap.max_connection = 4;
  out.ap.beacon_interval = 100;
  out.ap.pmf_cfg.required = false;
  return ESP_OK;
}

esp_err_t startMdns() {
  ESP_RETURN_ON_ERROR(mdns_init(), TAG, "mdns_init");
  ESP_RETURN_ON_ERROR(mdns_hostname_set("keyra"), TAG, "mdns hostname");
  ESP_RETURN_ON_ERROR(mdns_instance_name_set("Keyra"), TAG, "mdns instance");
  ESP_RETURN_ON_ERROR(mdns_service_add("Keyra", "_http", "_tcp", 80, nullptr, 0), TAG, "mdns service");
  return ESP_OK;
}

}  // namespace

esp_err_t start(const Config& c) {
  ESP_RETURN_ON_FALSE(!g_started, ESP_ERR_INVALID_STATE, TAG, "already started");
  wifi_config_t wc;
  ESP_RETURN_ON_ERROR(toWifiConfig(c, wc), TAG, "config");

  ESP_RETURN_ON_ERROR(esp_netif_init(), TAG, "netif init");
  const esp_err_t loop = esp_event_loop_create_default();
  ESP_RETURN_ON_FALSE(loop == ESP_OK || loop == ESP_ERR_INVALID_STATE, loop, TAG, "event loop");
  ESP_RETURN_ON_FALSE(esp_netif_create_default_wifi_ap() != nullptr, ESP_FAIL, TAG, "ap netif");

  wifi_init_config_t init = WIFI_INIT_CONFIG_DEFAULT();
  // Never load a Wi-Fi config persisted by earlier firmware: the AP must come up
  // exactly once, already secured, with the config below.
  init.nvs_enable = 0;
  ESP_RETURN_ON_ERROR(esp_wifi_init(&init), TAG, "wifi init");
  ESP_RETURN_ON_ERROR(esp_wifi_set_mode(WIFI_MODE_AP), TAG, "wifi mode");
  ESP_RETURN_ON_ERROR(esp_wifi_set_config(WIFI_IF_AP, &wc), TAG, "wifi config");
  // mDNS before the AP starts so it sees AP_START and binds to the AP netif.
  ESP_RETURN_ON_ERROR(startMdns(), TAG, "mdns");
  ESP_RETURN_ON_ERROR(esp_wifi_start(), TAG, "wifi start");
  const esp_err_t ps = esp_wifi_set_ps(WIFI_PS_NONE);
  if (ps != ESP_OK) ESP_LOGW(TAG, "set_ps(NONE): %s", esp_err_to_name(ps));
  ESP_RETURN_ON_ERROR(startDns(), TAG, "dns");

  g_started = true;
  ESP_LOGI(TAG, "AP \"%s\" up on channel %u", c.ssid.c_str(), c.channel);
  return ESP_OK;
}

esp_err_t reconfigure(const Config& c) {
  ESP_RETURN_ON_FALSE(g_started, ESP_ERR_INVALID_STATE, TAG, "not started");
  wifi_config_t wc;
  ESP_RETURN_ON_ERROR(toWifiConfig(c, wc), TAG, "config");
  // Stop first so the new credentials are in place before the AP beacons again.
  ESP_RETURN_ON_ERROR(esp_wifi_stop(), TAG, "wifi stop");
  ESP_RETURN_ON_ERROR(esp_wifi_set_config(WIFI_IF_AP, &wc), TAG, "wifi config");
  ESP_RETURN_ON_ERROR(esp_wifi_start(), TAG, "wifi start");
  ESP_LOGI(TAG, "AP reconfigured as \"%s\"", c.ssid.c_str());
  return ESP_OK;
}

int stations() {
  wifi_sta_list_t list{};
  return esp_wifi_ap_get_sta_list(&list) == ESP_OK ? list.num : 0;
}

}  // namespace keyra::net
