#include "handlers_protect.hpp"

#include "esp_log.h"
#include "http.hpp"
#include "handlers_gen.hpp"
#include "keyra/settings.hpp"
#include "runtime.hpp"

namespace keyra::api::protect {
namespace {

const char* TAG = "protect";

int64_t graceLeft(const std::string& token) { return sessions().graceLeft(token, monoMs()); }

std::string toHex(const uint8_t* p, size_t n) {
  static constexpr char kHex[] = "0123456789abcdef";
  std::string out(2 * n, '0');
  for (size_t i = 0; i < n; ++i) {
    out[2 * i] = kHex[p[i] >> 4];
    out[2 * i + 1] = kHex[p[i] & 0x0F];
  }
  return out;
}

}  // namespace

bool mayReveal(const std::string& token) { return !settings::get().protectReveal || graceLeft(token) > 0; }

esp_err_t requestPress(httpd_req_t* r, actions::Op op, const std::string& token) {
  // The token is copied into the commit; it is a session id, not a secret of the vault.
  const int64_t expires = machine().awaitPresence(op, [token] { return sessions().grantGrace(token, monoMs()); });
  json::Ptr o(cJSON_CreateObject());
  cJSON_AddStringToObject(o.get(), "awaiting", "button");
  cJSON_AddStringToObject(o.get(), "op", actions::opName(op));
  cJSON_AddNumberToObject(o.get(), "expiresIn", static_cast<double>(expires));
  return http::sendJson(r, http::k202, o.get());
}

void addEntry(cJSON* o, const vault::Entry& e, bool revealed) {
  cJSON_AddNumberToObject(o, "id", e.id);
  cJSON_AddStringToObject(o, "title", e.title.c_str());
  cJSON_AddStringToObject(o, "url", e.url.c_str());
  cJSON_AddStringToObject(o, "username", e.username.c_str());
  cJSON_AddBoolToObject(o, "revealed", revealed);
  cJSON_AddBoolToObject(o, "hasPassword", !e.password.empty());
  cJSON_AddBoolToObject(o, "hasTotp", !e.totp.empty());
  if (revealed) {
    cJSON_AddStringToObject(o, "password", e.password.c_str());
    cJSON_AddStringToObject(o, "totp", e.totp.c_str());
  }
  cJSON_AddStringToObject(o, "notes", e.notes.c_str());
  cJSON_AddBoolToObject(o, "favorite", e.favorite);
  cJSON_AddNumberToObject(o, "created", static_cast<double>(e.created));
  cJSON_AddNumberToObject(o, "updated", static_cast<double>(e.updated));
  cJSON_AddNumberToObject(o, "lastUsed", static_cast<double>(e.lastUsed));
  genapi::addHistory(o, e, revealed);
}

bool parseRecoveryKey(const std::string& hex, vault::RecoveryKey& out) {
  if (hex.size() != 2 * out.size()) return false;
  for (size_t i = 0; i < hex.size(); ++i) {
    const char c = hex[i];
    const int v = c >= '0' && c <= '9' ? c - '0' : c >= 'a' && c <= 'f' ? c - 'a' + 10 : -1;
    if (v < 0) return false;
    out[i / 2] = static_cast<uint8_t>(i % 2 ? (out[i / 2] | v) : v << 4);
  }
  return true;
}

esp_err_t getRecovery(httpd_req_t* r) {
  const vault::RecoveryInfo info = vault::recoveryInfo();
  json::Ptr o(cJSON_CreateObject());
  cJSON_AddBoolToObject(o.get(), "enabled", info.enabled);
  cJSON_AddNumberToObject(o.get(), "created", static_cast<double>(info.created));
  return http::sendJson(r, http::k200, o.get());
}

// Always needs a press (whatever the reveal setting): the key opens the vault.
esp_err_t createRecovery(httpd_req_t* r, const std::string& token) {
  if (graceLeft(token) <= 0) return requestPress(r, actions::Op::Recovery, token);
  vault::RecoveryKey key{};
  const int64_t now = unixSecondsOrZero();
  const vault::Status st = vault::createRecovery(now, key);
  if (st != vault::Status::Ok) {
    ESP_LOGE(TAG, "create recovery key: %s", vault::statusName(st));
    return http::sendError(r, http::k500, "storage", "Could not save the recovery key");
  }
  json::Secret hex;
  hex.s = toHex(key.data(), key.size());
  for (volatile uint8_t& b : key) b = 0;
  json::Ptr o(cJSON_CreateObject());
  cJSON_AddStringToObject(o.get(), "recoveryKey", hex.s.c_str());
  cJSON_AddNumberToObject(o.get(), "created", static_cast<double>(now));
  return http::sendJson(r, http::k200, o.get());
}

esp_err_t deleteRecovery(httpd_req_t* r, const std::string& token) {
  if (graceLeft(token) <= 0) return requestPress(r, actions::Op::Recovery, token);
  const vault::Status st = vault::removeRecovery();
  if (st == vault::Status::NotFound) return http::sendError(r, http::k404, "not_found", "No recovery key");
  if (st != vault::Status::Ok) return http::sendError(r, http::k500, "storage", "Could not remove the recovery key");
  return http::sendEmpty(r, http::k204);
}

}  // namespace keyra::api::protect
