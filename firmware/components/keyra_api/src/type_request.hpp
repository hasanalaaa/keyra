#pragma once
// Building a type action from a request body, shared by POST /api/type (a
// session, SPEC §5) and POST /api/agent/type (an access token, SPEC §17), so
// both arm exactly the same thing. Implemented in handlers.cpp.
#include <string>

#include "actions.hpp"
#include "esp_http_server.h"
#include "json.hpp"

namespace keyra::api::typereq {

// `target` from the body ("usb" or a bonded device), else the device's default
// (SPEC §8.1). False when refused; the reply is then sent (its result in `err`).
bool readTarget(httpd_req_t* r, const cJSON* body, Target& out, esp_err_t& err);
// An entry action (username, password, both or totp — not a sequence) for
// entry `id`, with `submit` and `switchLang` from the body. False when refused
// (unknown entry, missing field, no clock for a code); the reply is then sent.
bool entryRequest(httpd_req_t* r, const cJSON* body, uint32_t id, actions::What what, const Target& target,
                  actions::TypeRequest& out, esp_err_t& err);
// The fields of a `pending` object (SPEC §5 Pending), plus `by` when an access
// token or an NFC tag armed it.
void addPending(cJSON* o, const actions::Pending& p);
// Where a type action can go (SPEC §17 GET /api/agent/targets): USB, Bluetooth,
// the default, the pairing window and the bonded hosts.
void addTargets(cJSON* o);
// Opens Bluetooth pairing after a press (presence op ble_pair, owned by `owner`):
// 202, or 409 ble_disabled / bonds_full, 503 ble_unavailable, 409 busy.
esp_err_t pairBle(httpd_req_t* r, const std::string& owner);

}  // namespace keyra::api::typereq
