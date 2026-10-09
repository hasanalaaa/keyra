// Small opaque records sealed with the DEK like an entry, so they can only be
// read or changed while unlocked: the activity log (SPEC §15), the access
// tokens (SPEC §17) and the NFC tap tags (SPEC §18). Their content is keyra_api's business; here it is bytes.
#include <cstring>

#include "vault_core.hpp"

namespace keyra::vault {
namespace {

constexpr uint8_t kVersion = 1;
constexpr size_t kOverhead = 1 + 12 + 16;
constexpr char kActivityAad[] = "keyra/activity/v1";
constexpr char kTokensAad[] = "keyra/tokens/v1";
constexpr char kTagsAad[] = "keyra/tags/v1";

const uint8_t* bytes(const char* s) { return reinterpret_cast<const uint8_t*>(s); }

}  // namespace

Status Vault::sealedRead(const char* path, const char* aad, std::vector<uint8_t>& out) {
  std::lock_guard<std::mutex> g(m_);
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
  if (Status s = requireUnlocked(); s != Status::Ok) return s;
  if (data.size() > max) return Status::Invalid;
  if (data.empty()) return p_.storage.remove(path) ? Status::Ok : Status::StorageError;
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

}  // namespace keyra::vault
