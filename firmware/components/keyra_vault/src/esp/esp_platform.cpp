// Device wiring: NVS counter, monotonic clock, PSRAM-preferring secure heap,
// and the Vault instance behind the public free functions.
#include "core/secure_buf.hpp"
#include "core/vault_core.hpp"
#include "esp_adapters.hpp"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "mbedtls/platform_util.h"
#include "nvs.h"
#include "nvs_flash.h"

namespace keyra::vault {

namespace esp {
namespace {

constexpr char kTag[] = "vault.nvs";
constexpr char kNamespace[] = "keyra_v";
constexpr char kKey[] = "fail";

// Idempotent. Never erases NVS: a broken NVS partition is the app's call.
bool nvsReady() {
  esp_err_t err = nvs_flash_init();
  if (err != ESP_OK) ESP_LOGE(kTag, "nvs_flash_init: %s", esp_err_to_name(err));
  return err == ESP_OK;
}

}  // namespace

bool NvsCounter::load(uint32_t& value) {
  value = 0;
  if (!nvsReady()) return false;
  nvs_handle_t h;
  esp_err_t err = nvs_open(kNamespace, NVS_READONLY, &h);
  if (err == ESP_ERR_NVS_NOT_FOUND) return true;  // namespace never written
  if (err != ESP_OK) return false;
  err = nvs_get_u32(h, kKey, &value);
  nvs_close(h);
  return err == ESP_OK || err == ESP_ERR_NVS_NOT_FOUND;
}

bool NvsCounter::store(uint32_t value) {
  if (!nvsReady()) return false;
  nvs_handle_t h;
  if (nvs_open(kNamespace, NVS_READWRITE, &h) != ESP_OK) return false;
  esp_err_t err = value ? nvs_set_u32(h, kKey, value) : nvs_erase_key(h, kKey);
  if (err == ESP_ERR_NVS_NOT_FOUND) err = ESP_OK;  // erasing an absent key
  if (err == ESP_OK) err = nvs_commit(h);
  nvs_close(h);
  if (err != ESP_OK) ESP_LOGE(kTag, "store counter: %s", esp_err_to_name(err));
  return err == ESP_OK;
}

uint64_t EspClock::monotonicMs() { return uint64_t(esp_timer_get_time()) / 1000; }

}  // namespace esp

void* mem::alloc(size_t n) {
  // Decrypted entries live in PSRAM when the board has it (it does not need to
  // be configured as malloc-able); otherwise internal RAM.
  void* p = heap_caps_malloc(n, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
  return p ? p : heap_caps_malloc(n, MALLOC_CAP_8BIT);
}

void mem::free(void* p) { heap_caps_free(p); }

void mem::zeroize(void* p, size_t n) { mbedtls_platform_zeroize(p, n); }

namespace detail {
Vault& instance() {
  static esp::LittleFsStorage storage;
  static esp::NvsCounter counter;
  static esp::PsaCrypto crypto;
  static esp::EspClock clock;
  static Vault vault(Platform{storage, counter, crypto, clock}, Vault::Options{});
  return vault;
}
}  // namespace detail

}  // namespace keyra::vault
