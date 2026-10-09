// The vault state machine, independent of ESP-IDF. The public free functions in
// keyra/vault.hpp forward to one instance built from the device adapters.
//
// On-flash layout (paths relative to the vault filesystem root):
//
//   meta.bin   "KYR1" | u8 version=2 | u8 count | count × wrap        (SPEC §12.1)
//              wrap = u8 kind | u8 len | body[len]; each kind at most once.
//              Every wrap seals the same 32-byte DEK with AES-256-GCM:
//     kind 1 passphrase (required, len 80): u32 kdfIterations (LE) | salt[16]
//              | iv[12] | wrappedDEK[32] | tag[16]
//              KEK = PBKDF2-HMAC-SHA256(passphrase, salt, kdfIterations, 32),
//              AAD "keyra/meta/v1"
//     kind 2 recovery (optional, len 84): i64 created (unix s, LE) | salt[16]
//              | iv[12] | wrappedDEK[32] | tag[16]
//              KEK = HKDF-SHA256(ikm = 20-byte recovery key, salt,
//              info "keyra/recovery/v1"), AAD "keyra/wrap/recovery/v1"
//     kind 3 reserved for a device-bound wrap (eFuse HMAC key); not written yet.
//              Any other kind, a duplicate or a missing passphrase wrap is Corrupt:
//              firmware never drops a wrap it does not understand.
//              Version 1 (85 bytes: "KYR1" | 1 | the kind-1 body) is read as a
//              list of one passphrase wrap and rewritten as version 2 by the next
//              successful unlock.
//   e/<id>.bin u8 version=1 | iv[12] | ciphertext | tag[16]
//              <id> = 8 lowercase hex digits; AAD = "keyra/e/v1/" + <id>;
//              plaintext = entry_codec.hpp encoding
//   e/<id>.new a replace-restore's staged entry (same format as e/<id>.bin); never
//              read as an entry. Without restore.commit it is discarded at init/unlock,
//              as are f/<id>.new and fido.new.
//   restore.commit  a replace-restore has staged all of its entries (and passkeys):
//              v1  u8 1 | n × u32 id (LE)
//              v2  u8 2 | u32 n | n × u32 entry id | u32 m | m × u32 passkey id | u8 keys
//              (LE; written when the backup has a passkeys section). Finishing it
//              (init/unlock, idempotent): remove every e/<id>.bin whose id is not
//              listed, rename each e/<id>.new to e/<id>.bin; v2 does the same in f/,
//              then renames fido.new to fido.bin (keys > 0) or removes fido.bin
//              (keys = 0); then the marker goes. Either all old or all new.
//   f/<id>.bin passkey record, same format as e/<id>.bin with AAD "keyra/f/v1/" + <id>
//   f/<id>.new a replace-restore's staged passkey record (see e/<id>.new)
//   fido.bin   FIDO credential wrapping keys (vault_passkeys.cpp):
//              v1  u8 1 | salt[16]: one key, HMAC-SHA256(DEK, "keyra/fido/v1/wrap" || salt)
//              v2  u8 2 | iv[12] | AES-256-GCM(DEK, AAD "keyra/fido/v2/keys",
//                  u8 n | n × key[32]) | tag[16], 1 ≤ n ≤ 4, key[0] wraps new credentials.
//              v1 is kept until a restore adds a key, which writes v2.
//   fido.new   a replace-restore's staged fido.bin (v2)
//   fidopin.bin  the FIDO ClientPIN record, same format as e/<id>.bin with AAD
//              "keyra/fidopin/v1"; the plaintext is keyra_fido's (core/pin.hpp).
//              Removed with the passkeys (authenticatorReset, setup); restores keep it.
//   activity.bin  activity log (SPEC §15), same format as e/<id>.bin with AAD
//              "keyra/activity/v1"; the plaintext is keyra_api's encoding (vault_activity.cpp)
//   tokens.bin access tokens (SPEC §17), same format with AAD "keyra/tokens/v1";
//              the plaintext is keyra_api's encoding (tokens.cpp)
//   *.tmp      in-flight atomic writes (write tmp → close → rename); any found at
//              init are leftovers of an interrupted write and are deleted.
//
// Unlock throttling: the failure counter is persisted before the KDF runs, so
// pulling power mid-attempt still counts it. Delay after n consecutive failures
// is 0 for n ≤ 4, then 2^(n-4) s, capped at 900 s. The device has no RTC, so the
// window is measured on the monotonic clock; after a reboot the full delay for
// the stored count applies again from boot (a reboot can never shorten it).
#pragma once

