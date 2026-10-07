#pragma once
// Settings → Passkeys (docs/FIDO.md): the discoverable FIDO credentials stored
// in the vault. Session routes; the vault is unlocked whenever they run.
#include <cstdint>

#include "esp_http_server.h"

namespace keyra::api::fidoapi {

esp_err_t list(httpd_req_t* r);                 // GET /api/fido
esp_err_t remove(httpd_req_t* r, uint32_t id);  // DELETE /api/fido/{id} → 204

}  // namespace keyra::api::fidoapi
