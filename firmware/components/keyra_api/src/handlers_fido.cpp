#include "handlers_fido.hpp"

#include <algorithm>
#include <vector>

#include "esp_log.h"
#include "http.hpp"
#include "keyra/fido.hpp"
#include "keyra/vault.hpp"
#include "runtime.hpp"

namespace keyra::api::fidoapi {
namespace {

const char* TAG = "api.fido";

esp_err_t failed(httpd_req_t* r, fido::Result res) {
  if (res == fido::Result::Locked) return http::sendError(r, http::k401, "locked", "Vault is locked");
  if (res == fido::Result::NotFound) return http::sendError(r, http::k404, "not_found", "No such passkey");
  ESP_LOGE(TAG, "passkey storage failed");
  return http::sendError(r, http::k500, "storage_error", "Passkey storage failed");
}

}  // namespace

esp_err_t list(httpd_req_t* r) {
  std::vector<fido::Passkey> keys;
  if (const fido::Result res = fido::list(keys); res != fido::Result::Ok) return failed(r, res);
  json::Ptr o(cJSON_CreateObject());
  cJSON* arr = cJSON_AddArrayToObject(o.get(), "passkeys");
  for (const fido::Passkey& k : keys) {
    cJSON* j = cJSON_CreateObject();
    cJSON_AddNumberToObject(j, "id", k.id);
    cJSON_AddStringToObject(j, "rpId", k.rpId.c_str());
    cJSON_AddStringToObject(j, "userName", k.userName.c_str());
    cJSON_AddStringToObject(j, "displayName", k.displayName.c_str());
    cJSON_AddNumberToObject(j, "created", static_cast<double>(k.created));
    cJSON_AddItemToArray(arr, j);
  }
  cJSON_AddNumberToObject(o.get(), "max", fido::kMaxPasskeys);
  return http::sendJson(r, http::k200, o.get());
}

esp_err_t remove(httpd_req_t* r, uint32_t id, const std::string& token) {
  std::vector<fido::Passkey> keys;
  if (const fido::Result res = fido::list(keys); res != fido::Result::Ok) return failed(r, res);
  if (std::none_of(keys.begin(), keys.end(), [id](const fido::Passkey& k) { return k.id == id; }))
    return failed(r, fido::Result::NotFound);
  // On the press. The vault may have locked, or the passkey gone, meanwhile:
  // fido::remove then fails and so does the op.
  const auto armed = machine().awaitPresence(
      actions::Op::DeletePasskey, [id] { return vault::unlocked() && fido::remove(id) == fido::Result::Ok; }, token);
  if (!armed) {
    return http::sendError(r, http::k409, "busy",
                           "Keyra is waiting for another request; long-press its button to cancel it");
  }
  json::Ptr o(cJSON_CreateObject());
  cJSON_AddStringToObject(o.get(), "awaiting", "button");
  cJSON_AddStringToObject(o.get(), "op", actions::opName(actions::Op::DeletePasskey));
  cJSON_AddStringToObject(o.get(), "cancel", armed->cancel.c_str());
  cJSON_AddNumberToObject(o.get(), "expiresIn", static_cast<double>(armed->expiresIn));
  return http::sendJson(r, http::k202, o.get());
}

}  // namespace keyra::api::fidoapi
