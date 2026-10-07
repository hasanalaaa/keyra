// Passkey records for keyra_fido (keyra/vault.hpp, docs/FIDO.md). Kept apart
// from the entry code: records are opaque here, loaded lazily on first use
// after unlock (a damaged record never blocks unlocking the password vault),
// and wiped on lock like entries.
#include <algorithm>
#include <cstdio>
#include <cstring>

#include "vault_core.hpp"

namespace keyra::vault {
namespace {

constexpr char kDir[] = "f";
constexpr char kSaltPath[] = "fido.bin";
constexpr char kWrapLabel[] = "keyra/fido/v1/wrap";
constexpr uint8_t kVersion = 1;
constexpr size_t kOverhead = 1 + 12 + 16;

std::string hexId(uint32_t id) {
  char b[9];
  std::snprintf(b, sizeof b, "%08x", static_cast<unsigned>(id));
  return b;
}
std::string recordPath(uint32_t id) { return std::string(kDir) + "/" + hexId(id) + ".bin"; }
std::string recordAad(uint32_t id) { return "keyra/f/v1/" + hexId(id); }

bool parseName(const std::string& name, uint32_t& id) {
  if (name.size() != 12 || name.compare(8, 4, ".bin") != 0) return false;
  id = 0;
  for (int i = 0; i < 8; ++i) {
    const char c = name[i];
    const int v = c >= '0' && c <= '9' ? c - '0' : c >= 'a' && c <= 'f' ? c - 'a' + 10 : -1;
    if (v < 0) return false;
    id = (id << 4) | uint32_t(v);
  }
  return id != 0;
}

const uint8_t* bytes(const char* s) { return reinterpret_cast<const uint8_t*>(s); }

}  // namespace

Status Vault::removePasskeyFilesLocked() {
  std::vector<std::string> names;
  if (!p_.storage.list(kDir, names)) return Status::StorageError;
  for (const auto& n : names)
    if (!p_.storage.remove(std::string(kDir) + "/" + n)) return Status::StorageError;
  return p_.storage.remove(kSaltPath) ? Status::Ok : Status::StorageError;
}

Status Vault::loadPasskeysLocked() {
  if (passkeysLoaded_) return Status::Ok;
  passkeys_.clear();
  std::vector<std::string> names;
  if (!p_.storage.list(kDir, names)) return Status::StorageError;
  std::vector<uint8_t> file;
  for (const auto& name : names) {
    uint32_t id;
    if (!parseName(name, id)) continue;
    if (p_.storage.read(recordPath(id), file) != Storage::Read::Ok) return Status::StorageError;
    if (file.size() < kOverhead || file[0] != kVersion) return Status::Corrupt;
    SecureBuf plain;
    if (!plain.alloc(file.size() - kOverhead)) return Status::StorageError;
    const std::string aad = recordAad(id);
    switch (p_.crypto.gcmOpen(dek_.data(), file.data() + 1, bytes(aad.c_str()), aad.size(),
                              file.data() + 13, file.size() - 13, plain.data())) {
      case Crypto::Open::Ok: break;
      case Crypto::Open::AuthFailed: return Status::Corrupt;
      case Crypto::Open::Error: return Status::StorageError;
    }
    passkeys_.push_back(Slot{id, std::move(plain)});
  }
  passkeysLoaded_ = true;
  return Status::Ok;
}

Status Vault::passkeyList(std::vector<PasskeyRecord>& out) {
  std::lock_guard<std::mutex> g(m_);
  out.clear();
  if (Status s = requireUnlocked(); s != Status::Ok) return s;
  if (Status s = loadPasskeysLocked(); s != Status::Ok) return s;
  out.reserve(passkeys_.size());
  for (const auto& p : passkeys_)
    out.push_back(PasskeyRecord{p.id, std::vector<uint8_t>(p.plain.data(), p.plain.data() + p.plain.size())});
  return Status::Ok;
}

Status Vault::passkeyPut(uint32_t& id, const std::vector<uint8_t>& data) {
  std::lock_guard<std::mutex> g(m_);
  if (Status s = requireUnlocked(); s != Status::Ok) return s;
  if (data.empty() || data.size() > kMaxPasskeyRecord) return Status::Invalid;
  if (Status s = loadPasskeysLocked(); s != Status::Ok) return s;
  auto find = [&](uint32_t want) {
    return std::find_if(passkeys_.begin(), passkeys_.end(), [want](const Slot& s) { return s.id == want; });
  };
  uint32_t target = id;
  if (target == 0) {
    if (passkeys_.size() >= kMaxPasskeys) return Status::Full;
    for (int tries = 0; target == 0 || find(target) != passkeys_.end(); ++tries) {
      if (tries == 32 || !p_.crypto.random(reinterpret_cast<uint8_t*>(&target), sizeof target))
        return Status::StorageError;
    }
  } else if (find(target) == passkeys_.end()) {
    return Status::NotFound;
  }

  SecureBuf plain;
  if (!plain.alloc(data.size())) return Status::Full;
  std::memcpy(plain.data(), data.data(), data.size());
  std::vector<uint8_t> file(kOverhead + data.size());
  file[0] = kVersion;
  if (!p_.crypto.random(file.data() + 1, 12)) return Status::StorageError;
  const std::string aad = recordAad(target);
  if (!p_.crypto.gcmSeal(dek_.data(), file.data() + 1, bytes(aad.c_str()), aad.size(), plain.data(),
                         plain.size(), file.data() + 13))
    return Status::StorageError;
  if (Status s = writeAtomic(recordPath(target), file.data(), file.size()); s != Status::Ok) return s;
  auto it = find(target);
  if (it != passkeys_.end()) {
    it->plain = std::move(plain);
  } else {
    passkeys_.push_back(Slot{target, std::move(plain)});
  }
  id = target;
  return Status::Ok;
}

Status Vault::passkeyRemove(uint32_t id) {
  std::lock_guard<std::mutex> g(m_);
  if (Status s = requireUnlocked(); s != Status::Ok) return s;
  if (Status s = loadPasskeysLocked(); s != Status::Ok) return s;
  auto it = std::find_if(passkeys_.begin(), passkeys_.end(), [id](const Slot& s) { return s.id == id; });
  if (it == passkeys_.end()) return Status::NotFound;
  if (!p_.storage.remove(recordPath(id))) return Status::StorageError;
  passkeys_.erase(it);
  return Status::Ok;
}

Status Vault::passkeyWrapKey(uint8_t out[32]) {
  std::lock_guard<std::mutex> g(m_);
  if (Status s = requireUnlocked(); s != Status::Ok) return s;
  std::vector<uint8_t> file;
  switch (p_.storage.read(kSaltPath, file)) {
    case Storage::Read::Error: return Status::StorageError;
    case Storage::Read::NotFound: {
      file.assign(17, 0);
      file[0] = kVersion;
      if (!p_.crypto.random(file.data() + 1, 16)) return Status::StorageError;
      if (Status s = writeAtomic(kSaltPath, file.data(), file.size()); s != Status::Ok) return s;
      break;
    }
    case Storage::Read::Ok:
      if (file.size() != 17 || file[0] != kVersion) return Status::Corrupt;
      break;
  }
  uint8_t msg[sizeof kWrapLabel - 1 + 16];
  std::memcpy(msg, kWrapLabel, sizeof kWrapLabel - 1);
  std::memcpy(msg + sizeof kWrapLabel - 1, file.data() + 1, 16);
  uint8_t mac[64];
  size_t macLen = 0;
  const bool ok = p_.crypto.hmac(Hash::Sha256, dek_.data(), dek_.size(), msg, sizeof msg, mac, &macLen) &&
                  macLen == 32;
  if (ok) std::memcpy(out, mac, 32);
  mem::zeroize(mac, sizeof mac);
  return ok ? Status::Ok : Status::StorageError;
}

Status Vault::passkeyReset() {
  std::lock_guard<std::mutex> g(m_);
  if (Status s = requireUnlocked(); s != Status::Ok) return s;
  passkeys_.clear();
  passkeysLoaded_ = false;
  // The next passkeyWrapKey() creates a fresh salt.
  return removePasskeyFilesLocked();
}

}  // namespace keyra::vault