#include <array>
#include <atomic>
#include <functional>
#include <mutex>
#include <vector>

#include "backup_format.hpp"
#include "keyra/vault.hpp"
#include "platform.hpp"
#include "secure_buf.hpp"

namespace keyra::vault {

uint32_t unlockDelayMs(uint32_t failures);

inline constexpr char kActivityPath[] = "activity.bin";
inline constexpr size_t kMaxActivity = kMaxActivityBytes;
inline constexpr char kTokensPath[] = "tokens.bin";

class Vault {
 public:
  struct Options {
    uint32_t kdfIterations = 0;  // 0: calibrate at setup to ≈1.2 s; tests pass a small count
  };
  static constexpr uint32_t kMinIterations = 60000, kMaxIterations = 2000000;

  Vault(Platform platform, Options options);
  ~Vault();
  Vault(const Vault&) = delete;
  Vault& operator=(const Vault&) = delete;

  Status init();
  bool initialized() const { return initialized_; }
  bool unlocked() const { return unlocked_; }
  // Bumped on every unlock: a session from an earlier unlock never comes back.
  uint32_t generation() const { return generation_; }
  Status setup(const std::string& passphrase);
  Status unlock(const std::string& passphrase, uint32_t* retryAfterMs);
  void lock();
  Status list(std::vector<Entry>& out);
  Status get(uint32_t id, Entry& out);
  Status put(Entry& e);
  Status remove(uint32_t id);
  Status touch(uint32_t id, int64_t now, bool password, bool* burned);
  Status changePassphrase(const std::string& cur, const std::string& next, uint32_t* retryAfterMs = nullptr);
  Status createRecovery(int64_t now, RecoveryKey& out);
  Status removeRecovery();
  RecoveryInfo recoveryInfo();
  Status recover(const RecoveryKey& key, const std::string& next, uint32_t* retryAfterMs);
  Status checkRecovery(const RecoveryKey& key, uint32_t* retryAfterMs);
  Status exportBackup(const std::string& backupPass, std::string& outJson, bool passkeys = false,
                      uint32_t counter = 0);
  Status importBackup(const std::string& backupPass, const std::string& json, bool replace,
                      size_t* added, size_t* updated, PasskeyRestore* passkeys = nullptr);
  Status checkBackup(const std::string& backupPass, const std::string& json, bool replace);
  Status factoryReset();

  // Passkey records (vault_passkeys.cpp); see keyra/vault.hpp.
  Status passkeyList(std::vector<PasskeyRecord>& out);
  Status passkeyPut(uint32_t& id, const std::vector<uint8_t>& data);
  Status passkeyRemove(uint32_t id);
  Status passkeyWrapKeys(uint8_t out[kMaxPasskeyWrapKeys][32], size_t& count);
  Status passkeyReset();
  bool fidoPinSet();
  Status fidoPinRead(std::vector<uint8_t>& out);
  Status fidoPinWrite(const std::vector<uint8_t>& data);

  // Activity log (vault_activity.cpp): one opaque record, encrypted like an entry.
  Status activityRead(std::vector<uint8_t>& out);
  Status activityWrite(const std::vector<uint8_t>& data);
  // Access tokens (vault_activity.cpp): one opaque record, encrypted like an entry.
  Status tokensRead(std::vector<uint8_t>& out);
  Status tokensWrite(const std::vector<uint8_t>& data);
  // Wrong passphrases / recovery keys tried before the last successful unlock.
  uint32_t failedBeforeUnlock() const { return failedBefore_; }

  Crypto& crypto() { return p_.crypto; }

 private:
  struct PassWrap {
    uint32_t iterations = 0;
    uint8_t salt[16] = {};
    uint8_t iv[12] = {};
    uint8_t wrapped[48] = {};  // DEK ciphertext || tag
  };
  struct RecoveryWrap {
    int64_t created = 0;
    uint8_t salt[16] = {};
    uint8_t iv[12] = {};
    uint8_t wrapped[48] = {};
  };
  struct Meta {
    uint8_t version = 2;  // as read from flash; 1 is migrated on unlock
    PassWrap pass;
    bool hasRecovery = false;
    RecoveryWrap recovery;
  };
  struct Slot {
    uint32_t id;
    SecureBuf plain;  // entry_codec encoding
  };
  using Key = std::array<uint8_t, 32>;
  using KeyList = std::vector<Key, ZeroingAllocator<Key>>;  // reserved to kMaxPasskeyWrapKeys: no regrowth
  // What a restore will change in the passkeys, settled before anything is written.
  struct PasskeyPlan {
    KeyList keys;      // merge: the wrap key list to write when writeKeys
    bool writeKeys = false;
    std::vector<const std::vector<uint8_t>*> records;  // merge: backup records to add
  };

