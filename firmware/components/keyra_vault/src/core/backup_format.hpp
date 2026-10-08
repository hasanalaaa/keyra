// Backup file format (version 3; versions 1 and 2 are still imported). Outer JSON, UTF-8:
//
//   {"format":"keyra-backup","v":3,
//    "kdf":{"alg":"pbkdf2-sha256","iter":N,"salt":"<b64 16 bytes>"},
//    "iv":"<b64 12 bytes>","data":"<b64 ciphertext||tag16>"}
//
// data = AES-256-GCM(key = PBKDF2-HMAC-SHA256(backupPass, salt, iter, 32 bytes),
//                    iv, no AAD, plaintext):
//   v3:    {"entries":[entry,…],
//           "passkeys":{"keys":["<b64 32 bytes>",…],      (≤ 4, distinct; [0] wraps new credentials)
//                       "records":["<b64>",…],             (≤ 50 opaque keyra_fido records, ≤ 1 KiB)
//                       "counter":n}}                      (keyra_fido signature counter, u32)
//           "passkeys" is absent when the owner left passkeys out (docs/research/PASSKEY-BACKUP.md).
//   v1/v2: [entry,…]
// entry:
//   {"id":n,"title":s,"url":s,"username":s,"password":s,"totp":s,"notes":s,
//    "favorite":b,"created":n,"updated":n,"lastUsed":n,
//    "history":[{"password":s,"changedAt":n},…],          (v2: newest first, ≤ 10)
//    "sequence":s}                                         (optional, keyra/sequence.hpp)
// v1 differs only in lacking "history". "sequence" is written only when set;
// older Keyra firmware ignores it (unknown member) and keeps the rest. On import, missing members default to
// empty/false/0 and unknown members are ignored (forward compatible); a member
// of the wrong type rejects the backup.
#pragma once

#include <array>
#include <string>
#include <vector>

#include "json.hpp"
#include "keyra/vault.hpp"

namespace keyra::vault::backup {

// Bounds import cost on a hostile file: no Keyra calibrates above 2M
// (Vault::kMaxIterations), and 10M kept the vault locked for minutes.
inline constexpr uint32_t kMaxIterations = 2000000;
inline constexpr int64_t kVersion = 3;                // written; 1 to 3 are read

struct Envelope {
  int64_t version = kVersion;
  uint32_t iterations = 0;
  uint8_t salt[16] = {};
  uint8_t iv[12] = {};
  std::vector<uint8_t> data;  // ciphertext || tag
};

std::string writeEnvelope(const Envelope& env);
bool readEnvelope(const std::string& text, Envelope& out);

void writeEntry(SecureString& out, const Entry& e);
// Type checks only; field limits are checked by the caller via codec::valid.
bool readEntry(const json::Value& v, Entry& out);

using WrapKey = std::array<uint8_t, 32>;
struct Passkeys {
  bool present = false;
  std::vector<WrapKey, ZeroingAllocator<WrapKey>> keys;
  std::vector<std::vector<uint8_t>> records;
  uint32_t counter = 0;
};
// Writes ,"passkeys":{…} (the caller writes the object around it).
void writePasskeys(SecureString& out, const Passkeys& p);
// v: the "passkeys" member. Types and the limits in the format comment above.
bool readPasskeys(const json::Value& v, Passkeys& out);

}  // namespace keyra::vault::backup
