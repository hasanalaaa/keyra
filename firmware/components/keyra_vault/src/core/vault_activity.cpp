// Small records sealed with the DEK like an entry, so they can only be read or
// changed while unlocked: the activity log (SPEC §15), the access tokens
// (SPEC §17) and the NFC tap tags (SPEC §18) — keyra_api's bytes — and the
// FIDO signature counter's reservation (SPEC §11), the vault's own u32.
#include <cstring>

#include "vault_core.hpp"

namespace keyra::vault {
namespace {

constexpr uint8_t kVersion = 1;
constexpr size_t kOverhead = 1 + 12 + 16;
constexpr char kActivityAad[] = "keyra/activity/v1";
constexpr char kTokensAad[] = "keyra/tokens/v1";
constexpr char kTagsAad[] = "keyra/tags/v1";
constexpr char kFidoCounterAad[] = "keyra/fidoctr/v1";
constexpr size_t kFidoCounterBytes = 4;  // u32 LE

const uint8_t* bytes(const char* s) { return reinterpret_cast<const uint8_t*>(s); }

}  // namespace

Status Vault::sealedRead(const char* path, const char* aad, std::vector<uint8_t>& out) {
  std::lock_guard<std::mutex> g(m_);
  return sealedReadLocked(path, aad, out);
}

Status Vault::sealedReadLocked(const char* path, const char* aad, std::vector<uint8_t>& out) {
  out.clear();
  if (Status s = requireUnlocked(); s != Status::Ok) return s;
  std::vector<uint8_t> file;
  switch (p_.storage.read(path, file)) {
    case Storage::Read::NotFound: return Status::Ok;  // nothing written yet
    case Storage::Read::Error: return Status::StorageError;
    case Storage::Read::Ok: break;
  }
  if (file.size() < kOverhead || file[0] != kVersion) return Status::Corrupt;
  out.resize(file.size() - kOverhead);
  switch (p_.crypto.gcmOpen(dek_.data(), file.data() + 1, bytes(aad), std::strlen(aad), file.data() + 13,
                            file.size() - 13, out.data())) {
    case Crypto::Open::Ok: return Status::Ok;
    case Crypto::Open::AuthFailed: out.clear(); return Status::Corrupt;
    case Crypto::Open::Error: out.clear(); return Status::StorageError;
  }
  return Status::StorageError;
}

Status Vault::sealedWrite(const char* path, const char* aad, size_t max, const std::vector<uint8_t>& data) {
  std::lock_guard<std::mutex> g(m_);
  return sealedWriteLocked(path, aad, max, data);
}

Status Vault::sealedWriteLocked(const char* path, const char* aad, size_t max, const std::vector<uint8_t>& data) {
  if (Status s = requireUnlocked(); s != Status::Ok) return s;
  if (data.size() > max) return Status::Invalid;
  if (data.empty()) return p_.storage.remove(path) ? Status::Ok : storageFailure();
  std::vector<uint8_t> file(kOverhead + data.size());
  file[0] = kVersion;
  if (!p_.crypto.random(file.data() + 1, 12)) return Status::StorageError;
  if (!p_.crypto.gcmSeal(dek_.data(), file.data() + 1, bytes(aad), std::strlen(aad), data.data(), data.size(),
                         file.data() + 13))
    return Status::StorageError;
  return writeAtomic(path, file.data(), file.size());
}

Status Vault::activityRead(std::vector<uint8_t>& out) { return sealedRead(kActivityPath, kActivityAad, out); }

Status Vault::activityWrite(const std::vector<uint8_t>& data) {
  return sealedWrite(kActivityPath, kActivityAad, kMaxActivity, data);
}

Status Vault::tokensRead(std::vector<uint8_t>& out) { return sealedRead(kTokensPath, kTokensAad, out); }

Status Vault::tokensWrite(const std::vector<uint8_t>& data) {
  return sealedWrite(kTokensPath, kTokensAad, kMaxTokensBytes, data);
}

Status Vault::tagsRead(std::vector<uint8_t>& out) { return sealedRead(kTagsPath, kTagsAad, out); }

Status Vault::tagsWrite(const std::vector<uint8_t>& data) { return sealedWrite(kTagsPath, kTagsAad, kMaxTagsBytes, data); }

Status Vault::fidoCounterReadLocked(uint32_t& out) {
  out = 0;
  std::vector<uint8_t> b;
  if (Status s = sealedReadLocked(kFidoCounterPath, kFidoCounterAad, b); s != Status::Ok) return s;
  if (b.empty()) return Status::Ok;  // none yet
  if (b.size() != kFidoCounterBytes) return Status::Corrupt;
  out = uint32_t(b[0]) | uint32_t(b[1]) << 8 | uint32_t(b[2]) << 16 | uint32_t(b[3]) << 24;
  return Status::Ok;
}

Status Vault::fidoCounterRaiseLocked(uint32_t atLeast) {
  uint32_t cur = 0;
  if (Status s = fidoCounterReadLocked(cur); s != Status::Ok) return s;
  if (cur >= atLeast) return Status::Ok;  // never lowered
  const std::vector<uint8_t> b = {uint8_t(atLeast), uint8_t(atLeast >> 8), uint8_t(atLeast >> 16),
                                  uint8_t(atLeast >> 24)};
  return sealedWriteLocked(kFidoCounterPath, kFidoCounterAad, kFidoCounterBytes, b);
}

Status Vault::fidoCounterRead(uint32_t& out) {
  std::lock_guard<std::mutex> g(m_);
  return fidoCounterReadLocked(out);
}

Status Vault::fidoCounterRaise(uint32_t atLeast) {
  std::lock_guard<std::mutex> g(m_);
  return fidoCounterRaiseLocked(atLeast);
}

}  // namespace keyra::vault
