// A vault over host fakes that can be "rebooted" (new Vault, same storage/counter).
#pragma once

#include <memory>

#include "core/vault_core.hpp"
#include "host_fakes.hpp"

namespace keyra::vault::test {

inline constexpr uint32_t kTestIterations = 1000;
inline const std::string kPass = "correct horse battery";

struct Rig {
  MemStorage storage;
  MemCounter counter;
  OpenSslCrypto crypto;
  FakeClock clock;
  std::unique_ptr<Vault> v;

  Rig() { reboot(); }
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
         a.lastUsed == b.lastUsed && sameHistory(a, b);
}

}  // namespace keyra::vault::test