  Status ready() const;  // init succeeded and storage is usable
  // One small record sealed with the DEK (activity log, tokens); an empty write removes it.
  Status sealedRead(const char* path, const char* aad, std::vector<uint8_t>& out);
  Status sealedWrite(const char* path, const char* aad, size_t max, const std::vector<uint8_t>& data);
  Status requireUnlocked() const;
  Status loadMeta();
  Status writeMeta(const Meta& m);
  Status writeAtomic(const std::string& path, const uint8_t* data, size_t n);
  Status removeAllEntryFiles();
  Status removeStaged();     // e/*.new left by a restore that never committed
  Status settleRestore();    // finish (marker present) or discard a replace-restore
  // Decrypt, parse and validate a backup into `out`; touches nothing.
  Status readBackup(const std::string& backupPass, const std::string& json, std::vector<Entry>& out,
                    backup::Passkeys& passkeys);
  uint32_t calibrateIterations();
  Status deriveKey(const std::string& pass, const uint8_t salt[16], uint32_t iters, Key& out);
  Status wrapDek(const std::string& pass, uint32_t iters, const Key& dek, PassWrap& out);
  Status recoveryKek(const RecoveryKey& key, const uint8_t salt[16], Key& out);
  // Rate limit + counter + unwrap. `open` derives a KEK and opens one wrap into
  // dek (Ok, WrongPassphrase or an error). Ok → dek holds the key.
  Status attempt(const std::function<Status(Key& dek)>& open, Key& dek, uint32_t* retryAfterMs);
  Status attempt(const std::string& pass, Key& dek, uint32_t* retryAfterMs);
  Status attempt(const RecoveryKey& key, Key& dek, uint32_t* retryAfterMs);
  Status openWrap(const Key& kek, const uint8_t iv[12], const char* aad, const uint8_t wrapped[48], Key& dek);
  Status finishUnlock(Key& dek);  // dek → dek_, entries loaded, v1 meta migrated
  Status loadEntries();
  Status persist(const std::string& path, uint32_t id, const SecureBuf& plain);
  Status store(Entry& rec);  // encode + persist + update RAM slot
  Slot* find(uint32_t id);
  bool newId(uint32_t& id);
  void wipeKeys();
  Status loadPasskeysLocked();  // lazily, on first passkey call after unlock
  Status removePasskeyFilesLocked();
  // fido.bin as a key list; empty when there is none and !create (else a v1 salt is made).
  Status readWrapKeysLocked(KeyList& out, bool create);
  Status writeWrapKeys(const char* path, const KeyList& keys);  // as fido.bin v2
  Status persistPasskey(const std::string& path, uint32_t id, const uint8_t* data, size_t n);
  // Restores (vault_passkeys.cpp). Plan: limits and dedup, writes nothing.
  Status planPasskeys(const backup::Passkeys& in, bool replace, PasskeyPlan& out);
  Status mergePasskeys(const PasskeyPlan& plan);
  // Replace: writes f/<id>.new and fido.new; ids gets the staged record ids.
  Status stagePasskeys(const backup::Passkeys& in, std::vector<uint32_t>& ids);
  Status removeStagedPasskeys();
  Status settlePasskeys(const std::vector<uint32_t>& keep, bool keys);  // part of settleRestore

  Platform p_;
  Options opt_;
  std::mutex m_;  // backed by FreeRTOS on device; std::atomic flags keep state polls lock-free
  std::atomic<bool> ready_{false}, initialized_{false}, unlocked_{false};
  std::atomic<uint32_t> generation_{0};
  Meta meta_;
  Key dek_{};
  std::vector<Slot, ZeroingAllocator<Slot>> slots_;
  std::vector<Slot, ZeroingAllocator<Slot>> passkeys_;
  bool passkeysLoaded_ = false;
  uint32_t failures_ = 0;
  std::atomic<uint32_t> failedBefore_{0};
  uint64_t lockedUntilMs_ = 0;
};

}  // namespace keyra::vault
