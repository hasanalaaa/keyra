#pragma once
// An unusable NVS partition erased at boot (main.cpp, SPEC §5 "Settings storage
// reset"). The fact is kept in NVS until the next unlock logs it, shown in
// /api/state meanwhile, and the unlock answer says it once.
#include <optional>

#include "keyra/api.hpp"

namespace keyra::api::nvsreset {

std::optional<NvsErased> pending();  // erased and not logged yet
// At a successful unlock: the pending reset, now forgotten (the caller logs it).
std::optional<NvsErased> takeAtUnlock();
const char* name(NvsErased why);  // "no_free_pages" | "new_version"

}  // namespace keyra::api::nvsreset
