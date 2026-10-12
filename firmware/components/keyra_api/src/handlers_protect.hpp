#pragma once
// Presence-gated secrets and the recovery key (SPEC §12.2-12.3, §12.5b): a
// press of Keyra's button approves exactly the one request the session asked
// it for — reading one account's secrets, one backup download, one
// recovery-key change — and nothing else.
#include <string>

#include "actions.hpp"
#include "esp_http_server.h"
#include "json.hpp"
#include "keyra/vault.hpp"
#include "sessions.hpp"

namespace keyra::api::protect {

using Approval = Sessions::Approval;
using Purpose = Sessions::Approval::Purpose;

// Whether secrets may be shown without a press: "Protect reveal" is off.
bool unprotected();
// Uses up this session's approval for `want` (Sessions::redeem).
Sessions::Redeem redeem(const std::string& token, const Approval& want);
// Whether this session may download a backup now: protection is off, or a
// backup press is waiting to be used (used up here).
bool mayBackup(const std::string& token);
// Arms `op` (Reveal, Backup, Recovery, TokenCreate or TagCreate) for this
// session; the press approves `a` only (SPEC §12.5b). 202 {awaiting:"button",
// op, expiresIn, cancel, requestId}, or 409 busy while another session's item
// waits for the button.
esp_err_t requestPress(httpd_req_t* r, actions::Op op, const std::string& token, Approval a);
// A token or tag request body, reduced to what the press approves (SHA-256 of
// its JSON): the same body again redeems it, any other does not. Empty when it
// could not be made; the caller then refuses the request.
std::string bodyDigest(const cJSON* body);
// The entry as JSON: everything when `revealed`, else no password, 2FA secret
// or old passwords (hasPassword/hasTotp and history dates instead).
void addEntry(cJSON* o, const vault::Entry& e, bool revealed);

// 40 hex characters → the 20-byte key.
bool parseRecoveryKey(const std::string& hex, vault::RecoveryKey& out);

esp_err_t getRecovery(httpd_req_t* r);
esp_err_t createRecovery(httpd_req_t* r, const std::string& token);
esp_err_t deleteRecovery(httpd_req_t* r, const std::string& token);

}  // namespace keyra::api::protect
