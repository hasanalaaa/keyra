#pragma once
// Home Wi-Fi endpoints and the `net` part of /api/state (SPEC §8.2). Kept out
// of handlers.cpp so the network feature reads as one unit.
#include "esp_http_server.h"
#include "json.hpp"
#include "keyra/net.hpp"
#include "keyra/settings.hpp"

namespace keyra::api::netapi {

void addState(cJSON* state, net::Via via);                  // state.net
void addSettings(cJSON* o, const settings::Settings& s);    // homeWifi{enabled, ssid}, apMode — never the password
// Pushes the saved home Wi-Fi settings to keyra_net (after apMode or home changes).
void apply();
esp_err_t getScan(httpd_req_t* r);                          // GET /api/wifi/scan
esp_err_t putHome(httpd_req_t* r, const cJSON* body);       // PUT /api/wifi/home → 202 home_wifi

}  // namespace keyra::api::netapi
