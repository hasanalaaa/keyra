// A vault over host fakes that can be "rebooted" (new Vault, same storage/counter).
#pragma once

#include <cstdio>
#include <cstdlib>
#include <memory>

#include "core/bucket.hpp"
#include "core/vault_core.hpp"
#include "host_fakes.hpp"

namespace keyra::vault::test {

inline constexpr uint32_t kTestIterations = 1000;
inline const std::string kPass = "correct horse battery";

// Every vault test runs twice (CMakeLists.txt): on the entry-per-file layout of
// the "vault" partition, and with KEYRA_VAULT_LAYOUT=buckets on layout 2, the
// one a vault moved to the 16 MB tail uses (a storage with vault.home).
inline bool bucketLayout() {
  const char* v = std::getenv("KEYRA_VAULT_LAYOUT");
  return v && std::string(v) == "buckets";
}
inline std::vector<uint8_t> homeFile(const std::string& firmware = "0.0.0-test") {
  std::vector<uint8_t> b{'K', 'Y', 'R', 'H', 1, uint8_t(firmware.size())};
  b.insert(b.end(), firmware.begin(), firmware.end());
  return b;
}
inline void seedLayout(MemStorage& s) {
  if (bucketLayout()) s.files[kHomePath] = homeFile();
}

// An entry's sealed record (u8 1 | iv | ciphertext | tag) wherever the layout keeps it.
inline std::string entryFile(uint32_t id) {
  char b[16];
  std::snprintf(b, sizeof b, "e/%08x.bin", static_cast<unsigned>(id));
  return b;
}
inline bucket::Records bucketOf(const MemStorage& s, uint32_t id) {
  bucket::Records recs;
  const auto it = s.files.find(bucket::path(bucket::of(id)));
  if (it != s.files.end() && !bucket::parse(it->second, bucket::of(id), recs)) std::abort();
  return recs;
}
inline bool hasEntry(const MemStorage& s, uint32_t id) {
  if (!bucketLayout()) return s.files.count(entryFile(id)) == 1;
  for (const auto& r : bucketOf(s, id))
    if (r.id == id) return true;
  return false;
}
inline std::vector<uint8_t> entryRecord(const MemStorage& s, uint32_t id) {
  if (!bucketLayout()) return s.files.at(entryFile(id));
  for (const auto& r : bucketOf(s, id))
    if (r.id == id) return r.sealed;
  std::abort();
}
// Replaces (or adds) the record, keeping the bucket's framing valid.
inline void setEntryRecord(MemStorage& s, uint32_t id, const std::vector<uint8_t>& sealed) {
  if (!bucketLayout()) {
    s.files[entryFile(id)] = sealed;
    return;
  }
  bucket::Records recs = bucketOf(s, id);
  bool found = false;
  for (auto& r : recs)
    if (r.id == id) r.sealed = sealed, found = true;
  if (!found) recs.push_back(bucket::Record{id, sealed});
  s.files[bucket::path(bucket::of(id))] = bucket::encode(recs);
}

struct Rig {
  MemStorage storage;
  MemCounter counter;
  OpenSslCrypto crypto;
  FakeClock clock;
  std::unique_ptr<Vault> v;

  Rig() {
    seedLayout(storage);
    reboot();
  }
  Vault& reboot() {
    v.reset();
    storage.reboot();
    v = std::make_unique<Vault>(Platform{storage, counter, crypto, clock},
                                Vault::Options{kTestIterations});
    return *v;
  }
  Vault* operator->() { return v.get(); }
  // Fresh vault, set up with kPass and unlocked.
  static std::unique_ptr<Rig> ready() {
    auto r = std::make_unique<Rig>();
    if ((*r)->init() != Status::Ok || (*r)->setup(kPass) != Status::Ok) std::abort();
    return r;
  }
};

inline Entry sample(const std::string& title, const std::string& user = "hasan",
                    const std::string& url = "https://example.com") {
  Entry e;
  e.title = title;
  e.username = user;
  e.url = url;
  e.password = "pw-" + title;
  e.notes = "notes for " + title;
  e.created = 1700000000;
  e.updated = 1700000001;
  return e;
}

inline bool sameHistory(const Entry& a, const Entry& b) {
  if (a.history.size() != b.history.size()) return false;
  for (size_t i = 0; i < a.history.size(); ++i)
    if (a.history[i].password != b.history[i].password ||
        a.history[i].changedAt != b.history[i].changedAt)
      return false;
  return true;
}

inline bool same(const Entry& a, const Entry& b) {
  return a.id == b.id && a.title == b.title && a.url == b.url && a.username == b.username &&
         a.password == b.password && a.totp == b.totp && a.notes == b.notes &&
         a.favorite == b.favorite && a.created == b.created && a.updated == b.updated &&
         a.lastUsed == b.lastUsed && a.sequence == b.sequence && a.burnAfter == b.burnAfter && sameHistory(a, b);
}

}  // namespace keyra::vault::test
