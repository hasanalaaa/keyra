#include "nvs_reset.hpp"

#include <mutex>

#include "esp_log.h"
#include "nvs.h"

namespace keyra::api {
namespace {

const char* TAG = "nvsreset";
constexpr char kNamespace[] = "keyra_sys";
constexpr char kKey[] = "nvsReset";

std::mutex g_mu;
bool g_loaded = false;
std::optional<NvsErased> g_pending;

std::optional<NvsErased> valid(uint8_t v) {
  if (v == static_cast<uint8_t>(NvsErased::NoFreePages)) return NvsErased::NoFreePages;
  if (v == static_cast<uint8_t>(NvsErased::NewVersion)) return NvsErased::NewVersion;
  return std::nullopt;
}

// Caller holds g_mu.
void loadLocked() {
  if (g_loaded) return;
  g_loaded = true;
  nvs_handle_t h;
  if (nvs_open(kNamespace, NVS_READONLY, &h) != ESP_OK) return;  // never recorded
  uint8_t v = 0;
  if (nvs_get_u8(h, kKey, &v) == ESP_OK) g_pending = valid(v);
  nvs_close(h);
}

}  // namespace

void nvsErased(NvsErased why) {
  std::lock_guard<std::mutex> lock(g_mu);
  g_loaded = true;
  g_pending = why;
  nvs_handle_t h;
  esp_err_t err = nvs_open(kNamespace, NVS_READWRITE, &h);
  if (err == ESP_OK) {
    err = nvs_set_u8(h, kKey, static_cast<uint8_t>(why));
    if (err == ESP_OK) err = nvs_commit(h);
    nvs_close(h);
  }
  // Still shown and logged from RAM until the next restart.
  if (err != ESP_OK) ESP_LOGE(TAG, "recording the NVS reset: %s", esp_err_to_name(err));
}

namespace nvsreset {

std::optional<NvsErased> pending() {
  std::lock_guard<std::mutex> lock(g_mu);
  loadLocked();
  return g_pending;
}

std::optional<NvsErased> takeAtUnlock() {
  std::lock_guard<std::mutex> lock(g_mu);
  loadLocked();
  const std::optional<NvsErased> was = g_pending;
  if (!was) return was;
  g_pending.reset();
  nvs_handle_t h;
  esp_err_t err = nvs_open(kNamespace, NVS_READWRITE, &h);
  if (err == ESP_OK) {
    err = nvs_erase_key(h, kKey);
    if (err == ESP_ERR_NVS_NOT_FOUND) err = ESP_OK;
    if (err == ESP_OK) err = nvs_commit(h);
    nvs_close(h);
  }
  if (err != ESP_OK) ESP_LOGE(TAG, "forgetting the NVS reset: %s", esp_err_to_name(err));
  return was;
}

const char* name(NvsErased why) { return why == NvsErased::NewVersion ? "new_version" : "no_free_pages"; }

}  // namespace nvsreset
}  // namespace keyra::api
