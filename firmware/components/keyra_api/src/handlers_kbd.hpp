#pragma once
// Keyboard endpoints (SPEC §10): layouts per output, the Layout Doctor,
// layout-proof generation and auto-type sequences. Kept out of handlers.cpp
// so the feature reads as one unit.
#include <string>

#include "actions.hpp"
#include "esp_http_server.h"
#include "json.hpp"
#include "keyra/hid.hpp"
#include "keyra/settings.hpp"
#include "keyra/vault.hpp"

namespace keyra::api::kbdapi {

// The layout of the computer an action types into.
hid::Layout layoutFor(const Target& t, const settings::Settings& s);

esp_err_t getKeyboard(httpd_req_t* r);  // GET /api/keyboard
// settings: layoutUsb, layoutBle, bothSequence.
void addSettings(cJSON* o, const settings::Settings& s);
// False when a keyboard field of PUT /api/settings is invalid; the 400 is then sent (result in `err`).
bool readSettings(httpd_req_t* r, const cJSON* body, settings::Settings& next, esp_err_t& err);
// POST /api/generate {layoutSafe?, layouts?}: the characters every chosen layout
// types with the same key press. False after sending a 400.
bool layoutSafeChars(httpd_req_t* r, const cJSON* body, bool& restrict, std::string& allowed, esp_err_t& err);

// POST /api/type {id, what:"sequence"}: the entry's sequence, else the
// "Both" default from settings, else the built-in one. False after sending an error.
bool sequenceRequest(httpd_req_t* r, const vault::Entry& e, const Target& target, actions::TypeRequest& out,
                     esp_err_t& err);
// pending: preview (masked), part and parts of a sequence.
void addPending(cJSON* p, const actions::TypeRequest& req);
// Entry JSON: the custom sequence ("" = none).
void addEntry(cJSON* o, const vault::Entry& e);

}  // namespace keyra::api::kbdapi
