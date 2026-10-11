#pragma once
// HTTP API, sessions and the pending-action/button state machine (SPEC §4, §5).
#include <cstdint>

#include "esp_err.h"

namespace keyra::api {
// Starts the actions/type tasks and the HTTP server. Call after io, vault, hid,
// settings and net are up.
esp_err_t start();
// Call once boot is complete. After an update's first boot: keep it when
// `healthy`, else go back to the previous firmware (SPEC §14).
void confirmBoot(bool healthy);
// The running firmware is confirmed (no automatic rollback can follow); the
// vault's one-time move to the 16 MB tail waits for this (SPEC §6.1).
bool imageConfirmed();
// main.cpp had to erase an unusable NVS partition at boot (SPEC §5 "Settings
// storage reset"). Call before start(): it is recorded in the fresh NVS, shown
// in /api/state and logged in the activity log at the next unlock.
enum class NvsErased : uint8_t { NoFreePages = 1, NewVersion = 2 };
void nvsErased(NvsErased why);
}
