#pragma once
// Firmware updates over the network (SPEC §14). An image reaches the idle app
// partition either uploaded from the phone (POST /api/update) or fetched by
// Keyra itself from the latest GitHub release (POST /api/update/download).
// Either way it is checked (Keyra image, signed with the running firmware's
// key, not older) and "staged"; POST /api/update/apply then waits for the
// button and switches to it.
#include "cJSON.h"
#include "esp_err.h"
#include "esp_http_server.h"

namespace keyra::api::update {

esp_err_t upload(httpd_req_t* r);    // slow worker: streams the request body
esp_err_t check(httpd_req_t* r);     // slow worker: asks GitHub for the latest release
esp_err_t download(httpd_req_t* r);  // starts the background download, 202
esp_err_t apply(httpd_req_t* r);     // staged image → presence op `update`, 202

// Biggest image accepted (one app partition).
size_t maxImage();
// `update` object for GET /api/state (sessions only).
void addState(cJSON* state);
// After a boot into a just-installed image: keep it when `healthy`, otherwise
// mark it invalid and reboot into the previous firmware. No-op on a normal boot.
void confirmBoot(bool healthy);

}  // namespace keyra::api::update
