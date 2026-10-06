#pragma once
// Trusted browsers on the device (SPEC §8.2): `kt` cookie handling, the
// trust_browser presence op, NVS persistence and the /api/trusted endpoints.
// Policy lives in trust.hpp.
#include <cstdint>
#include <string>

#include "esp_http_server.h"
#include "trust.hpp"

namespace keyra::api::trust {

esp_err_t load();  // reads NVS once at boot; a corrupt blob starts empty (logged)

// Id of the trusted browser whose `kt` cookie this request carries (lastSeen is
// updated), or 0. `token` receives the cookie value so the caller can renew it.
uint32_t recognise(httpd_req_t* r, std::string& token);

// Unlock through the home network from a browser not trusted yet: arms the
// trust_browser op (never displacing another item), gives the browser a fresh
// pending `kt` cookie and replies 202 — or 409 busy.
esp_err_t requestApproval(httpd_req_t* r);

esp_err_t sendList(httpd_req_t* r);                // GET /api/trusted
esp_err_t revoke(httpd_req_t* r, uint32_t id);     // DELETE /api/trusted/{id}

}  // namespace keyra::api::trust
