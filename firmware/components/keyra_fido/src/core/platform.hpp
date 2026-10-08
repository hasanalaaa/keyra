#pragma once
// Seams between the FIDO core and the device (PSA crypto, vault, NVS, the
// button) or the host tests (OpenSSL, fakes). Kept to what the core needs.
#include <cstddef>
#include <cstdint>
#include <vector>

namespace keyra::fido {

class Crypto {
 public:
  virtual ~Crypto() = default;
  virtual bool random(uint8_t* out, size_t n) = 0;
  virtual bool sha256(const uint8_t* msg, size_t n, uint8_t out[32]) = 0;
  // AES-256-GCM, 12-byte IV; out = ciphertext || 16-byte tag.
  virtual bool gcmSeal(const uint8_t key[32], const uint8_t iv[12], const uint8_t* aad, size_t aadLen,
                       const uint8_t* in, size_t n, uint8_t* out) = 0;
  // in = ciphertext || tag; false on a wrong key/AAD as well as on errors.
  virtual bool gcmOpen(const uint8_t key[32], const uint8_t iv[12], const uint8_t* aad, size_t aadLen,
                       const uint8_t* in, size_t n, uint8_t* out) = 0;
  // P-256: fresh key pair from the platform RNG; pub = 0x04 || X || Y.
  virtual bool p256Generate(uint8_t priv[32], uint8_t pub[65]) = 0;
  // ECDSA-SHA256 over msg; sig = r || s (32 bytes each, big-endian).
  virtual bool p256Sign(const uint8_t priv[32], const uint8_t* msg, size_t n, uint8_t sig[64]) = 0;
};

// The credential wrapping keys (docs/FIDO.md): key[0] wraps new credentials;
// the others came with restored backups. A credential ID opens under at most
// one of them (AES-GCM authenticates). Wiped when it goes out of scope.
struct WrapKeys {
  static constexpr size_t kMax = 4;
  uint8_t key[kMax][32] = {};
  size_t count = 0;
  WrapKeys() = default;
  WrapKeys(const WrapKeys&) = delete;
  WrapKeys& operator=(const WrapKeys&) = delete;
  ~WrapKeys() { clear(); }
  void clear() {
    for (volatile uint8_t* v = &key[0][0]; v != &key[0][0] + sizeof key; ++v) *v = 0;  // not a removable dead store
    count = 0;
  }
};

struct Record {
  uint32_t id = 0;
  std::vector<uint8_t> data;  // cred.hpp resident encoding
};

// Discoverable credentials and the wrapping keys, held by the vault.
class Store {
 public:
  enum class Result { Ok, Locked, Full, NotFound, Error };
  virtual ~Store() = default;
  virtual bool unlocked() = 0;
  virtual Result wrapKeys(WrapKeys& out) = 0;  // count ≥ 1 on Ok
  virtual Result list(std::vector<Record>& out) = 0;
  virtual Result put(uint32_t& id, const std::vector<uint8_t>& data) = 0;  // id 0 = new
  virtual Result remove(uint32_t id) = 0;
  virtual Result reset() = 0;  // all records and wrapping keys gone; a new key next time
};

// Global signature counter; next() persists the incremented value before returning it.
class Counter {
 public:
  virtual ~Counter() = default;
  virtual bool next(uint32_t& value) = 0;
};

// A restored backup carries the counter of the Keyra it came from, which may
// have signed again since. The counter jumps this far past the backup's so a
// site that checks it never sees it go backwards (docs/research/PASSKEY-BACKUP.md).
constexpr uint32_t kRestoreCounterMargin = 1000;
inline uint32_t restoredCounter(uint32_t local, uint32_t backup) {
  const uint32_t want = backup > UINT32_MAX - kRestoreCounterMargin ? UINT32_MAX : backup + kRestoreCounterMargin;
  return local > want ? local : want;  // never lowered
}

// Asking the person holding Keyra (implemented by Device, which keeps the
// USB channel alive with keepalives while it waits).
class User {
 public:
  enum class Answer { Approved, Denied, Timeout, Cancelled };
  virtual ~User() = default;
  virtual Answer waitUnlocked() = 0;  // returns at once when already unlocked
  virtual Answer waitPresence() = 0;  // a short press within 30 s
  // U2F has no keepalive: true when a press is waiting to be used (consumes
  // it); otherwise starts asking and returns false (the host polls again).
  virtual bool takePresence() = 0;
  virtual int64_t uptimeMs() = 0;
  virtual int64_t unixTime() = 0;  // seconds, 0 = unknown
};

}  // namespace keyra::fido
