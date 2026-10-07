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
// POST /api/type {text, repeat?, separator?, target?}: builds the request to arm.
// False when the body is invalid; the 400 is then already sent (its result in `err`).
bool textRequest(httpd_req_t* r, const cJSON* body, const Target& target, actions::TypeRequest& out, esp_err_t& err);
// pending.title / last.title: the entry title, or null for free text (no entry).
void addTitle(cJSON* o, actions::What what, const std::string& title);
// GET /api/entries/{id} → history:[{password, changedAt}], newest first.
// `withPasswords` false: only the dates (secrets not revealed, SPEC §10.3).
void addHistory(cJSON* o, const vault::Entry& e, bool withPasswords);

}  // namespace keyra::api::genapi
