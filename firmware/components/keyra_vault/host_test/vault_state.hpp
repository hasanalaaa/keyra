// Everything an unlocked vault shows, for comparing a vault before and after a
// move or a rewrite (test_migrate.cpp, test_buckets.cpp), and a filler that
// uses every kind of file the vault writes.
#pragma once

#include <algorithm>
#include <map>
#include <string>
#include <vector>

#include "check.hpp"
#include "rig.hpp"

namespace keyra::vault::test {

struct VaultState {
  std::map<uint32_t, Entry> entries;
  std::vector<std::pair<uint32_t, std::vector<uint8_t>>> passkeys;
  std::vector<std::vector<uint8_t>> wrapKeys;
  std::vector<uint8_t> pin, activity, tokens, tags;
  uint32_t fidoCounter = 0;
  bool ok = false;
};

// The entries alone (reads nothing from flash).
inline std::map<uint32_t, Entry> entriesOf(Vault& v) {
  std::map<uint32_t, Entry> out;
  std::vector<Entry> all;
  CHECK(v.list(all) == Status::Ok);
  for (auto& e : all) out[e.id] = e;
  return out;
}

// Passkey wrap keys are read with create=true: a vault without fido.bin gets one.
inline VaultState stateOf(Vault& v) {
  VaultState s;
  std::vector<Entry> all;
  if (v.list(all) != Status::Ok) return s;
  for (auto& e : all) s.entries[e.id] = e;
  std::vector<PasskeyRecord> recs;
  if (v.passkeyList(recs) != Status::Ok) return s;
  for (auto& r : recs) s.passkeys.emplace_back(r.id, r.data);
  std::sort(s.passkeys.begin(), s.passkeys.end());
  uint8_t keys[kMaxPasskeyWrapKeys][32];
  size_t n = 0;
  if (v.passkeyWrapKeys(keys, n) != Status::Ok) return s;
  for (size_t i = 0; i < n; ++i) s.wrapKeys.emplace_back(keys[i], keys[i] + 32);
  const Status pin = v.fidoPinRead(s.pin);
  if (pin != Status::Ok && pin != Status::NotFound) return s;
  if (v.activityRead(s.activity) != Status::Ok || v.tokensRead(s.tokens) != Status::Ok ||
      v.tagsRead(s.tags) != Status::Ok || v.fidoCounterRead(s.fidoCounter) != Status::Ok)
    return s;
  s.ok = true;
  return s;
}

inline bool sameState(const VaultState& a, const VaultState& b) {
  if (!a.ok || !b.ok || a.entries.size() != b.entries.size()) return false;
  for (const auto& [id, e] : a.entries) {
    const auto it = b.entries.find(id);
    if (it == b.entries.end() || !same(e, it->second)) return false;
  }
  return a.passkeys == b.passkeys && a.wrapKeys == b.wrapKeys && a.pin == b.pin && a.activity == b.activity &&
         a.tokens == b.tokens && a.tags == b.tags && a.fidoCounter == b.fidoCounter;
}

// n entries of every shape (TOTP, long notes, history, sequence, burn-after,
// favourite), three passkeys and their wrap key, a FIDO PIN, the activity
// log, tokens, tags and the FIDO counter reservation. v must be unlocked.
inline void fillVault(Vault& v, size_t n) {
  for (size_t i = 0; i < n; ++i) {
    Entry e = sample("account " + std::to_string(i), "user" + std::to_string(i),
                     "https://site" + std::to_string(i % 97) + ".example");
    if (i % 3 == 0) e.totp = "otpauth://totp/Site:" + std::to_string(i) + "?secret=JBSWY3DPEHPK3PXP&issuer=Site";
    if (i % 5 == 0) e.notes = std::string(600, char('a' + i % 26));
    if (i % 7 == 0) e.sequence = "{USERNAME}{TAB}{PASSWORD}{ENTER}";
    if (i % 11 == 0) e.burnAfter = 3;
    e.favorite = i % 4 == 0;
    CHECK(v.put(e) == Status::Ok);
    if (i % 2 == 0) {  // an old password in the history
      e.password = "changed-" + std::to_string(i);
      e.updated += 100;
      CHECK(v.put(e) == Status::Ok);
    }
    wipe(e);
  }
  for (int i = 0; i < 3; ++i) {
    uint32_t id = 0;
    CHECK(v.passkeyPut(id, std::vector<uint8_t>(80 + i, uint8_t(0x40 + i))) == Status::Ok);
  }
  uint8_t keys[kMaxPasskeyWrapKeys][32];
  size_t nk = 0;
  CHECK(v.passkeyWrapKeys(keys, nk) == Status::Ok && nk == 1);  // creates fido.bin
  CHECK(v.fidoPinWrite({9, 8, 7, 6}) == Status::Ok);
  CHECK(v.activityWrite(std::vector<uint8_t>(300, 0x11)) == Status::Ok);
  CHECK(v.tokensWrite(std::vector<uint8_t>(120, 0x22)) == Status::Ok);
  CHECK(v.tagsWrite(std::vector<uint8_t>(64, 0x33)) == Status::Ok);
  CHECK(v.fidoCounterRaise(4096) == Status::Ok);
}

}  // namespace keyra::vault::test
