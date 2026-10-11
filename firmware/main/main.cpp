// Boot wiring only (SPEC §4): bring components up in dependency order.
#include <optional>

#include "esp_heap_caps.h"
#include "esp_log.h"
#include "keyra/ble.hpp"
#include "keyra/api.hpp"
#include "keyra/fido.hpp"
#include "keyra/hid.hpp"
#include "keyra/io.hpp"
#include "keyra/net.hpp"
#include "keyra/settings.hpp"
#include "keyra/vault.hpp"
#include "nvs_flash.h"
#include "sdkconfig.h"

namespace {

const char* TAG = "main";

#if defined(CONFIG_KEYRA_DEV_CDC)
constexpr bool kDevCdc = true;
#else
constexpr bool kDevCdc = false;
#endif

// Settings, Bluetooth bonds, trusted browsers, the unlock-failure count and
// the FIDO counter's NVS copy live in NVS; nothing boots without it. When the
// partition itself is unusable (full, or written by a newer NVS format) it is
// erased, never silently (SPEC §5 "Settings storage reset"): the vault assumes
// the longest unlock delay, the FIDO counter carries on from its vault
// reservation, and the owner is told in /api/state and the activity log.
// Vault data lives in LittleFS and is unaffected. Returns why, if erased.
std::optional<keyra::api::NvsErased> initNvs() {
  esp_err_t err = nvs_flash_init();
  std::optional<keyra::api::NvsErased> erased;
  if (err == ESP_ERR_NVS_NO_FREE_PAGES || err == ESP_ERR_NVS_NEW_VERSION_FOUND) {
    ESP_LOGE(TAG, "NVS unusable (%s); erasing it", esp_err_to_name(err));
    erased = err == ESP_ERR_NVS_NEW_VERSION_FOUND ? keyra::api::NvsErased::NewVersion
                                                   : keyra::api::NvsErased::NoFreePages;
    ESP_ERROR_CHECK(nvs_flash_erase());
    err = nvs_flash_init();
  }
  ESP_ERROR_CHECK(err);
  return erased;
}

}  // namespace

extern "C" void app_main() {
  const auto nvsErased = initNvs();
  if (nvsErased) {
    keyra::api::nvsErased(*nvsErased);
    const keyra::vault::Status es = keyra::vault::nvsErased();
    if (es != keyra::vault::Status::Ok) ESP_LOGE(TAG, "keeping the unlock delay: %s", keyra::vault::statusName(es));
  }
  keyra::io::init();
  // A vault that fails to mount must not stop the device: the API reports it and
  // factory reset (button-gated) stays reachable.
  const keyra::vault::Status vs = keyra::vault::init();
  if (vs != keyra::vault::Status::Ok) ESP_LOGE(TAG, "vault init: %s", keyra::vault::statusName(vs));
  keyra::hid::init(kDevCdc);
  // The security key is an extra: typing keeps working if its task cannot start.
  if (!keyra::fido::start()) ESP_LOGE(TAG, "FIDO security key unavailable");

  ESP_ERROR_CHECK(keyra::settings::load());
  const keyra::settings::Settings s = keyra::settings::get();
  keyra::io::brightness(s.ledBrightness);
  ESP_ERROR_CHECK(keyra::net::start({keyra::settings::ssid(s), s.wifiPassword, 6}, keyra::settings::home(s)));
  // Bluetooth is an extra: if it cannot start, Keyra keeps typing over USB.
  const esp_err_t be = keyra::ble::init(
      s.deviceName, s.bleEnabled,
      s.bleConnect == keyra::settings::BleConnect::Always ? keyra::ble::Connect::Always : keyra::ble::Connect::OnDemand);
  if (be != ESP_OK) ESP_LOGE(TAG, "Bluetooth unavailable: %s", esp_err_to_name(be));
  ESP_ERROR_CHECK(keyra::api::start());
  // A just-installed update starts its probation here (SPEC §14): a crash
  // before it ends, or a vault the old firmware could read and this one
  // cannot, sends the bootloader back to the previous firmware. A storage
  // (flash/mount) error counts as healthy: going back cannot fix the hardware.
  keyra::api::confirmBoot(vs == keyra::vault::Status::Ok || vs == keyra::vault::Status::NotInitialized ||
                          vs == keyra::vault::Status::StorageError);
  ESP_LOGI(TAG, "Keyra up: http://keyra.local (internal heap free %u, largest block %u)",
           static_cast<unsigned>(heap_caps_get_free_size(MALLOC_CAP_INTERNAL)),
           static_cast<unsigned>(heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL)));
}
