#pragma once
// Settings → Passkeys (docs/FIDO.md): the discoverable FIDO credentials stored
// in the vault. Session routes; the vault is unlocked whenever they run.
#include <cstdint>
#include <string>

#include "esp_http_server.h"

namespace keyra::api::fidoapi {

esp_err_t list(httpd_req_t* r);                 // GET /api/fido
// DELETE /api/fido/{id}: 202 {awaiting:"button", op:"delete_passkey", expiresIn,
// cancel}; the passkey is removed on the press (409 busy, 404 unknown id).
esp_err_t remove(httpd_req_t* r, uint32_t id, const std::string& token);

}  // namespace keyra::api::fidoapi
