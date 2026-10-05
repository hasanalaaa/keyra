#pragma once
// Process-wide API state and the tasks that own the button and typing (SPEC §4).
#include <cstdint>

#include "actions.hpp"
#include "esp_err.h"
#include "sessions.hpp"

namespace keyra::api {

actions::Machine& machine();
Sessions& sessions();

int64_t monoMs();
int64_t unixMs();
bool timeValid();
int64_t unixSecondsOrZero();  // for vault timestamps; 0 = unknown

// Locks the vault, ends every session and drops session-armed items.
void lockAll();
// Restarts the AP from the actions task ~3 s from now, so the HTTP reply that
// triggered it (or the client's next poll) still reaches the phone.
void reconfigureNetSoon();
// Waits (≤15 s) for GPIO0 to be released so the ROM does not latch download mode.
[[noreturn]] void safeRestart();

esp_err_t startTasks();

}  // namespace keyra::api
