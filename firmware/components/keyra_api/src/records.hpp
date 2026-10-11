#pragma once
// tokens.bin and tags.bin through the vault (SPEC §17, §18). A missing record
// is an empty store. A record this firmware cannot read — written by a newer
// firmware before a rollback, or damaged — is an error, never an empty store,
// and save() refuses to write over it: making a token or a tag must not
// destroy the ones a newer firmware keeps there.
#include <cstdint>

#include "tags.hpp"
#include "tokens.hpp"

namespace keyra::api::records {

enum class Status { Ok, Locked, Unsupported, Corrupt, Storage };

Status load(tokens::Store& out);
Status save(const tokens::Store& s);  // the stored record must be absent or readable
Status load(tags::Store& out);
Status save(const tags::Store& s);

// The API error for a failed load or save: "locked" (401), "unsupported_format",
// "corrupt", "storage" (500).
const char* code(Status s);
const char* message(Status s);

}  // namespace keyra::api::records
