#pragma once
// v1.2 endpoints (SPEC §9.1–9.3): the hardware-RNG password generator, typing
// free text, and the password-history part of an entry. Kept out of
// handlers.cpp so the feature reads as one unit.
#include <string>

#include "actions.hpp"
#include "esp_http_server.h"
#include "json.hpp"
#include "keyra/vault.hpp"

namespace keyra::api::genapi {

esp_err_t postGenerate(httpd_req_t* r, const cJSON* body);  // POST /api/generate
esp_err_t postTypeText(httpd_req_t* r, const cJSON* body);  // POST /api/type {text, repeat?, separator?}
// pending.title / last.title: the entry title, or null for free text (no entry).
void addTitle(cJSON* o, actions::What what, const std::string& title);
// GET /api/entries/{id} → history:[{password, changedAt}], newest first.
void addHistory(cJSON* o, const vault::Entry& e);

}  // namespace keyra::api::genapi
