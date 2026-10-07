#include "trusted.hpp"

#include <memory>
#include <mutex>

#include "esp_log.h"
#include "esp_random.h"
#include "http.hpp"
#include "nvs.h"
#include "psa/crypto.h"
#include "runtime.hpp"

namespace keyra::api::trust {
namespace {

const char* TAG = "trust";
constexpr const char* kNamespace = "keyra";  // erased with the rest on factory reset
constexpr const char* kKey = "trusted";
constexpr size_t kTokenHex = 64;
constexpr size_t kMaxUa = 512;

std::mutex g_mu;
Store g_store;

bool sha256(const std::string& token, Digest& out) {
  size_t len = 0;
  return psa_crypto_init() == PSA_SUCCESS &&
         psa_hash_compute(PSA_ALG_SHA_256, reinterpret_cast<const uint8_t*>(token.data()), token.size(), out.data(),
                          out.size(), &len) == PSA_SUCCESS &&
         len == out.size();
}

std::string randomToken() {
  uint8_t raw[kTokenHex / 2];
  esp_fill_random(raw, sizeof raw);
  static constexpr char kHex[] = "0123456789abcdef";
  std::string out(kTokenHex, '0');
  for (size_t i = 0; i < sizeof raw; ++i) {
    out[2 * i] = kHex[raw[i] >> 4];
    out[2 * i + 1] = kHex[raw[i] & 0x0F];
    raw[i] = 0;
  }
  return out;
}

// The `kt` cookie, only when it has the shape of one of our tokens.
std::string cookieToken(httpd_req_t* r) {
  char buf[kTokenHex + 1] = {};
  size_t len = sizeof buf;
  if (httpd_req_get_cookie_val(r, "kt", buf, &len) != ESP_OK) return {};
  std::string t(buf);
  return t.size() == kTokenHex && t.find_first_not_of("0123456789abcdef") == std::string::npos ? t : std::string();
}

// Caller holds g_mu.
esp_err_t persistLocked() {
  const std::vector<uint8_t> blob = g_store.serialize();
  nvs_handle_t h;
  esp_err_t err = nvs_open(kNamespace, NVS_READWRITE, &h);
  if (err != ESP_OK) return err;
  err = nvs_set_blob(h, kKey, blob.data(), blob.size());
  if (err == ESP_OK) err = nvs_commit(h);
  nvs_close(h);
  if (err != ESP_OK) ESP_LOGE(TAG, "saving trusted browsers: %s", esp_err_to_name(err));
  return err;
}

std::optional<size_t> findLocked(const std::string& token) {
  Digest d;
  if (token.empty() || !sha256(token, d)) return std::nullopt;
  return g_store.find(d);
}

bool commitTrust(const Browser& pending) {
  std::lock_guard<std::mutex> lock(g_mu);
  Browser b = pending;
  do {
    esp_fill_random(&b.id, sizeof b.id);
  } while (b.id == 0 || g_store.contains(b.id));
  const uint32_t evicted = g_store.add(b);
  if (evicted != 0) {
    sessions().endTrusted(evicted);
    ESP_LOGI(TAG, "trusted browser limit reached; forgot the least recently used one");
  }
  ESP_LOGI(TAG, "browser trusted: %s", b.name.c_str());
  return persistLocked() == ESP_OK;
}

}  // namespace

esp_err_t load() {
  nvs_handle_t h;
  esp_err_t err = nvs_open(kNamespace, NVS_READONLY, &h);
  if (err == ESP_ERR_NVS_NOT_FOUND) return ESP_OK;
  if (err != ESP_OK) return err;
  size_t len = 0;
  err = nvs_get_blob(h, kKey, nullptr, &len);
  std::vector<uint8_t> blob(len);
  if (err == ESP_OK && len > 0) err = nvs_get_blob(h, kKey, blob.data(), &len);
  nvs_close(h);
  if (err == ESP_ERR_NVS_NOT_FOUND) return ESP_OK;
  if (err != ESP_OK) return err;
  auto parsed = Store::parse(blob.data(), blob.size());
  std::lock_guard<std::mutex> lock(g_mu);
  if (!parsed) {
    // Fails safe: every home-network browser simply asks for the button again.
    ESP_LOGE(TAG, "stored trusted browsers unreadable; starting with none");
    return ESP_OK;
  }
  g_store = std::move(*parsed);
  return ESP_OK;
}

uint32_t recognise(httpd_req_t* r, std::string& token) {
  token = cookieToken(r);
  std::lock_guard<std::mutex> lock(g_mu);
  const auto i = findLocked(token);
  if (!i) return 0;
  const int64_t now = unixSecondsOrZero();
  if (now != 0) {
    g_store.touch(*i, now);
    persistLocked();
  }
  return g_store.all()[*i].id;
}

esp_err_t requestApproval(httpd_req_t* r) {
  const std::string token = randomToken();
  auto pending = std::make_shared<Browser>();
  if (!sha256(token, pending->hash)) return http::sendError(r, http::k500, "crypto", "Could not hash the token");
  pending->name = browserName(http::header(r, "User-Agent", kMaxUa));
  pending->created = pending->lastSeen = unixSecondsOrZero();
  const auto expires = machine().tryAwaitPresence(actions::Op::TrustBrowser, [pending] { return commitTrust(*pending); });
  if (!expires) {
    return http::sendError(r, http::k409, "busy",
                           "Keyra is waiting for another request; long-press its button to cancel it");
  }
  // Session cookie until approved; the successful unlock renews it for a year.
  const std::string cookie = "kt=" + token + "; HttpOnly; SameSite=Strict; Path=/";
  httpd_resp_set_hdr(r, "Set-Cookie", cookie.c_str());
  json::Ptr o(cJSON_CreateObject());
  cJSON_AddStringToObject(o.get(), "awaiting", "button");
  cJSON_AddStringToObject(o.get(), "op", "trust_browser");
  cJSON_AddStringToObject(o.get(), "cancel", machine().presenceCancelToken().c_str());
  cJSON_AddNumberToObject(o.get(), "expiresIn", static_cast<double>(*expires));
  return http::sendJson(r, http::k202, o.get());
}

esp_err_t sendList(httpd_req_t* r) {
  const std::string token = cookieToken(r);
  json::Ptr o(cJSON_CreateObject());
  cJSON* arr = cJSON_AddArrayToObject(o.get(), "browsers");
  {
    std::lock_guard<std::mutex> lock(g_mu);
    const auto current = findLocked(token);
    for (size_t i = 0; i < g_store.all().size(); ++i) {
      const Browser& b = g_store.all()[i];
      cJSON* j = cJSON_CreateObject();
      cJSON_AddNumberToObject(j, "id", b.id);
      cJSON_AddStringToObject(j, "name", b.name.c_str());
      cJSON_AddNumberToObject(j, "created", static_cast<double>(b.created));
      cJSON_AddNumberToObject(j, "lastSeen", static_cast<double>(b.lastSeen));
      cJSON_AddBoolToObject(j, "current", current && *current == i);
      cJSON_AddItemToArray(arr, j);
    }
  }
  return http::sendJson(r, http::k200, o.get());
}

esp_err_t revoke(httpd_req_t* r, uint32_t id) {
  bool wasCurrent = false;
  {
    std::lock_guard<std::mutex> lock(g_mu);
    const auto current = findLocked(cookieToken(r));
    wasCurrent = current && g_store.all()[*current].id == id;
    if (!g_store.remove(id)) return http::sendError(r, http::k404, "not_found", "No such trusted browser");
    if (persistLocked() != ESP_OK) return http::sendError(r, http::k500, "storage", "Could not save");
  }
  const size_t ended = sessions().endTrusted(id);
  ESP_LOGI(TAG, "browser trust revoked; %u session(s) ended", unsigned(ended));
  if (wasCurrent) httpd_resp_set_hdr(r, "Set-Cookie", "kt=; HttpOnly; SameSite=Strict; Path=/; Max-Age=0");
  return http::sendEmpty(r, http::k204);
}

}  // namespace keyra::api::trust
