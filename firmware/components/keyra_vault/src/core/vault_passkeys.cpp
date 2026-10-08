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
constexpr char kKeysPath[] = "fido.bin";
constexpr char kStagedKeysPath[] = "fido.new";
constexpr char kPinPath[] = "fidopin.bin";
constexpr char kPinAad[] = "keyra/fidopin/v1";
constexpr char kWrapLabel[] = "keyra/fido/v1/wrap";
constexpr char kKeysAad[] = "keyra/fido/v2/keys";
constexpr uint8_t kVersion = 1;
constexpr uint8_t kSaltVersion = 1, kKeysVersion = 2;  // fido.bin
constexpr size_t kSaltSize = 1 + 16;
constexpr size_t kOverhead = 1 + 12 + 16;

std::string hexId(uint32_t id) {
  char b[9];
  std::snprintf(b, sizeof b, "%08x", static_cast<unsigned>(id));
  return b;
}
std::string recordPath(uint32_t id) { return std::string(kDir) + "/" + hexId(id) + ".bin"; }
std::string stagedRecordPath(uint32_t id) { return std::string(kDir) + "/" + hexId(id) + ".new"; }
std::string recordAad(uint32_t id) { return "keyra/f/v1/" + hexId(id); }

bool endsWith(const std::string& s, const char* suffix) {
  const size_t n = std::strlen(suffix);
  return s.size() >= n && s.compare(s.size() - n, n, suffix) == 0;
}

bool parseName(const std::string& name, uint32_t& id, const char* ext = ".bin") {
  if (name.size() != 12 || name.compare(8, 4, ext) != 0) return false;
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
  return p_.storage.remove(kKeysPath) && p_.storage.remove(kPinPath) ? Status::Ok : Status::StorageError;
}

