#pragma once
// tokens.bin and tags.bin through the vault (SPEC §17, §18). A missing record
// is an empty store. A record this firmware cannot read — written by a newer
// firmware before a rollback, or damaged — is an error, never an empty store,
// and save() refuses to write over it: making a token or a tag must not
// destroy the ones a newer firmware keeps there.
#include <cstdint>
#include <utility>
#include <vector>

#include "tags.hpp"
#include "tokens.hpp"

namespace keyra::api::records {

// Absent: only from loadFor, which then read nothing.
enum class Status { Ok, Locked, Unsupported, Corrupt, Storage, Absent };

Status load(tokens::Store& out);
Status save(const tokens::Store& s);  // the stored record must be absent or readable
Status load(tags::Store& out);
Status save(const tags::Store& s);

// What a record held when it was last read or written in one vault unlock
// (F10), so a request naming a token or tag it does not hold is refused,
// against the shared budget, before any flash read or GCM open. Never trusted
// when stale (another unlock, nothing read yet): the record is read then.
template <typename Key>
class Known {
 public:
  void set(std::vector<Key> keys, uint32_t generation) {
    keys_ = std::move(keys);
    generation_ = generation;
    valid_ = true;
  }
  bool absent(const Key& k, uint32_t generation) const {
    if (!valid_ || generation != generation_) return false;
    bool found = false;
    for (const Key& x : keys_) found |= x == k;  // every one, like Store::find
    return !found;
  }

 private:
  std::vector<Key> keys_;
  uint32_t generation_ = 0;
  bool valid_ = false;
};
using KnownTokens = Known<tokens::Digest>;
using KnownTags = Known<std::pair<uint32_t, tags::Kind>>;  // id, kind: no keys or secrets

// After a successful load or save of `s` in the current unlock.
void remember(const tokens::Store& s, KnownTokens& known);
void remember(const tags::Store& s, KnownTags& known);
// load() for a request naming `key`, unless `known` says the record does not
// hold it: then Absent, with nothing read or decrypted. Refreshes `known`.
Status loadFor(const tokens::Digest& key, tokens::Store& out, KnownTokens& known);
Status loadFor(uint32_t id, tags::Kind kind, tags::Store& out, KnownTags& known);

// The API error for a failed load or save: "locked" (401), "unsupported_format",
// "corrupt", "storage" (500).
const char* code(Status s);
const char* message(Status s);

}  // namespace keyra::api::records
