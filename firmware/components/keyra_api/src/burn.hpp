#pragma once
// Delete after typing (SPEC §16). A typing that uses up one of an entry's
// allowed typings takes that use from the vault — the count saved, or the
// entry deleted on its last one — before the first key is sent. If the vault
// cannot record it, nothing is typed: a one-time code must never go out while
// the vault would still allow it again.
#include <functional>

#include "actions.hpp"
#include "keyra/vault.hpp"

namespace keyra::api::burn {

struct Counted {
  bool used = false;    // the vault recorded this typing (its lastUsed too)
  bool burned = false;  // ... and it was the last one: the entry is gone
};

// counts: the job types the password of an entry with burnAfter > 0.
// takeUse: vault::touch(id, now, true, burned) for that entry.
// check: refuses (any code but Typed) before anything is used up, e.g. a
// character the layout cannot type or an empty field. type: sends the keys.
actions::Code typeCounted(bool counts, const std::function<vault::Status(bool* burned)>& takeUse,
                          const std::function<actions::Code()>& check, const std::function<actions::Code()>& type,
                          Counted& out);

}  // namespace keyra::api::burn
