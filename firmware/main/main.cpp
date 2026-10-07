// Boot wiring only (SPEC §4): bring components up in dependency order.
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

void initNvs() {
  esp_err_t err = nvs_flash_init();
  if (err == ESP_ERR_NVS_NO_FREE_PAGES || err == ESP_ERR_NVS_NEW_VERSION_FOUND) {
    // The NVS layout itself is unusable (not just a missing key): start clean
    // rather than boot-loop. Vault data lives in LittleFS and is unaffected.
    ESP_LOGW(TAG, "NVS unusable (%s); erasing", esp_err_to_name(err));
    ESP_ERROR_CHECK(nvs_flash_erase());
    err = nvs_flash_init();
  }
  ESP_ERROR_CHECK(err);
}

}  // namespace

extern "C" void app_main() {
  initNvs();
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
  // A just-installed update proves itself here (SPEC §14): a crash before this
  // line, or a vault the old firmware could read and this one cannot, sends
  // the bootloader back to the previous firmware.
  keyra::api::confirmBoot(vs == keyra::vault::Status::Ok || vs == keyra::vault::Status::NotInitialized);
  ESP_LOGI(TAG, "Keyra up: http://keyra.local (internal heap free %u, largest block %u)",
           static_cast<unsigned>(heap_caps_get_free_size(MALLOC_CAP_INTERNAL)),
           static_cast<unsigned>(heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL)));
}
