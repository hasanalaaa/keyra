#include "peer_store.hpp"

#include <algorithm>
#include <array>
#include <cstring>

#include "esp_log.h"
#include "nvs.h"
#include "policy.hpp"

namespace keyra::ble::peers {
namespace {

constexpr const char* TAG = "keyra_ble";
constexpr const char* kNamespace = "keyra_ble";
constexpr const char* kKey = "peers";
constexpr uint8_t kVersion = 1;
constexpr size_t kNameMax = 32;

struct Rec {
  uint8_t used;
  uint8_t type;
  uint8_t addr[6];
  int64_t lastSeen;
  char name[kNameMax + 1];
};

struct Blob {
  uint8_t version;
  Rec recs[kMaxBonds];
};

Blob s_blob{};

Rec* find(const Key& k) {
  for (Rec& r : s_blob.recs) {
    if (r.used && r.type == k.type && std::memcmp(r.addr, k.addr.data(), 6) == 0) return &r;
  }
  return nullptr;
}

// A new host takes a free record; with none free (records outlived their
// bonds) the stalest one goes — names are a convenience, keys are not here.
Rec& findOrAdd(const Key& k) {
  if (Rec* r = find(k)) return *r;
  Rec* slot = std::find_if(std::begin(s_blob.recs), std::end(s_blob.recs), [](const Rec& r) { return !r.used; });
  if (slot == std::end(s_blob.recs)) {
    slot = std::min_element(std::begin(s_blob.recs), std::end(s_blob.recs),
                            [](const Rec& a, const Rec& b) { return a.lastSeen < b.lastSeen; });
  }
  *slot = Rec{};
  slot->used = 1;
  slot->type = k.type;
  std::memcpy(slot->addr, k.addr.data(), 6);
  return *slot;
}

void save() {
  nvs_handle_t h;
  esp_err_t err = nvs_open(kNamespace, NVS_READWRITE, &h);
  if (err == ESP_OK) {
    err = nvs_set_blob(h, kKey, &s_blob, sizeof s_blob);
    if (err == ESP_OK) err = nvs_commit(h);
    nvs_close(h);
  }
  if (err != ESP_OK) ESP_LOGW(TAG, "saving host names: %s", esp_err_to_name(err));
}

}  // namespace

void load() {
  s_blob = Blob{};
  s_blob.version = kVersion;
  nvs_handle_t h;
  if (nvs_open(kNamespace, NVS_READONLY, &h) != ESP_OK) return;  // first boot
  Blob b{};
  size_t len = sizeof b;
  const esp_err_t err = nvs_get_blob(h, kKey, &b, &len);
  nvs_close(h);
  if (err == ESP_OK && len == sizeof b && b.version == kVersion) {
    for (Rec& r : b.recs) r.name[kNameMax] = '\0';
    s_blob = b;
  } else if (err != ESP_ERR_NVS_NOT_FOUND) {
    ESP_LOGW(TAG, "host names unreadable (%s); starting empty", esp_err_to_name(err));
  }
}

Peer get(const Key& k) {
  Peer p;
  p.addr = k.addr;
  if (const Rec* r = find(k)) {
    p.name = r->name;
    p.lastSeen = r->lastSeen;
  }
  return p;
}

void seen(const Key& k, int64_t unixSeconds) {
  const bool known = find(k) != nullptr;
  Rec& r = findOrAdd(k);
  if (known && (unixSeconds == 0 || unixSeconds == r.lastSeen)) return;  // spare the flash
  if (unixSeconds != 0) r.lastSeen = unixSeconds;
  save();
}

void named(const Key& k, const std::string& name) {
  const std::string fit = cleanName(name, kNameMax);
  Rec& r = findOrAdd(k);
  if (fit == r.name) return;
  std::memset(r.name, 0, sizeof r.name);
  std::memcpy(r.name, fit.data(), fit.size());
  save();
}

void forget(const Key& k) {
  if (Rec* r = find(k)) {
    *r = Rec{};
    save();
  }
}

void clear() {
  s_blob = Blob{};
  s_blob.version = kVersion;
  nvs_handle_t h;
  if (nvs_open(kNamespace, NVS_READWRITE, &h) != ESP_OK) return;
  const esp_err_t err = nvs_erase_all(h);
  if (err == ESP_OK) nvs_commit(h);
  nvs_close(h);
}

}  // namespace keyra::ble::peers
