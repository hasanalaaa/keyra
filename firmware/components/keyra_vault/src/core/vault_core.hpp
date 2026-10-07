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
//   f/<id>.bin passkey record, same format as e/<id>.bin with AAD "keyra/f/v1/" + <id>
//   fido.bin   u8 version=1 | salt[16]   (FIDO wrapping-key salt, vault_passkeys.cpp)
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

#include "keyra/vault.hpp"
#include "platform.hpp"
#include "secure_buf.hpp"

namespace keyra::vault {

uint32_t unlockDelayMs(uint32_t failures);

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
  Status setup(const std::string& passphrase);
  Status unlock(const std::string& passphrase, uint32_t* retryAfterMs);
  void lock();
  Status list(std::vector<Entry>& out);
  Status get(uint32_t id, Entry& out);
  Status put(Entry& e);
  Status remove(uint32_t id);
  Status touch(uint32_t id, int64_t now);
  Status changePassphrase(const std::string& cur, const std::string& next);
  Status createRecovery(int64_t now, RecoveryKey& out);
  Status removeRecovery();
  RecoveryInfo recoveryInfo();
  Status recover(const RecoveryKey& key, const std::string& next, uint32_t* retryAfterMs);
  Status checkRecovery(const RecoveryKey& key, uint32_t* retryAfterMs);
  Status exportBackup(const std::string& backupPass, std::string& outJson);
  Status importBackup(const std::string& backupPass, const std::string& json, bool replace,
                      size_t* added, size_t* updated);
  Status factoryReset();

  // Passkey records (vault_passkeys.cpp); see keyra/vault.hpp.
  Status passkeyList(std::vector<PasskeyRecord>& out);
  Status passkeyPut(uint32_t& id, const std::vector<uint8_t>& data);
  Status passkeyRemove(uint32_t id);
  Status passkeyWrapKey(uint8_t out[32]);
  Status passkeyReset();

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

  Status ready() const;  // init succeeded and storage is usable
  Status requireUnlocked() const;
  Status loadMeta();
  Status writeMeta(const Meta& m);
  Status writeAtomic(const std::string& path, const uint8_t* data, size_t n);
  Status removeAllEntryFiles();
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
  Status persist(uint32_t id, const SecureBuf& plain);
  Status store(Entry& rec);  // encode + persist + update RAM slot
  Slot* find(uint32_t id);
  bool newId(uint32_t& id);
  void wipeKeys();
  Status loadPasskeysLocked();  // lazily, on first passkey call after unlock
  Status removePasskeyFilesLocked();

  Platform p_;
  Options opt_;
  std::mutex m_;  // backed by FreeRTOS on device; std::atomic flags keep state polls lock-free
  std::atomic<bool> ready_{false}, initialized_{false}, unlocked_{false};
  Meta meta_;
  Key dek_{};
  std::vector<Slot, ZeroingAllocator<Slot>> slots_;
  std::vector<Slot, ZeroingAllocator<Slot>> passkeys_;
  bool passkeysLoaded_ = false;
  uint32_t failures_ = 0;
  uint64_t lockedUntilMs_ = 0;
};

}  // namespace keyra::vault
