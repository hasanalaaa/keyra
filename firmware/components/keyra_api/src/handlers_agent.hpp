#pragma once
// Access tokens and the Agent Gate (SPEC §17): /api/tokens (a session manages
// them) and /api/agent/… (an app or AI agent uses one). Token policy is in
// tokens.hpp; this file does the hashing, storage, HTTP and logging.
#include <optional>
#include <string>

#include "esp_http_server.h"
#include "json.hpp"
#include "routes.hpp"
#include "tokens.hpp"

namespace keyra::api::agent {

// Bearer check for /api/agent/…: the vault must be unlocked (401 locked), the
// token known (401 invalid_token) and within its rate (429 rate_limited). On
// refusal the reply is already sent (its result in `err`).
std::optional<tokens::Token> authenticate(httpd_req_t* r, esp_err_t& err);
esp_err_t dispatch(httpd_req_t* r, Route route, const tokens::Token& t, const cJSON* body);

esp_err_t listTokens(httpd_req_t* r);                                            // GET /api/tokens
esp_err_t createToken(httpd_req_t* r, const cJSON* body, const std::string& session);  // POST /api/tokens
esp_err_t deleteToken(httpd_req_t* r, uint32_t id);                              // DELETE /api/tokens/{id}

// The name of the token behind a pending-machine owner, or empty for a session.
std::string ownerName(const std::string& owner);

}  // namespace keyra::api::agent
