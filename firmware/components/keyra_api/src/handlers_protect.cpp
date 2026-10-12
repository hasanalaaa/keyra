#include "handlers_protect.hpp"

#include <cstring>

#include "entry_fields.hpp"
#include "esp_log.h"
#include "http.hpp"
#include "handlers_gen.hpp"
#include "keyra/settings.hpp"
#include "activity.hpp"
#include "psa/crypto.h"
#include "runtime.hpp"

namespace keyra::api::protect {
namespace {

const char* TAG = "protect";

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

bool unprotected() { return !settings::get().protectReveal; }

Sessions::Redeem redeem(const std::string& token, const Approval& want) {
  return sessions().redeem(token, monoMs(), want);
}

bool mayBackup(const std::string& token) {
  return unprotected() || redeem(token, {Purpose::Backup, 0, 0, {}}) == Sessions::Redeem::Ok;
}

std::string bodyDigest(const cJSON* body) {
  std::string out(32, '\0');
  char* text = cJSON_PrintUnformatted(body);
  size_t len = 0;
  const bool ok = text && psa_crypto_init() == PSA_SUCCESS &&
                  psa_hash_compute(PSA_ALG_SHA_256, reinterpret_cast<const uint8_t*>(text), std::strlen(text),
                                   reinterpret_cast<uint8_t*>(out.data()), out.size(), &len) == PSA_SUCCESS &&
                  len == out.size();
  cJSON_free(text);
  if (!ok) ESP_LOGE(TAG, "could not digest the request");
  return ok ? out : std::string();
}

esp_err_t requestPress(httpd_req_t* r, actions::Op op, const std::string& token, Approval a) {
  // The token is copied into the commit; it is a session id, not a secret of the vault.
  const auto armed = machine().awaitPresence(
      op, [token, a = std::move(a)] { return sessions().approve(token, monoMs(), a); }, token);
  if (!armed) {
    return http::sendError(r, http::k409, "busy",
                           "Keyra is waiting for another request; long-press its button to cancel it");
  }
  json::Ptr o(cJSON_CreateObject());
  cJSON_AddStringToObject(o.get(), "awaiting", "button");
  cJSON_AddStringToObject(o.get(), "op", actions::opName(op));
  cJSON_AddStringToObject(o.get(), "cancel", armed->cancel.c_str());
  cJSON_AddNumberToObject(o.get(), "expiresIn", static_cast<double>(armed->expiresIn));
  return http::sendJson(r, http::k202, o.get());
}

void addEntry(cJSON* o, const vault::Entry& e, bool revealed) {
  cJSON_AddNumberToObject(o, "id", e.id);
  for (const auto& [key, value] : entryStrings(e, revealed)) cJSON_AddStringToObject(o, key, value->c_str());
  cJSON_AddBoolToObject(o, "revealed", revealed);
  cJSON_AddBoolToObject(o, "hasPassword", !e.password.empty());
  cJSON_AddBoolToObject(o, "hasTotp", !e.totp.empty());
  cJSON_AddBoolToObject(o, "hasSequence", !e.sequence.empty());
  cJSON_AddBoolToObject(o, "hasNotes", !e.notes.empty());
  cJSON_AddBoolToObject(o, "favorite", e.favorite);
  cJSON_AddNumberToObject(o, "created", static_cast<double>(e.created));
  cJSON_AddNumberToObject(o, "updated", static_cast<double>(e.updated));
  cJSON_AddNumberToObject(o, "lastUsed", static_cast<double>(e.lastUsed));
  cJSON_AddNumberToObject(o, "burnAfter", e.burnAfter);
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

// Always needs its own press (whatever the reveal setting), used up by one
// change: the key opens the vault.
esp_err_t createRecovery(httpd_req_t* r, const std::string& token) {
  if (redeem(token, {Purpose::RecoveryCreate, 0, 0, {}}) != Sessions::Redeem::Ok)
    return requestPress(r, actions::Op::Recovery, token, {Purpose::RecoveryCreate, 0, 0, {}});
  vault::RecoveryKey key{};
  const int64_t now = unixSecondsOrZero();
  const vault::Status st = vault::createRecovery(now, key);
  if (st != vault::Status::Ok) {
    ESP_LOGE(TAG, "create recovery key: %s", vault::statusName(st));
    return http::sendError(r, http::k500, "storage", "Could not save the recovery key");
  }
  activity::log(activity::Kind::RecoveryCreated);
  json::Secret hex;
  hex.s = toHex(key.data(), key.size());
  for (volatile uint8_t& b : key) b = 0;
  json::Ptr o(cJSON_CreateObject());
  cJSON_AddStringToObject(o.get(), "recoveryKey", hex.s.c_str());
  cJSON_AddNumberToObject(o.get(), "created", static_cast<double>(now));
  return http::sendJson(r, http::k200, o.get());
}

esp_err_t deleteRecovery(httpd_req_t* r, const std::string& token) {
  if (redeem(token, {Purpose::RecoveryRemove, 0, 0, {}}) != Sessions::Redeem::Ok)
    return requestPress(r, actions::Op::Recovery, token, {Purpose::RecoveryRemove, 0, 0, {}});
  const vault::Status st = vault::removeRecovery();
  if (st == vault::Status::NotFound) return http::sendError(r, http::k404, "not_found", "No recovery key");
  if (st != vault::Status::Ok) return http::sendError(r, http::k500, "storage", "Could not remove the recovery key");
  activity::log(activity::Kind::RecoveryRemoved);
  return http::sendEmpty(r, http::k204);
}

}  // namespace keyra::api::protect