Status Vault::persistPasskey(const std::string& path, uint32_t id, const uint8_t* data, size_t n) {
  std::vector<uint8_t> file(kOverhead + n);
  file[0] = kVersion;
  if (!p_.crypto.random(file.data() + 1, 12)) return Status::StorageError;
  const std::string aad = recordAad(id);
  if (!p_.crypto.gcmSeal(dek_.data(), file.data() + 1, bytes(aad.c_str()), aad.size(), data, n, file.data() + 13))
    return Status::StorageError;
  return writeAtomic(path, file.data(), file.size());
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
  if (Status s = persistPasskey(recordPath(target), target, plain.data(), plain.size()); s != Status::Ok) return s;
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

Status Vault::readWrapKeysLocked(KeyList& out, bool create) {
  out.clear();
  out.reserve(kMaxPasskeyWrapKeys);
  std::vector<uint8_t> file;
  switch (p_.storage.read(kKeysPath, file)) {
    case Storage::Read::Error: return Status::StorageError;
    case Storage::Read::NotFound: {
      if (!create) return Status::Ok;
      file.assign(kSaltSize, 0);
      file[0] = kSaltVersion;
      if (!p_.crypto.random(file.data() + 1, 16)) return Status::StorageError;
      if (Status s = writeAtomic(kKeysPath, file.data(), file.size()); s != Status::Ok) return s;
      break;
    }
    case Storage::Read::Ok: break;
  }
  if (file.size() == kSaltSize && file[0] == kSaltVersion) {
    uint8_t msg[sizeof kWrapLabel - 1 + 16];
    std::memcpy(msg, kWrapLabel, sizeof kWrapLabel - 1);
    std::memcpy(msg + sizeof kWrapLabel - 1, file.data() + 1, 16);
    uint8_t mac[64];
    size_t macLen = 0;
    const bool ok = p_.crypto.hmac(Hash::Sha256, dek_.data(), dek_.size(), msg, sizeof msg, mac, &macLen) &&
                    macLen == 32;
    if (ok) {
      out.emplace_back();
      std::memcpy(out.back().data(), mac, 32);
    }
    mem::zeroize(mac, sizeof mac);
    return ok ? Status::Ok : Status::StorageError;
  }
  if (file.size() < kOverhead + 1 + 32 || file[0] != kKeysVersion) return Status::Corrupt;
  SecureBuf plain;
  if (!plain.alloc(file.size() - kOverhead)) return Status::StorageError;
  switch (p_.crypto.gcmOpen(dek_.data(), file.data() + 1, bytes(kKeysAad), sizeof kKeysAad - 1, file.data() + 13,
                            file.size() - 13, plain.data())) {
    case Crypto::Open::Ok: break;
    case Crypto::Open::AuthFailed: return Status::Corrupt;
    case Crypto::Open::Error: return Status::StorageError;
  }
  const size_t n = plain.data()[0];
  if (n < 1 || n > kMaxPasskeyWrapKeys || plain.size() != 1 + 32 * n) return Status::Corrupt;
  for (size_t i = 0; i < n; ++i) {
    out.emplace_back();
    std::memcpy(out.back().data(), plain.data() + 1 + 32 * i, 32);
  }
  return Status::Ok;
}

Status Vault::writeWrapKeys(const char* path, const KeyList& keys) {
  if (keys.empty() || keys.size() > kMaxPasskeyWrapKeys) return Status::Invalid;
  SecureBuf plain;
  if (!plain.alloc(1 + 32 * keys.size())) return Status::StorageError;
  plain.data()[0] = uint8_t(keys.size());
  for (size_t i = 0; i < keys.size(); ++i) std::memcpy(plain.data() + 1 + 32 * i, keys[i].data(), 32);
  std::vector<uint8_t> file(kOverhead + plain.size());
  file[0] = kKeysVersion;
  if (!p_.crypto.random(file.data() + 1, 12) ||
      !p_.crypto.gcmSeal(dek_.data(), file.data() + 1, bytes(kKeysAad), sizeof kKeysAad - 1, plain.data(),
                         plain.size(), file.data() + 13))
    return Status::StorageError;
  return writeAtomic(path, file.data(), file.size());
}

Status Vault::passkeyWrapKeys(uint8_t out[kMaxPasskeyWrapKeys][32], size_t& count) {
  std::lock_guard<std::mutex> g(m_);
  count = 0;
  if (Status s = requireUnlocked(); s != Status::Ok) return s;
  KeyList keys;
  if (Status s = readWrapKeysLocked(keys, true); s != Status::Ok) return s;
  for (const Key& k : keys) std::memcpy(out[count++], k.data(), 32);
  return Status::Ok;
}

Status Vault::planPasskeys(const backup::Passkeys& in, bool replace, PasskeyPlan& out) {
  out.keys.clear();
  out.writeKeys = false;
  out.records.clear();
  // Replace takes the backup's as they are; their limits were checked when it was read.
  if (!in.present || replace) return Status::Ok;
  if (Status s = loadPasskeysLocked(); s != Status::Ok) return s;
  if (Status s = readWrapKeysLocked(out.keys, false); s != Status::Ok) return s;
  for (const Key& k : in.keys) {
    if (std::find(out.keys.begin(), out.keys.end(), k) != out.keys.end()) continue;
    if (out.keys.size() == kMaxPasskeyWrapKeys) return Status::PasskeysFull;
    out.keys.push_back(k);  // after the local ones: new credentials keep using this Keyra's key
    out.writeKeys = true;
  }
  // Records are opaque here. keyra_fido never edits one (registering again writes
  // a new credential ID), so an identical record is the same credential.
  auto same = [](const uint8_t* a, size_t n, const std::vector<uint8_t>& b) {
    return n == b.size() && std::memcmp(a, b.data(), n) == 0;
  };
  for (const auto& r : in.records) {
    const bool known =
        std::any_of(passkeys_.begin(), passkeys_.end(),
                    [&](const Slot& p) { return same(p.plain.data(), p.plain.size(), r); }) ||
        std::any_of(out.records.begin(), out.records.end(),
                    [&](const std::vector<uint8_t>* q) { return same(q->data(), q->size(), r); });
    if (!known) out.records.push_back(&r);
  }
  return passkeys_.size() + out.records.size() > kMaxPasskeys ? Status::PasskeysFull : Status::Ok;
}

Status Vault::mergePasskeys(const PasskeyPlan& plan) {
  // Keys first: a record without its key would be dead; a key without records is harmless.
  if (plan.writeKeys)
    if (Status s = writeWrapKeys(kKeysPath, plan.keys); s != Status::Ok) return s;
  for (const auto* r : plan.records) {
    uint32_t id = 0;
    for (int tries = 0;
         id == 0 || std::any_of(passkeys_.begin(), passkeys_.end(), [id](const Slot& p) { return p.id == id; });
         ++tries) {
      if (tries == 32 || !p_.crypto.random(reinterpret_cast<uint8_t*>(&id), sizeof id)) return Status::StorageError;
    }
    SecureBuf plain;
    if (!plain.alloc(r->size())) return Status::Full;
    std::memcpy(plain.data(), r->data(), r->size());
    if (Status s = persistPasskey(recordPath(id), id, plain.data(), plain.size()); s != Status::Ok) return s;
    passkeys_.push_back(Slot{id, std::move(plain)});
  }
  return Status::Ok;
}

Status Vault::stagePasskeys(const backup::Passkeys& in, std::vector<uint32_t>& ids) {
  ids.clear();
  for (const auto& r : in.records) {
    uint32_t id = 0;
    for (int tries = 0; id == 0 || std::find(ids.begin(), ids.end(), id) != ids.end(); ++tries) {
      if (tries == 32 || !p_.crypto.random(reinterpret_cast<uint8_t*>(&id), sizeof id)) return Status::StorageError;
    }
    if (Status s = persistPasskey(stagedRecordPath(id), id, r.data(), r.size()); s != Status::Ok) return s;
    ids.push_back(id);
  }
  if (in.keys.empty()) return Status::Ok;  // the commit removes fido.bin
  KeyList keys;
  keys.reserve(kMaxPasskeyWrapKeys);
  keys.assign(in.keys.begin(), in.keys.end());
  return writeWrapKeys(kStagedKeysPath, keys);
}

Status Vault::removeStagedPasskeys() {
  std::vector<std::string> names;
  if (!p_.storage.list(kDir, names)) return Status::StorageError;
  for (const auto& n : names)
    if ((endsWith(n, ".new") || endsWith(n, ".new.tmp")) && !p_.storage.remove(std::string(kDir) + "/" + n))
      return Status::StorageError;
  // Checked first: this runs at every init and unlock, which should not write.
  if (!p_.storage.list("", names)) return Status::StorageError;
  if (std::find(names.begin(), names.end(), kStagedKeysPath) == names.end()) return Status::Ok;
  return p_.storage.remove(kStagedKeysPath) ? Status::Ok : Status::StorageError;
}

// The passkey half of settleRestore; idempotent in the same way.
Status Vault::settlePasskeys(const std::vector<uint32_t>& keep, bool keys) {
  passkeys_.clear();
  passkeysLoaded_ = false;
  std::vector<std::string> names;
  if (!p_.storage.list(kDir, names)) return Status::StorageError;
  for (const auto& n : names) {
    uint32_t id;
    if (parseName(n, id) && std::find(keep.begin(), keep.end(), id) == keep.end() &&
        !p_.storage.remove(std::string(kDir) + "/" + n))
      return Status::StorageError;
  }
  for (const auto& n : names) {
    uint32_t id;
    if (parseName(n, id, ".new") && !p_.storage.rename(stagedRecordPath(id), recordPath(id)))
      return Status::StorageError;
  }
  if (!keys) return p_.storage.remove(kKeysPath) ? Status::Ok : Status::StorageError;
  std::vector<uint8_t> staged;
  switch (p_.storage.read(kStagedKeysPath, staged)) {
    case Storage::Read::NotFound: return Status::Ok;  // renamed by an earlier, interrupted settle
    case Storage::Read::Error: return Status::StorageError;
    case Storage::Read::Ok: break;
  }
  return p_.storage.rename(kStagedKeysPath, kKeysPath) ? Status::Ok : Status::StorageError;
}

Status Vault::passkeyReset() {
  std::lock_guard<std::mutex> g(m_);
  if (Status s = requireUnlocked(); s != Status::Ok) return s;
  passkeys_.clear();
  passkeysLoaded_ = false;
  // The next passkeyWrapKeys() creates a fresh salt.
  return removePasskeyFilesLocked();
}

bool Vault::fidoPinSet() {
  std::lock_guard<std::mutex> g(m_);
  if (!ready_ || !initialized_) return false;
  std::vector<uint8_t> file;
  return p_.storage.read(kPinPath, file) == Storage::Read::Ok;
}

Status Vault::fidoPinRead(std::vector<uint8_t>& out) {
  std::lock_guard<std::mutex> g(m_);
  out.clear();
  if (Status s = requireUnlocked(); s != Status::Ok) return s;
  std::vector<uint8_t> file;
  switch (p_.storage.read(kPinPath, file)) {
    case Storage::Read::NotFound: return Status::NotFound;
    case Storage::Read::Error: return Status::StorageError;
    case Storage::Read::Ok: break;
  }
  if (file.size() < kOverhead || file[0] != kVersion) return Status::Corrupt;
  out.resize(file.size() - kOverhead);
  switch (p_.crypto.gcmOpen(dek_.data(), file.data() + 1, bytes(kPinAad), sizeof kPinAad - 1, file.data() + 13,
                            file.size() - 13, out.data())) {
    case Crypto::Open::Ok: return Status::Ok;
    case Crypto::Open::AuthFailed: out.clear(); return Status::Corrupt;
    case Crypto::Open::Error: out.clear(); return Status::StorageError;
  }
  return Status::StorageError;
}

Status Vault::fidoPinWrite(const std::vector<uint8_t>& data) {
  std::lock_guard<std::mutex> g(m_);
  if (Status s = requireUnlocked(); s != Status::Ok) return s;
  if (data.size() > kMaxFidoPinRecord) return Status::Invalid;
  if (data.empty()) return p_.storage.remove(kPinPath) ? Status::Ok : Status::StorageError;
  std::vector<uint8_t> file(kOverhead + data.size());
  file[0] = kVersion;
  if (!p_.crypto.random(file.data() + 1, 12) ||
      !p_.crypto.gcmSeal(dek_.data(), file.data() + 1, bytes(kPinAad), sizeof kPinAad - 1, data.data(), data.size(),
                         file.data() + 13))
    return Status::StorageError;
  return writeAtomic(kPinPath, file.data(), file.size());
}

}  // namespace keyra::vault
