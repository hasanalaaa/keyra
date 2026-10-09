// Keyra encrypted vault — public interface (docs/SPEC.md §4.1, security model §6).
//
// All functions are thread-safe (one internal mutex). Secrets returned through
// Entry copies are the caller's responsibility to wipe (see keyra::vault::wipe).
#pragma once

#include <array>
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
  std::string sequence;  // custom auto-type sequence (keyra/sequence.hpp); empty = none
  // Delete the entry after its password has been typed this many more times
  // (SPEC §16); 0 = keep. At most kMaxBurnAfter.
  uint8_t burnAfter = 0;
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
  Corrupt,
  PasskeysFull  // a restore would pass kMaxPasskeyWrapKeys or kMaxPasskeys
};

Status init();  // mount storage, load meta
bool initialized();
bool unlocked();
// Changes on every unlock (sessions are tied to the unlock they were made in).
uint32_t unlockGeneration();
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
// All string fields must be valid UTF-8 within the limits below, and `sequence`
// empty or valid per keyra/sequence.hpp (else Invalid).
// History is owned by the vault, so a client can neither forge nor erase it:
// e.history is ignored; a create starts empty, and an update that changes the
// password moves the stored one to the front (changedAt = e.updated), keeping
// the newest kMaxHistory.
Status put(Entry& e);
Status remove(uint32_t id);
// After typing: lastUsed = now (unless now is 0) and, when `password` was
// typed, one use off burnAfter; at zero the entry is deleted and *burned set.
Status touch(uint32_t id, int64_t now, bool password, bool* burned);
// A wrong `cur` is throttled like unlock; `retryAfterMs` as for unlock().
Status changePassphrase(const std::string& cur, const std::string& next, uint32_t* retryAfterMs = nullptr);

// Recovery key (SPEC §12.2): a random key from the hardware RNG that wraps a
// second copy of the DEK, so a forgotten passphrase is not the end of the data.
inline constexpr size_t kRecoveryKeyBytes = 20;  // 160 bits
using RecoveryKey = std::array<uint8_t, kRecoveryKeyBytes>;
struct RecoveryInfo {
  bool enabled = false;
  int64_t created = 0;  // unix seconds (0 = unknown)
};
// Unlocked only. Replaces any earlier recovery key; `out` is the only copy that
// ever leaves the vault (the caller shows it once and wipes it).
Status createRecovery(int64_t now, RecoveryKey& out);
Status removeRecovery();  // unlocked only; NotFound when there is none
RecoveryInfo recoveryInfo();
// Proves `key` (throttled and counted exactly like a wrong passphrase), then
// re-wraps the DEK under `next` (the old passphrase stops working) and leaves
// the vault unlocked. The recovery key stays valid. checkRecovery() only proves it.
Status recover(const RecoveryKey& key, const std::string& next, uint32_t* retryAfterMs);
Status checkRecovery(const RecoveryKey& key, uint32_t* retryAfterMs);

// passkeys: the backup also carries the passkey wrap keys, the passkey records
// and `counter`, keyra_fido's signature counter (docs/research/PASSKEY-BACKUP.md).
Status exportBackup(const std::string& backupPass, std::string& outJson, bool passkeys = false,
                    uint32_t counter = 0);
// What a restore did with a backup's passkeys section.
struct PasskeyRestore {
  size_t added = 0;       // records added
  bool present = false;   // the backup had a passkeys section
  uint32_t counter = 0;   // its signature counter (when present); the caller raises its own past it
};
// `passkeys` gets present/counter as soon as the backup is read, even when the
// restore then fails (it may have added passkeys, or finish at the next unlock).
// replace: all old entries or all new ones, even across a power cut. With a
// passkeys section the records and wrap keys become the backup's in the same
// commit; without one the local passkeys stay as they are.
// merge: an entry matches by id (with the same title, username and url) or else
// by title, username and url; the copy with the newer `updated` wins, and a
// replaced local password goes into history. The backup's wrap keys and records
// not already here are added; nothing local is removed. PasskeysFull (before
// any change) when the result would pass kMaxPasskeyWrapKeys or kMaxPasskeys.
Status importBackup(const std::string& backupPass, const std::string& json, bool replace,
                    size_t* added, size_t* updated, PasskeyRestore* passkeys = nullptr);
// The checks importBackup makes before writing (passphrase, format, limits); writes nothing.
Status checkBackup(const std::string& backupPass, const std::string& json, bool replace);
Status factoryReset();  // erases everything vault-related

