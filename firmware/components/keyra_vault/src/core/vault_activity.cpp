// The activity log's storage (SPEC §15): one record, sealed with the DEK like
// an entry, so it can only be read or extended while unlocked. Its content is
// keyra_api's business; here it is opaque bytes.
#include <cstring>

#include "vault_core.hpp"

namespace keyra::vault {
namespace {

constexpr uint8_t kVersion = 1;
constexpr size_t kOverhead = 1 + 12 + 16;
constexpr char kAad[] = "keyra/activity/v1";

const uint8_t* bytes(const char* s) { return reinterpret_cast<const uint8_t*>(s); }

}  // namespace

Status Vault::activityRead(std::vector<uint8_t>& out) {
  std::lock_guard<std::mutex> g(m_);
  out.clear();
  if (Status s = requireUnlocked(); s != Status::Ok) return s;
  std::vector<uint8_t> file;
  switch (p_.storage.read(kActivityPath, file)) {
    case Storage::Read::NotFound: return Status::Ok;  // nothing logged yet
    case Storage::Read::Error: return Status::StorageError;
    case Storage::Read::Ok: break;
  }
  if (file.size() < kOverhead || file[0] != kVersion) return Status::Corrupt;
  out.resize(file.size() - kOverhead);
  switch (p_.crypto.gcmOpen(dek_.data(), file.data() + 1, bytes(kAad), sizeof kAad - 1, file.data() + 13,
                            file.size() - 13, out.data())) {
    case Crypto::Open::Ok: return Status::Ok;
    case Crypto::Open::AuthFailed: out.clear(); return Status::Corrupt;
    case Crypto::Open::Error: out.clear(); return Status::StorageError;
  }
  return Status::StorageError;
}

Status Vault::activityWrite(const std::vector<uint8_t>& data) {
  std::lock_guard<std::mutex> g(m_);
  if (Status s = requireUnlocked(); s != Status::Ok) return s;
  if (data.size() > kMaxActivity) return Status::Invalid;
  if (data.empty()) return p_.storage.remove(kActivityPath) ? Status::Ok : Status::StorageError;
  std::vector<uint8_t> file(kOverhead + data.size());
  file[0] = kVersion;
  if (!p_.crypto.random(file.data() + 1, 12)) return Status::StorageError;
  if (!p_.crypto.gcmSeal(dek_.data(), file.data() + 1, bytes(kAad), sizeof kAad - 1, data.data(), data.size(),
                         file.data() + 13))
    return Status::StorageError;
  return writeAtomic(kActivityPath, file.data(), file.size());
}

}  // namespace keyra::vault
