// Keyra encrypted vault — public interface (docs/SPEC.md §4.1, security model §6).
//
// All functions are thread-safe (one internal mutex). Secrets returned through
// Entry copies are the caller's responsibility to wipe (see keyra::vault::wipe).
#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace keyra::vault {

// A password the entry used to have (SPEC §9.3). Kept inside the encrypted entry.
struct OldPassword {
  std::string password;
  int64_t changedAt = 0;  // unix seconds it was replaced (0 = unknown)
};

struct Entry {
  uint32_t id = 0;  // random non-zero, stable
  std::string title, url, username, password, totp /*otpauth URI or base32*/, notes;
  bool favorite = false;
  int64_t created = 0, updated = 0, lastUsed = 0;  // unix seconds (0 = unknown)
  std::vector<OldPassword> history;               // newest first, at most kMaxHistory
};

enum class Status {
  Ok,
  NotInitialized,
  AlreadyInitialized,
  Locked,
  WrongPassphrase,
  RateLimited,
  NotFound,
  Invalid,
  Full,
  StorageError,
  Corrupt
};

Status init();  // mount storage, load meta
bool initialized();
bool unlocked();
Status setup(const std::string& passphrase);  // creates meta + empty vault, leaves it unlocked
// retryAfterMs (may be null) is set on WrongPassphrase and RateLimited: how long
// until the next attempt is accepted (0 when no delay applies yet).
Status unlock(const std::string& passphrase, uint32_t* retryAfterMs);
void lock();  // wipes keys + decrypted entries
Status list(std::vector<Entry>& out);  // passwords/totp included; callers strip
Status get(uint32_t id, Entry& out);
// id==0 → create (assigns e.id); else update an existing entry (NotFound otherwise).
// The vault has no wall clock: timestamps are stored as the caller sets them,
// except that on update a zero created/lastUsed keeps the stored value.
// All string fields must be valid UTF-8 within the limits below (else Invalid).
// History is owned by the vault, so a client can neither forge nor erase it:
// e.history is ignored; a create starts empty, and an update that changes the
// password moves the stored one to the front (changedAt = e.updated), keeping
// the newest kMaxHistory.
Status put(Entry& e);
Status remove(uint32_t id);
Status touch(uint32_t id, int64_t now);  // lastUsed
Status changePassphrase(const std::string& cur, const std::string& next);
Status exportBackup(const std::string& backupPass, std::string& outJson);
Status importBackup(const std::string& backupPass, const std::string& json, bool replace,
                    size_t* added, size_t* updated);
Status factoryReset();  // erases everything vault-related

// Passkeys (keyra_fido, docs/FIDO.md). The vault stores each discoverable FIDO
// credential as an opaque record it encrypts like an entry ("f/<id>.bin",
// AES-256-GCM(DEK), AAD "keyra/f/v1/<id>") and derives the credential wrapping
// key from the DEK, so nothing FIDO-related is readable while locked. Records
// are not part of backups. All of these need the vault unlocked.
struct PasskeyRecord {
  uint32_t id = 0;
  std::vector<uint8_t> data;
};
inline constexpr size_t kMaxPasskeys = 50, kMaxPasskeyRecord = 1024;
Status passkeyList(std::vector<PasskeyRecord>& out);
// id == 0 → create (assigns id; Full beyond kMaxPasskeys); else replace (NotFound otherwise).
Status passkeyPut(uint32_t& id, const std::vector<uint8_t>& data);
Status passkeyRemove(uint32_t id);
// HMAC-SHA256(DEK, "keyra/fido/v1/wrap" || salt); the 16-byte salt is created on first use.
Status passkeyWrapKey(uint8_t out[32]);
// authenticatorReset: deletes every record and replaces the salt (old wrapped
// credentials stop decrypting).
Status passkeyReset();

const char* statusName(Status s);  // stable lowercase token, e.g. "wrong_passphrase"

// Overwrites a string's whole buffer (size and spare capacity) with zeros and
// clears it. Use on Entry copies that held secrets once you are done with them.
void wipe(std::string& s);
void wipe(Entry& e);

// Field limits in bytes (UTF-8); put()/importBackup() return Invalid beyond them.
inline constexpr size_t kMaxEntries = 1000, kMaxHistory = 10;
inline constexpr size_t kMaxTitle = 128, kMaxUrl = 512, kMaxUsername = 256, kMaxPassword = 256,
                        kMaxTotp = 512, kMaxNotes = 2048;
inline constexpr size_t kMinBackupPass = 12;

}  // namespace keyra::vault

namespace keyra::totp {
// RFC 6238. secretOrUri: `otpauth://totp/...` (secret, algorithm SHA1|SHA256|SHA512,
// digits 6|8, period 30|60) or bare base32 (spaces/lowercase ok, padding optional).
// out receives the NUL-terminated code. period/remaining may be null.
// Returns false for invalid input, HOTP URIs, or unixTime < 0.
bool code(const std::string& secretOrUri, int64_t unixTime, char out[11], int* period,
          int* remaining);
}  // namespace keyra::totp