// Passkeys (keyra_fido, docs/FIDO.md). The vault stores each discoverable FIDO
// credential as an opaque record it encrypts like an entry ("f/<id>.bin",
// AES-256-GCM(DEK), AAD "keyra/f/v1/<id>") and derives the credential wrapping
// keys under the DEK, so nothing FIDO-related is readable while locked. Records
// and wrap keys go into backups unless left out. All of these need the vault unlocked.
struct PasskeyRecord {
  uint32_t id = 0;
  std::vector<uint8_t> data;
};
inline constexpr size_t kMaxPasskeys = 50, kMaxPasskeyRecord = 1024, kMaxPasskeyWrapKeys = 4;
Status passkeyList(std::vector<PasskeyRecord>& out);
// id == 0 → create (assigns id; Full beyond kMaxPasskeys); else replace (NotFound otherwise).
Status passkeyPut(uint32_t& id, const std::vector<uint8_t>& data);
Status passkeyRemove(uint32_t id);
// The credential wrapping keys, at most kMaxPasskeyWrapKeys; out[0] wraps new
// credentials, the others came with restored backups. A fresh vault has one,
// HMAC-SHA256(DEK, "keyra/fido/v1/wrap" || salt), its 16-byte salt created on first use.
Status passkeyWrapKeys(uint8_t out[kMaxPasskeyWrapKeys][32], size_t& count);
// authenticatorReset: deletes every record, every wrap key (old wrapped
// credentials stop decrypting; the next call makes a fresh salt) and the FIDO PIN.
Status passkeyReset();
// The FIDO ClientPIN record (keyra_fido's encoding, at most kMaxFidoPinRecord
// bytes) in "fidopin.bin", AES-256-GCM(DEK), AAD "keyra/fidopin/v1". Read and
// write need the vault unlocked (NotFound when there is none; an empty write
// removes it). fidoPinSet() only checks that the file exists, so it also works
// while locked (getInfo has to say whether a PIN is set). Not part of backups.
inline constexpr size_t kMaxFidoPinRecord = 64;
bool fidoPinSet();
Status fidoPinRead(std::vector<uint8_t>& out);
Status fidoPinWrite(const std::vector<uint8_t>& data);

// Activity log (SPEC §15). One opaque record of at most kMaxActivityBytes,
// encrypted with the DEK ("activity.bin", AAD "keyra/activity/v1"); keyra_api
// owns its format. Unlocked only. An empty write removes it; it is not part of
// backups and goes with factory reset or a new setup.
inline constexpr size_t kMaxActivityBytes = 16 * 1024;
Status activityRead(std::vector<uint8_t>& out);  // empty when nothing was logged
Status activityWrite(const std::vector<uint8_t>& data);
// Access tokens (SPEC §17): like the activity log, one opaque record of at most
// kMaxTokensBytes ("tokens.bin", AAD "keyra/tokens/v1") that keyra_api owns.
// Not part of backups; kept across passphrase changes and restores; gone with
// a factory reset or a new setup.
inline constexpr size_t kMaxTokensBytes = 8 * 1024;
Status tokensRead(std::vector<uint8_t>& out);  // empty when there are none
Status tokensWrite(const std::vector<uint8_t>& data);
// NFC tap tags (SPEC §18): the same kind of record ("tags.bin", AAD
// "keyra/tags/v1"), holding each tag's secret hash or AES keys and counter.
// Not part of backups; kept across passphrase changes and restores; gone with
// a factory reset or a new setup.
inline constexpr size_t kMaxTagsBytes = 4 * 1024;
Status tagsRead(std::vector<uint8_t>& out);  // empty when there are none
Status tagsWrite(const std::vector<uint8_t>& data);
// Wrong passphrases or recovery keys tried before the latest successful unlock.
uint32_t failedBeforeUnlock();

const char* statusName(Status s);  // stable lowercase token, e.g. "wrong_passphrase"

// Overwrites a string's whole buffer (size and spare capacity) with zeros and
// clears it. Use on Entry copies that held secrets once you are done with them.
void wipe(std::string& s);
void wipe(Entry& e);

// Field limits in bytes (UTF-8); put()/importBackup() return Invalid beyond them.
inline constexpr size_t kMaxEntries = 1000, kMaxHistory = 10, kMaxBurnAfter = 99;
inline constexpr size_t kMaxTitle = 128, kMaxUrl = 512, kMaxUsername = 256, kMaxPassword = 256,
                        kMaxTotp = 512, kMaxNotes = 2048, kMaxSequence = 256;
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
