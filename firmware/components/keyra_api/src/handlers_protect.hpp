#pragma once
// Presence-gated secrets and the recovery key (SPEC §12.2-12.3): a press of
// Keyra's button gives the session that asked a 60 s grace to read secrets,
// download a backup or change the recovery key.
#include <string>

#include "actions.hpp"
#include "esp_http_server.h"
#include "json.hpp"
#include "keyra/vault.hpp"

namespace keyra::api::protect {

// Whether this session may read secrets right now: protection is off or a
// press opened its grace window.
bool mayReveal(const std::string& token);
// Arms `op` (replacing whatever waits for the button); the press starts this
// session's grace. Sends 202 {awaiting:"button", op, expiresIn}.
esp_err_t requestPress(httpd_req_t* r, actions::Op op, const std::string& token);
// The entry as JSON: everything when `revealed`, else no password, 2FA secret
// or old passwords (hasPassword/hasTotp and history dates instead).
void addEntry(cJSON* o, const vault::Entry& e, bool revealed);

// 40 hex characters → the 20-byte key.
bool parseRecoveryKey(const std::string& hex, vault::RecoveryKey& out);

esp_err_t getRecovery(httpd_req_t* r);
esp_err_t createRecovery(httpd_req_t* r, const std::string& token);
esp_err_t deleteRecovery(httpd_req_t* r, const std::string& token);

}  // namespace keyra::api::protect
