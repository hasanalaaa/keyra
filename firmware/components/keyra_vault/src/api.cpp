// Public free functions (keyra/vault.hpp) forwarding to the single Vault built
// by the platform file (esp/platform_esp.cpp on device, host_test/host_platform.cpp).
#include "core/totp_core.hpp"
#include "core/vault_core.hpp"
#include "keyra/vault.hpp"

namespace keyra::vault {

namespace detail {
Vault& instance();  // defined by the platform file
}

Status init() { return detail::instance().init(); }
bool initialized() { return detail::instance().initialized(); }
bool unlocked() { return detail::instance().unlocked(); }
Status setup(const std::string& passphrase) { return detail::instance().setup(passphrase); }
Status unlock(const std::string& passphrase, uint32_t* retryAfterMs) {
  return detail::instance().unlock(passphrase, retryAfterMs);
}
void lock() { detail::instance().lock(); }
Status list(std::vector<Entry>& out) { return detail::instance().list(out); }
Status get(uint32_t id, Entry& out) { return detail::instance().get(id, out); }
Status put(Entry& e) { return detail::instance().put(e); }
Status remove(uint32_t id) { return detail::instance().remove(id); }
Status touch(uint32_t id, int64_t now) { return detail::instance().touch(id, now); }
Status changePassphrase(const std::string& cur, const std::string& next) {
  return detail::instance().changePassphrase(cur, next);
}
Status exportBackup(const std::string& backupPass, std::string& outJson) {
  return detail::instance().exportBackup(backupPass, outJson);
}
Status importBackup(const std::string& backupPass, const std::string& json, bool replace,
                    size_t* added, size_t* updated) {
  return detail::instance().importBackup(backupPass, json, replace, added, updated);
}
Status factoryReset() { return detail::instance().factoryReset(); }
Status passkeyList(std::vector<PasskeyRecord>& out) { return detail::instance().passkeyList(out); }
Status passkeyPut(uint32_t& id, const std::vector<uint8_t>& data) { return detail::instance().passkeyPut(id, data); }
Status passkeyRemove(uint32_t id) { return detail::instance().passkeyRemove(id); }
Status passkeyWrapKey(uint8_t out[32]) { return detail::instance().passkeyWrapKey(out); }
Status passkeyReset() { return detail::instance().passkeyReset(); }

const char* statusName(Status s) {
  switch (s) {
    case Status::Ok: return "ok";
    case Status::NotInitialized: return "not_initialized";
    case Status::AlreadyInitialized: return "already_initialized";
    case Status::Locked: return "locked";
    case Status::WrongPassphrase: return "wrong_passphrase";
    case Status::RateLimited: return "rate_limited";
    case Status::NotFound: return "not_found";
    case Status::Invalid: return "invalid";
    case Status::Full: return "full";
    case Status::StorageError: return "storage_error";
    case Status::Corrupt: return "corrupt";
  }
  return "unknown";
}

void wipe(std::string& s) {
  // Cover the spare capacity too: earlier, longer contents may still sit there.
  s.resize(s.capacity());
  if (!s.empty()) mem::zeroize(&s[0], s.size());
  s.clear();
}

void wipe(Entry& e) {
  for (std::string* f : {&e.title, &e.url, &e.username, &e.password, &e.totp, &e.notes}) wipe(*f);
  for (OldPassword& h : e.history) wipe(h.password);
  e = Entry{};
}

}  // namespace keyra::vault

namespace keyra::totp {
bool code(const std::string& secretOrUri, int64_t unixTime, char out[11], int* period,
          int* remaining) {
  return vault::totp::code(vault::detail::instance().crypto(), secretOrUri, unixTime, out, period,
                           remaining);
}
}  // namespace keyra::totp
