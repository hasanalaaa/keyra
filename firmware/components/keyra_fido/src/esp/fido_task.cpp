// Device wiring for keyra_fido: TinyUSB reports (via keyra_hid) → queue →
// FIDO task running core::Device; NVS signature counter; the shared TouchGate
// that keyra_api's button routing answers.
#include <sys/time.h>

#include <atomic>
#include <cstring>
#include <mutex>
#include <vector>

#include "core/device.hpp"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/task.h"
#include "keyra/fido.hpp"
#include "keyra/hid.hpp"
#include "nvs.h"
#include "psa_crypto.hpp"
#include "vault_store.hpp"

namespace keyra::fido {
namespace {

constexpr const char* TAG = "fido";
constexpr char kNvsNamespace[] = "keyra_fido";
constexpr char kNvsCounter[] = "ctr";
constexpr char kNvsAttKey[] = "att_key";
constexpr char kNvsAttCert[] = "att_cert";
// Requests are at most maxMsgSize (1200 B, getInfo) = 22 reports; hosts send
// one per 5 ms poll, the task drains them in microseconds unless it is signing.
constexpr int kQueueDepth = 32;
constexpr uint32_t kSendTimeoutMs = 100;

struct Packet {
  uint8_t b[hid::kPacket];
};

QueueHandle_t s_rx = nullptr;
TouchGate s_gate;

int64_t monoMs() { return esp_timer_get_time() / 1000; }

// USB task context: copy and hand over; a full queue drops the report (the
// host times out and retries, as on a lossy link).
void onReport(const uint8_t* report, size_t len) {
  if (len != hid::kPacket || !s_rx) return;
  Packet p;
  std::memcpy(p.b, report, hid::kPacket);
  xQueueSend(s_rx, &p, 0);
}

class UsbLink final : public Link {
 public:
  bool recv(uint8_t packet[hid::kPacket], uint32_t waitMs) override {
    Packet p;
    if (xQueueReceive(s_rx, &p, pdMS_TO_TICKS(waitMs)) != pdTRUE) return false;
    std::memcpy(packet, p.b, hid::kPacket);
    return true;
  }
  void send(const uint8_t packet[hid::kPacket]) override {
    if (!keyra::hid::fidoSend(packet, kSendTimeoutMs)) ESP_LOGW(TAG, "report not taken by the host");
  }
  void wink() override {
    // A wink asks "which key is this?": show the FIDO pattern for a moment.
    s_winkUntil = monoMs() + 1500;
  }
  int64_t nowMs() override { return monoMs(); }
  int64_t unixTime() override {
    timeval tv{};
    gettimeofday(&tv, nullptr);
    return tv.tv_sec > 1600000000 ? tv.tv_sec : 0;  // no RTC: before the clock is set, unknown
  }
  static inline std::atomic<int64_t> s_winkUntil{0};
};

// Serialises the counter's read-modify-write between the FIDO task (next) and
// keyra_api's backup/restore: a raise racing a next() could otherwise be undone.
std::mutex s_counterMutex;

bool readCounter(nvs_handle_t h, uint32_t& v) {
  v = 0;
  const esp_err_t err = nvs_get_u32(h, kNvsCounter, &v);
  return err == ESP_OK || err == ESP_ERR_NVS_NOT_FOUND;
}

// Monotonic signature counter in NVS, persisted before it is used.
class NvsCounter final : public Counter {
 public:
  bool next(uint32_t& value) override {
    std::lock_guard<std::mutex> g(s_counterMutex);
    nvs_handle_t h;
    if (nvs_open(kNvsNamespace, NVS_READWRITE, &h) != ESP_OK) return false;
    uint32_t v = 0;
    esp_err_t err = nvs_get_u32(h, kNvsCounter, &v);
    if (err == ESP_ERR_NVS_NOT_FOUND) err = ESP_OK;
    if (err == ESP_OK && v == UINT32_MAX) err = ESP_FAIL;  // never wraps
    if (err == ESP_OK) err = nvs_set_u32(h, kNvsCounter, v + 1);
    if (err == ESP_OK) err = nvs_commit(h);
    nvs_close(h);
    if (err != ESP_OK) {
      ESP_LOGE(TAG, "counter: %s", esp_err_to_name(err));
      return false;
    }
    value = v + 1;
    return true;
  }
};

// This Keyra's U2F attestation key and self-signed certificate (core/attest.hpp).
// Plain NVS: the key attests nothing beyond "this Keyra" and signs nothing a
// relying party trusts for security, so it is not wrapped by the vault.
class NvsAttestation final : public AttestationStore {
 public:
  bool load(uint8_t priv[32], std::vector<uint8_t>& cert) override {
    nvs_handle_t h;
    if (nvs_open(kNvsNamespace, NVS_READONLY, &h) != ESP_OK) return false;
    size_t keyLen = 32, certLen = 0;
    bool ok = nvs_get_blob(h, kNvsAttKey, priv, &keyLen) == ESP_OK && keyLen == 32 &&
              nvs_get_blob(h, kNvsAttCert, nullptr, &certLen) == ESP_OK && certLen > 0 && certLen <= 1024;
    if (ok) {
      cert.resize(certLen);
      ok = nvs_get_blob(h, kNvsAttCert, cert.data(), &certLen) == ESP_OK;
    }
    nvs_close(h);
    return ok;
  }
  bool save(const uint8_t priv[32], const std::vector<uint8_t>& cert) override {
    nvs_handle_t h;
    if (nvs_open(kNvsNamespace, NVS_READWRITE, &h) != ESP_OK) return false;
    esp_err_t err = nvs_set_blob(h, kNvsAttKey, priv, 32);
    if (err == ESP_OK) err = nvs_set_blob(h, kNvsAttCert, cert.data(), cert.size());
    if (err == ESP_OK) err = nvs_commit(h);
    nvs_close(h);
    if (err != ESP_OK) ESP_LOGE(TAG, "attestation: %s", esp_err_to_name(err));
    return err == ESP_OK;
  }
};

void fidoTask(void*) {
  static UsbLink link;
  static esp::PsaCrypto crypto;
  static VaultStore store;
  static NvsCounter counter;
  static NvsAttestation attestation;
  static Device device(link, crypto, store, counter, attestation, s_gate, {0, 1, 0});
  for (;;) device.step(50);
}

}  // namespace

bool start() {
  s_rx = xQueueCreate(kQueueDepth, sizeof(Packet));
  if (!s_rx) return false;
  keyra::hid::setFidoReceiver(onReport);
  // ECDSA/AES and CBOR only (no KDF); 8 KiB covers PSA signing plus parse trees.
  if (xTaskCreate(fidoTask, "fido", 8192, nullptr, 5, nullptr) != pdPASS) {
    keyra::hid::setFidoReceiver(nullptr);
    return false;
  }
  return true;
}

bool awaitingTouch() { return s_gate.awaiting(); }

bool ledActive() { return s_gate.awaiting() || monoMs() < UsbLink::s_winkUntil.load(); }

void press(bool shortPress) { s_gate.press(shortPress, monoMs()); }

bool signatureCounter(uint32_t& out) {
  std::lock_guard<std::mutex> g(s_counterMutex);
  nvs_handle_t h;
  out = 0;
  const esp_err_t err = nvs_open(kNvsNamespace, NVS_READONLY, &h);
  if (err == ESP_ERR_NVS_NOT_FOUND) return true;  // nothing signed yet: no namespace
  if (err != ESP_OK) return false;
  const bool ok = readCounter(h, out);
  nvs_close(h);
  return ok;
}

bool raiseCounterAfterRestore(uint32_t backupCounter) {
  std::lock_guard<std::mutex> g(s_counterMutex);
  nvs_handle_t h;
  if (nvs_open(kNvsNamespace, NVS_READWRITE, &h) != ESP_OK) return false;
  uint32_t v = 0;
  esp_err_t err = readCounter(h, v) ? ESP_OK : ESP_FAIL;
  const uint32_t next = restoredCounter(v, backupCounter);
  if (err == ESP_OK && next != v) err = nvs_set_u32(h, kNvsCounter, next);
  if (err == ESP_OK) err = nvs_commit(h);
  nvs_close(h);
  if (err != ESP_OK) ESP_LOGE(TAG, "counter raise: %s", esp_err_to_name(err));
  return err == ESP_OK;
}

bool forgetAttestation() {
  nvs_handle_t h;
  if (nvs_open(kNvsNamespace, NVS_READWRITE, &h) != ESP_OK) return false;
  esp_err_t a = nvs_erase_key(h, kNvsAttKey), b = nvs_erase_key(h, kNvsAttCert);
  if (a == ESP_ERR_NVS_NOT_FOUND) a = ESP_OK;
  if (b == ESP_ERR_NVS_NOT_FOUND) b = ESP_OK;
  const esp_err_t c = a == ESP_OK && b == ESP_OK ? nvs_commit(h) : ESP_FAIL;
  nvs_close(h);
  return c == ESP_OK;
}

}  // namespace keyra::fido
