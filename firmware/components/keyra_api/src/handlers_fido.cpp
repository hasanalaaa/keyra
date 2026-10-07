#include "handlers_fido.hpp"

#include <vector>

#include "esp_log.h"
#include "http.hpp"
#include "keyra/fido.hpp"

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

esp_err_t remove(httpd_req_t* r, uint32_t id) {
  const fido::Result res = fido::remove(id);
  return res == fido::Result::Ok ? http::sendEmpty(r, http::k204) : failed(r, res);
}

}  // namespace keyra::api::fidoapi
