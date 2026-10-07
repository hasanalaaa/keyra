#pragma once
// HTTP API, sessions and the pending-action/button state machine (SPEC §4, §5).
#include "esp_err.h"

namespace keyra::api {
// Starts the actions/type tasks and the HTTP server. Call after io, vault, hid,
// settings and net are up.
esp_err_t start();
// Call once boot is complete. After an update's first boot: keep it when
// `healthy`, else go back to the previous firmware (SPEC §14).
void confirmBoot(bool healthy);
}
