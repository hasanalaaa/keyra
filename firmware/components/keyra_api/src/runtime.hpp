#pragma once
// Process-wide API state and the tasks that own the button and typing (SPEC §4).
#include <cstdint>

#include "actions.hpp"
#include "activity.hpp"
#include "esp_err.h"
#include "sessions.hpp"

namespace keyra::api {

actions::Machine& machine();
Sessions& sessions();

int64_t monoMs();
int64_t unixMs();
bool timeValid();
int64_t unixSecondsOrZero();  // for vault timestamps; 0 = unknown

// Locks the vault, ends every session and drops session-armed items; `why`
// goes in the activity log first (it cannot be written once locked).
void lockAll(activity::LockWhy why);
// Restarts the AP from the actions task ~3 s from now, so the HTTP reply that
// triggered it (or the client's next poll) still reaches the phone.
void reconfigureNetSoon();
// Restarts from the actions task ~2 s from now, so the client polling state
// sees the approved op's result first (an installed update).
void restartSoon();
// Waits (≤15 s) for GPIO0 to be released so the ROM does not latch download mode.
[[noreturn]] void safeRestart();

esp_err_t startTasks();

}  // namespace keyra::api
