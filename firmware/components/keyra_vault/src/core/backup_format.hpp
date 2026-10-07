// Backup file format (version 2; version 1 files are still imported). Outer JSON, UTF-8:
//
//   {"format":"keyra-backup","v":2,
//    "kdf":{"alg":"pbkdf2-sha256","iter":N,"salt":"<b64 16 bytes>"},
//    "iv":"<b64 12 bytes>","data":"<b64 ciphertext||tag16>"}
//
// data = AES-256-GCM(key = PBKDF2-HMAC-SHA256(backupPass, salt, iter, 32 bytes),
//                    iv, no AAD, plaintext = JSON array of entry objects):
//   {"id":n,"title":s,"url":s,"username":s,"password":s,"totp":s,"notes":s,
//    "favorite":b,"created":n,"updated":n,"lastUsed":n,
//    "history":[{"password":s,"changedAt":n},…],          (v2: newest first, ≤ 10)
//    "sequence":s}                                         (optional, keyra/sequence.hpp)
// v1 differs only in lacking "history". "sequence" is written only when set;
// older Keyra firmware ignores it (unknown member) and keeps the rest. On import, missing members default to
// empty/false/0 and unknown members are ignored (forward compatible); a member
// of the wrong type rejects the backup.
#pragma once

#include <string>
#include <vector>

#include "json.hpp"
#include "keyra/vault.hpp"

namespace keyra::vault::backup {

// Bounds import cost on a hostile file: no Keyra calibrates above 2M
// (Vault::kMaxIterations), and 10M kept the vault locked for minutes.
inline constexpr uint32_t kMaxIterations = 2000000;
inline constexpr int64_t kVersion = 2;                // written; 1 and 2 are read

struct Envelope {
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

}  // namespace keyra::vault::backup
