#include "vault_core.hpp"

#include <algorithm>
#include <cstdio>
#include <cstring>

#include "backup_format.hpp"
#include "entry_codec.hpp"
#include "json.hpp"
#include "text.hpp"

namespace keyra::vault {
namespace {

constexpr char kMetaPath[] = "meta.bin";
constexpr char kEntryDir[] = "e";
constexpr char kRestoreMarker[] = "restore.commit";
constexpr uint8_t kMarkerVersion = 1;
constexpr char kMetaAad[] = "keyra/meta/v1";  // the passphrase wrap keeps its v1 AAD
constexpr char kRecoveryAad[] = "keyra/wrap/recovery/v1";
constexpr char kRecoveryInfo[] = "keyra/recovery/v1";
constexpr uint8_t kMagic[4] = {'K', 'Y', 'R', '1'};
constexpr uint8_t kEntryVersion = 1;
constexpr uint8_t kWrapPass = 1, kWrapRecovery = 2;  // 3: reserved (device-bound)
constexpr size_t kPassBody = 4 + 16 + 12 + 48, kRecoveryBody = 8 + 16 + 12 + 48;
constexpr size_t kMetaV1Size = 4 + 1 + kPassBody;
constexpr size_t kEntryOverhead = 1 + 12 + 16;
constexpr uint32_t kCalibrationIterations = 10000;
constexpr uint64_t kCalibrationTargetMs = 1200;

std::string hexId(uint32_t id) {
  char b[9];
  std::snprintf(b, sizeof b, "%08x", static_cast<unsigned>(id));
  return b;
}
std::string entryPath(uint32_t id) { return std::string(kEntryDir) + "/" + hexId(id) + ".bin"; }
std::string entryAad(uint32_t id) { return "keyra/e/v1/" + hexId(id); }
std::string stagedPath(uint32_t id) { return std::string(kEntryDir) + "/" + hexId(id) + ".new"; }

bool parseEntryName(const std::string& name, uint32_t& id) {
  if (name.size() != 12 || name.compare(8, 4, ".bin") != 0) return false;
  id = 0;
  for (int i = 0; i < 8; ++i) {
    char c = name[i];
    int v = c >= '0' && c <= '9' ? c - '0' : c >= 'a' && c <= 'f' ? c - 'a' + 10 : -1;
    if (v < 0) return false;
    id = (id << 4) | uint32_t(v);
  }
  return id != 0;
}

bool endsWith(const std::string& s, const char* suffix) {
  size_t n = std::strlen(suffix);
  return s.size() >= n && s.compare(s.size() - n, n, suffix) == 0;
}

bool isStaged(const std::string& name) { return endsWith(name, ".new") || endsWith(name, ".new.tmp"); }

const uint8_t* bytes(const char* s) { return reinterpret_cast<const uint8_t*>(s); }

// rec.history := stored history of `old`, with old's password in front when
// rec changes it. Copies, not moves: the caller wipes `old` as a whole.
void keepHistory(const Entry& old, Entry& rec) {
  for (OldPassword& h : rec.history) wipe(h.password);
  rec.history.clear();
  rec.history.reserve(kMaxHistory);  // no regrowth (see codec::decode)
  if (!old.password.empty() && old.password != rec.password)
    rec.history.push_back(OldPassword{old.password, rec.updated});
  for (const OldPassword& h : old.history) {
    if (rec.history.size() == kMaxHistory) break;
    rec.history.push_back(h);
  }
}

struct Identity {  // merge key for imports without a matching id
  uint32_t id;
  std::string title, username, url;
};

}  // namespace

uint32_t unlockDelayMs(uint32_t failures) {
  if (failures <= 4) return 0;
  uint32_t exp = failures - 4;
  uint32_t seconds = exp >= 10 ? 900 : std::min<uint32_t>(900, 1u << exp);
  return seconds * 1000;
}

Vault::Vault(Platform platform, Options options) : p_(platform), opt_(options) {}

Vault::~Vault() { lock(); }

Status Vault::ready() const { return ready_ ? Status::Ok : Status::StorageError; }

Status Vault::requireUnlocked() const {
  if (!ready_) return Status::StorageError;
  if (!initialized_) return Status::NotInitialized;
  return unlocked_ ? Status::Ok : Status::Locked;
}

void Vault::wipeKeys() { mem::zeroize(dek_.data(), dek_.size()); }

Status Vault::init() {
  std::lock_guard<std::mutex> g(m_);
  wipeKeys();
  slots_.clear();
  unlocked_ = initialized_ = ready_ = false;

  if (!p_.storage.mount()) return Status::StorageError;
  for (const char* dir : {"", kEntryDir, "f"}) {
    std::vector<std::string> names;
    if (!p_.storage.list(dir, names)) return Status::StorageError;
    for (const auto& n : names) {
      if (!endsWith(n, ".tmp")) continue;
      std::string path = *dir ? std::string(dir) + "/" + n : n;
      if (!p_.storage.remove(path)) return Status::StorageError;
    }
  }
  if (Status s = settleRestore(); s != Status::Ok) return s;
  uint32_t failures;
  if (!p_.counter.load(failures)) return Status::StorageError;
  failures_ = failures;
  lockedUntilMs_ = p_.clock.monotonicMs() + unlockDelayMs(failures);

  Status s = loadMeta();
  if (s == Status::NotInitialized) {
    ready_ = true;
    return Status::Ok;
  }
  if (s != Status::Ok) return s;  // only factoryReset() can recover from here
  ready_ = initialized_ = true;
  return Status::Ok;
}

namespace {
uint64_t getLe(const uint8_t* p, int n) {
  uint64_t v = 0;
  for (int i = n - 1; i >= 0; --i) v = v << 8 | p[i];
  return v;
}
void putLe(uint8_t* p, uint64_t v, int n) {
  for (int i = 0; i < n; ++i) p[i] = uint8_t(v >> (8 * i));
}
}  // namespace

Status Vault::loadMeta() {
  std::vector<uint8_t> b;
  switch (p_.storage.read(kMetaPath, b)) {
    case Storage::Read::NotFound: return Status::NotInitialized;
    case Storage::Read::Error: return Status::StorageError;
    case Storage::Read::Ok: break;
  }
  if (b.size() < 6 || std::memcmp(b.data(), kMagic, 4) != 0) return Status::Corrupt;
  Meta m;
  m.version = b[4];
  bool hasPass = false;
  auto readPass = [&](const uint8_t* p) {
    m.pass.iterations = uint32_t(getLe(p, 4));
    std::memcpy(m.pass.salt, p + 4, 16);
    std::memcpy(m.pass.iv, p + 20, 12);
    std::memcpy(m.pass.wrapped, p + 32, 48);
    hasPass = true;
  };
  if (m.version == 1) {
    if (b.size() != kMetaV1Size) return Status::Corrupt;
    readPass(b.data() + 5);
  } else if (m.version == 2) {
    size_t at = 6;
    for (uint8_t i = 0; i < b[5]; ++i) {
      if (at + 2 > b.size()) return Status::Corrupt;
      const uint8_t kind = b[at], len = b[at + 1];
      const uint8_t* body = b.data() + at + 2;
      at += 2 + size_t(len);
      if (at > b.size()) return Status::Corrupt;
      if (kind == kWrapPass && len == kPassBody && !hasPass) {
        readPass(body);
      } else if (kind == kWrapRecovery && len == kRecoveryBody && !m.hasRecovery) {
        m.recovery.created = int64_t(getLe(body, 8));
        std::memcpy(m.recovery.salt, body + 8, 16);
        std::memcpy(m.recovery.iv, body + 24, 12);
        std::memcpy(m.recovery.wrapped, body + 36, 48);
        m.hasRecovery = true;
      } else {
        return Status::Corrupt;  // unknown kind, duplicate or bad length: never guess
      }
    }
    if (at != b.size()) return Status::Corrupt;
  } else {
    return Status::Corrupt;
  }
  static_assert(Vault::kMaxIterations <= backup::kMaxIterations, "a backup of this vault must import");
  if (!hasPass || m.pass.iterations == 0 || m.pass.iterations > backup::kMaxIterations) return Status::Corrupt;
  meta_ = m;
  return Status::Ok;
}

Status Vault::writeMeta(const Meta& m) {
  std::vector<uint8_t> b(kMagic, kMagic + 4);
  b.push_back(2);
  b.push_back(m.hasRecovery ? 2 : 1);
  b.push_back(kWrapPass);
  b.push_back(uint8_t(kPassBody));
  size_t at = b.size();
  b.resize(at + kPassBody);
  putLe(&b[at], m.pass.iterations, 4);
  std::memcpy(&b[at + 4], m.pass.salt, 16);
  std::memcpy(&b[at + 20], m.pass.iv, 12);
  std::memcpy(&b[at + 32], m.pass.wrapped, 48);
  if (m.hasRecovery) {
    b.push_back(kWrapRecovery);
    b.push_back(uint8_t(kRecoveryBody));
    at = b.size();
    b.resize(at + kRecoveryBody);
    putLe(&b[at], uint64_t(m.recovery.created), 8);
    std::memcpy(&b[at + 8], m.recovery.salt, 16);
    std::memcpy(&b[at + 24], m.recovery.iv, 12);
    std::memcpy(&b[at + 36], m.recovery.wrapped, 48);
  }
  Status s = writeAtomic(kMetaPath, b.data(), b.size());
  if (s == Status::Ok) {
    meta_ = m;
    meta_.version = 2;
  }
  return s;
}

Status Vault::writeAtomic(const std::string& path, const uint8_t* data, size_t n) {
  const std::string tmp = path + ".tmp";
  if (!p_.storage.write(tmp, data, n) || !p_.storage.rename(tmp, path)) {
    p_.storage.remove(tmp);  // best effort; init() also sweeps leftovers
    return Status::StorageError;
  }
  return Status::Ok;
}

Status Vault::removeAllEntryFiles() {
  std::vector<std::string> names;
  if (!p_.storage.list(kEntryDir, names)) return Status::StorageError;
  for (const auto& n : names)
    if (!p_.storage.remove(std::string(kEntryDir) + "/" + n)) return Status::StorageError;
  return Status::Ok;
}

Status Vault::removeStaged() {
  std::vector<std::string> names;
  if (!p_.storage.list(kEntryDir, names)) return Status::StorageError;
  for (const auto& n : names)
    if (isStaged(n) && !p_.storage.remove(std::string(kEntryDir) + "/" + n)) return Status::StorageError;
  return Status::Ok;
}

// Idempotent, so a power cut anywhere in here is finished by the next call. The
// id list is what makes it so: once some staged files are renamed, the old and
// new e/<id>.bin can no longer be told apart by name.
Status Vault::settleRestore() {
  std::vector<uint8_t> marker;
  switch (p_.storage.read(kRestoreMarker, marker)) {
    case Storage::Read::NotFound: return removeStaged();  // never committed: roll back
    case Storage::Read::Error: return Status::StorageError;
    case Storage::Read::Ok: break;
  }
  if (marker.empty() || marker[0] != kMarkerVersion || (marker.size() - 1) % 4 != 0) return Status::Corrupt;
  std::vector<uint32_t> keep;
  for (size_t at = 1; at < marker.size(); at += 4) keep.push_back(uint32_t(getLe(&marker[at], 4)));
  std::vector<std::string> names;
  if (!p_.storage.list(kEntryDir, names)) return Status::StorageError;
  const std::string dir = std::string(kEntryDir) + "/";
  for (const auto& n : names) {
    uint32_t id;
    // An old entry whose id the backup reuses is replaced by the rename below.
    if (parseEntryName(n, id) && std::find(keep.begin(), keep.end(), id) == keep.end() &&
        !p_.storage.remove(dir + n))
      return Status::StorageError;
  }
  for (const auto& n : names) {
    if (endsWith(n, ".new") && !p_.storage.rename(dir + n, dir + n.substr(0, n.size() - 4) + ".bin"))
      return Status::StorageError;
  }
  return p_.storage.remove(kRestoreMarker) ? Status::Ok : Status::StorageError;
}

Status Vault::deriveKey(const std::string& pass, const uint8_t salt[16], uint32_t iters, Key& out) {
  return p_.crypto.pbkdf2Sha256(pass, salt, 16, iters, out.data(), out.size())
             ? Status::Ok
             : Status::StorageError;
}

uint32_t Vault::calibrateIterations() {
  static const uint8_t salt[16] = {};
  Key k;
  uint64_t t0 = p_.clock.monotonicMs();
  if (deriveKey("keyra-calibration", salt, kCalibrationIterations, k) != Status::Ok) return 0;
  uint64_t dt = std::max<uint64_t>(1, p_.clock.monotonicMs() - t0);
  mem::zeroize(k.data(), k.size());
  uint64_t iters = uint64_t(kCalibrationIterations) * kCalibrationTargetMs / dt;
  return uint32_t(std::clamp<uint64_t>(iters, kMinIterations, kMaxIterations));
}

Status Vault::wrapDek(const std::string& pass, uint32_t iters, const Key& dek, PassWrap& out) {
  out.iterations = iters;
  if (!p_.crypto.random(out.salt, 16) || !p_.crypto.random(out.iv, 12)) return Status::StorageError;
  Key kek;
  Status s = deriveKey(pass, out.salt, iters, kek);
  if (s == Status::Ok &&
      !p_.crypto.gcmSeal(kek.data(), out.iv, bytes(kMetaAad), std::strlen(kMetaAad), dek.data(),
                         dek.size(), out.wrapped))
    s = Status::StorageError;
  mem::zeroize(kek.data(), kek.size());
  return s;
}

Status Vault::setup(const std::string& passphrase) {
  std::lock_guard<std::mutex> g(m_);
  if (Status s = ready(); s != Status::Ok) return s;
  if (initialized_) return Status::AlreadyInitialized;
  if (passphrase.empty()) return Status::Invalid;

  uint32_t iters = opt_.kdfIterations ? opt_.kdfIterations : calibrateIterations();
  if (iters == 0) return Status::StorageError;
  // Files without a meta can only be leftovers of an interrupted reset; they
  // could never be decrypted with the new key.
  if (Status s = removeAllEntryFiles(); s != Status::Ok) return s;
  if (Status s = removePasskeyFilesLocked(); s != Status::Ok) return s;
  if (!p_.storage.remove(kActivityPath)) return Status::StorageError;
  if (!p_.counter.store(0)) return Status::StorageError;
  failures_ = 0;
  lockedUntilMs_ = 0;

  Key dek;
  if (!p_.crypto.random(dek.data(), dek.size())) return Status::StorageError;
  Meta m;
  Status s = wrapDek(passphrase, iters, dek, m.pass);
  if (s == Status::Ok) s = writeMeta(m);
  if (s != Status::Ok) {
    mem::zeroize(dek.data(), dek.size());
    return s;
  }
  dek_ = dek;
  mem::zeroize(dek.data(), dek.size());
  slots_.clear();
  initialized_ = unlocked_ = true;
  ++generation_;
  return Status::Ok;
}

Status Vault::openWrap(const Key& kek, const uint8_t iv[12], const char* aad, const uint8_t wrapped[48],
                       Key& dek) {
  switch (p_.crypto.gcmOpen(kek.data(), iv, bytes(aad), std::strlen(aad), wrapped, 48, dek.data())) {
    case Crypto::Open::Ok: return Status::Ok;
    case Crypto::Open::AuthFailed: return Status::WrongPassphrase;
    case Crypto::Open::Error: break;
  }
  return Status::StorageError;
}

// HKDF-SHA256 (RFC 5869) with one output block: the recovery key is uniformly
// random, so no slow KDF is needed on top of it.
Status Vault::recoveryKek(const RecoveryKey& key, const uint8_t salt[16], Key& out) {
  uint8_t prk[64], t[64];
  size_t n = 0;
  uint8_t info[sizeof kRecoveryInfo];
  std::memcpy(info, kRecoveryInfo, sizeof kRecoveryInfo - 1);
  info[sizeof kRecoveryInfo - 1] = 0x01;
  bool ok = p_.crypto.hmac(Hash::Sha256, salt, 16, key.data(), key.size(), prk, &n) && n == 32 &&
            p_.crypto.hmac(Hash::Sha256, prk, 32, info, sizeof info, t, &n) && n == 32;
  if (ok) std::memcpy(out.data(), t, 32);
  mem::zeroize(prk, sizeof prk);
  mem::zeroize(t, sizeof t);
  return ok ? Status::Ok : Status::StorageError;
}

Status Vault::attempt(const std::function<Status(Key&)>& open, Key& dek, uint32_t* retryAfterMs) {
  uint64_t now = p_.clock.monotonicMs();
  if (now < lockedUntilMs_) {
    if (retryAfterMs) *retryAfterMs = uint32_t(std::min<uint64_t>(lockedUntilMs_ - now, UINT32_MAX));
    return Status::RateLimited;
  }
  // Persist the attempt before doing any work, so cutting power mid-KDF still counts.
  uint32_t n = failures_ == UINT32_MAX ? failures_ : failures_ + 1;
  if (!p_.counter.store(n)) return Status::StorageError;
  failures_ = n;

  Status s = open(dek);
  if (s != Status::Ok) mem::zeroize(dek.data(), dek.size());
  if (s == Status::WrongPassphrase) {
    uint32_t delay = unlockDelayMs(n);
    lockedUntilMs_ = p_.clock.monotonicMs() + delay;
    if (retryAfterMs) *retryAfterMs = delay;
    return s;
  }
  if (s != Status::Ok) return s;
  if (!p_.counter.store(0)) {
    mem::zeroize(dek.data(), dek.size());
    return Status::StorageError;
  }
  failedBefore_ = n - 1;  // n counted this (successful) attempt too
  failures_ = 0;
  lockedUntilMs_ = 0;
  return Status::Ok;
}

Status Vault::attempt(const std::string& pass, Key& dek, uint32_t* retryAfterMs) {
  return attempt(
      [&](Key& out) {
        Key kek;
        Status s = deriveKey(pass, meta_.pass.salt, meta_.pass.iterations, kek);
        if (s == Status::Ok) s = openWrap(kek, meta_.pass.iv, kMetaAad, meta_.pass.wrapped, out);
        mem::zeroize(kek.data(), kek.size());
        return s;
      },
      dek, retryAfterMs);
}

Status Vault::attempt(const RecoveryKey& key, Key& dek, uint32_t* retryAfterMs) {
  return attempt(
      [&](Key& out) {
        if (!meta_.hasRecovery) return Status::WrongPassphrase;  // indistinguishable from a wrong key
        Key kek;
        Status s = recoveryKek(key, meta_.recovery.salt, kek);
        if (s == Status::Ok) s = openWrap(kek, meta_.recovery.iv, kRecoveryAad, meta_.recovery.wrapped, out);
        mem::zeroize(kek.data(), kek.size());
        return s;
      },
      dek, retryAfterMs);
}

Status Vault::finishUnlock(Key& dek) {
  if (unlocked_) {  // another client proving the passphrase; state is already loaded
    mem::zeroize(dek.data(), dek.size());
    return Status::Ok;
  }
  dek_ = dek;
  mem::zeroize(dek.data(), dek.size());
  Status s = loadEntries();
  if (s != Status::Ok) {
    wipeKeys();
    slots_.clear();
    return s;
  }
  unlocked_ = true;
  ++generation_;
  // A failed migration write is not fatal: version 1 stays readable and the
  // next unlock tries again. Any real storage fault shows on the next entry write.
  if (meta_.version == 1) (void)writeMeta(meta_);
  return Status::Ok;
}

Status Vault::unlock(const std::string& passphrase, uint32_t* retryAfterMs) {
  std::lock_guard<std::mutex> g(m_);
  if (retryAfterMs) *retryAfterMs = 0;
  if (Status s = ready(); s != Status::Ok) return s;
  if (!initialized_) return Status::NotInitialized;

  Key dek;
  Status s = attempt(passphrase, dek, retryAfterMs);
  if (s != Status::Ok) return s;
  return finishUnlock(dek);
}

Status Vault::checkRecovery(const RecoveryKey& key, uint32_t* retryAfterMs) {
  std::lock_guard<std::mutex> g(m_);
  if (retryAfterMs) *retryAfterMs = 0;
  if (Status s = ready(); s != Status::Ok) return s;
  if (!initialized_) return Status::NotInitialized;
  Key dek;
  Status s = attempt(key, dek, retryAfterMs);
  mem::zeroize(dek.data(), dek.size());
  return s;
}

Status Vault::recover(const RecoveryKey& key, const std::string& next, uint32_t* retryAfterMs) {
  std::lock_guard<std::mutex> g(m_);
  if (retryAfterMs) *retryAfterMs = 0;
  if (Status s = ready(); s != Status::Ok) return s;
  if (!initialized_) return Status::NotInitialized;
  if (next.empty()) return Status::Invalid;
  Key dek;
  Status s = attempt(key, dek, retryAfterMs);
  if (s != Status::Ok) return s;
  // The new passphrase wrap is written before the vault opens: a power cut
  // leaves either the old passphrase or the new one working, never neither.
  Meta m = meta_;
  s = wrapDek(next, meta_.pass.iterations, dek, m.pass);
  if (s == Status::Ok) s = writeMeta(m);
  if (s != Status::Ok) {
    mem::zeroize(dek.data(), dek.size());
    return s;
  }
  return finishUnlock(dek);
}

Status Vault::createRecovery(int64_t now, RecoveryKey& out) {
  std::lock_guard<std::mutex> g(m_);
  if (Status s = requireUnlocked(); s != Status::Ok) return s;
  RecoveryKey key;
  Meta m = meta_;
  m.hasRecovery = true;
  m.recovery.created = now;
  Status s = p_.crypto.random(key.data(), key.size()) && p_.crypto.random(m.recovery.salt, 16) &&
                     p_.crypto.random(m.recovery.iv, 12)
                 ? Status::Ok
                 : Status::StorageError;
  Key kek;
  if (s == Status::Ok) s = recoveryKek(key, m.recovery.salt, kek);
  if (s == Status::Ok &&
      !p_.crypto.gcmSeal(kek.data(), m.recovery.iv, bytes(kRecoveryAad), std::strlen(kRecoveryAad), dek_.data(),
                         dek_.size(), m.recovery.wrapped))
    s = Status::StorageError;
  mem::zeroize(kek.data(), kek.size());
  if (s == Status::Ok) s = writeMeta(m);
  if (s == Status::Ok) out = key;
  mem::zeroize(key.data(), key.size());
  return s;
}

Status Vault::removeRecovery() {
  std::lock_guard<std::mutex> g(m_);
  if (Status s = requireUnlocked(); s != Status::Ok) return s;
  if (!meta_.hasRecovery) return Status::NotFound;
  Meta m = meta_;
  m.hasRecovery = false;
  m.recovery = RecoveryWrap{};
  return writeMeta(m);
}

RecoveryInfo Vault::recoveryInfo() {
  std::lock_guard<std::mutex> g(m_);
  if (!initialized_ || !meta_.hasRecovery) return {};
  return {true, meta_.recovery.created};
}

Status Vault::loadEntries() {
  slots_.clear();
  if (Status s = settleRestore(); s != Status::Ok) return s;  // never read a half-done restore
  std::vector<std::string> names;
  if (!p_.storage.list(kEntryDir, names)) return Status::StorageError;
  slots_.reserve(names.size());
  std::vector<uint8_t> file;
  for (const auto& name : names) {
    uint32_t id;
    if (!parseEntryName(name, id)) continue;  // not ours; never decrypted, never listed
    if (p_.storage.read(entryPath(id), file) != Storage::Read::Ok) return Status::StorageError;
    if (file.size() < kEntryOverhead || file[0] != kEntryVersion) return Status::Corrupt;
    SecureBuf plain;
    if (!plain.alloc(file.size() - kEntryOverhead)) return Status::StorageError;
    const std::string aad = entryAad(id);
    Crypto::Open r = p_.crypto.gcmOpen(dek_.data(), file.data() + 1, bytes(aad.c_str()), aad.size(),
                                       file.data() + 13, file.size() - 13, plain.data());
    if (r == Crypto::Open::Error) return Status::StorageError;
    if (r == Crypto::Open::AuthFailed) return Status::Corrupt;
    Entry e;
    bool ok = codec::decode(plain.data(), plain.size(), e) && e.id == id;
    wipe(e);
    if (!ok) return Status::Corrupt;
    slots_.push_back(Slot{id, std::move(plain)});
  }
  return Status::Ok;
}

void Vault::lock() {
  std::lock_guard<std::mutex> g(m_);
  wipeKeys();
  slots_.clear();
  slots_.shrink_to_fit();
  passkeys_.clear();
  passkeysLoaded_ = false;
  unlocked_ = false;
}

Vault::Slot* Vault::find(uint32_t id) {
  for (auto& s : slots_)
    if (s.id == id) return &s;
  return nullptr;
}

bool Vault::newId(uint32_t& id) {
  for (int tries = 0; tries < 32; ++tries) {
    if (!p_.crypto.random(reinterpret_cast<uint8_t*>(&id), sizeof id)) return false;
    if (id != 0 && !find(id)) return true;
  }
  return false;
}

Status Vault::list(std::vector<Entry>& out) {
  std::lock_guard<std::mutex> g(m_);
  if (Status s = requireUnlocked(); s != Status::Ok) return s;
  out.clear();
  out.reserve(slots_.size());  // no regrowth: moved-from short strings would linger unwiped
  for (const auto& slot : slots_) {
    out.emplace_back();
    if (!codec::decode(slot.plain.data(), slot.plain.size(), out.back())) return Status::Corrupt;
  }
  return Status::Ok;
}

Status Vault::get(uint32_t id, Entry& out) {
  std::lock_guard<std::mutex> g(m_);
  if (Status s = requireUnlocked(); s != Status::Ok) return s;
  Slot* slot = find(id);
  if (!slot) return Status::NotFound;
  return codec::decode(slot->plain.data(), slot->plain.size(), out) ? Status::Ok : Status::Corrupt;
}

Status Vault::persist(const std::string& path, uint32_t id, const SecureBuf& plain) {
  std::vector<uint8_t> file(kEntryOverhead + plain.size());
  file[0] = kEntryVersion;
  if (!p_.crypto.random(file.data() + 1, 12)) return Status::StorageError;
  const std::string aad = entryAad(id);
  if (!p_.crypto.gcmSeal(dek_.data(), file.data() + 1, bytes(aad.c_str()), aad.size(),
                         plain.data(), plain.size(), file.data() + 13))
    return Status::StorageError;
  return writeAtomic(path, file.data(), file.size());
}

Status Vault::store(Entry& rec) {
  SecureBuf plain;
  if (!codec::encode(rec, plain)) return Status::Full;  // out of RAM for plaintext
  if (Status s = persist(entryPath(rec.id), rec.id, plain); s != Status::Ok) return s;
  if (Slot* slot = find(rec.id)) {
    slot->plain = std::move(plain);
  } else {
    slots_.push_back(Slot{rec.id, std::move(plain)});
  }
  return Status::Ok;
}

Status Vault::put(Entry& e) {
  std::lock_guard<std::mutex> g(m_);
  if (Status s = requireUnlocked(); s != Status::Ok) return s;
  if (!codec::valid(e)) return Status::Invalid;

  Entry rec = e;
  Status s = Status::Ok;
  if (e.id == 0) {
    if (slots_.size() >= kMaxEntries) s = Status::Full;
    else if (!newId(rec.id)) s = Status::StorageError;
    keepHistory(Entry{}, rec);
  } else if (Slot* slot = find(e.id)) {
    Entry old;
    if (codec::decode(slot->plain.data(), slot->plain.size(), old)) {
      if (rec.created == 0) rec.created = old.created;
      if (rec.lastUsed == 0) rec.lastUsed = old.lastUsed;
      keepHistory(old, rec);
    } else {
      s = Status::Corrupt;
    }
    wipe(old);
  } else {
    s = Status::NotFound;
  }
  if (s == Status::Ok) s = store(rec);
  if (s == Status::Ok) {
    e.id = rec.id;
    e.created = rec.created;
    e.lastUsed = rec.lastUsed;
  }
  wipe(rec);
  return s;
}

Status Vault::remove(uint32_t id) {
  std::lock_guard<std::mutex> g(m_);
  if (Status s = requireUnlocked(); s != Status::Ok) return s;
  auto it = std::find_if(slots_.begin(), slots_.end(), [id](const Slot& s) { return s.id == id; });
  if (it == slots_.end()) return Status::NotFound;
  if (!p_.storage.remove(entryPath(id))) return Status::StorageError;
  slots_.erase(it);  // SecureBuf wipes the plaintext
  return Status::Ok;
}

Status Vault::touch(uint32_t id, int64_t now, bool password, bool* burned) {
  std::lock_guard<std::mutex> g(m_);
  if (burned) *burned = false;
  if (Status s = requireUnlocked(); s != Status::Ok) return s;
  Slot* slot = find(id);
  if (!slot) return Status::NotFound;
  Entry rec;
  Status s = codec::decode(slot->plain.data(), slot->plain.size(), rec) ? Status::Ok : Status::Corrupt;
  if (s == Status::Ok && password && rec.burnAfter > 0 && --rec.burnAfter == 0) {
    // Its last allowed use: gone from flash and RAM.
    wipe(rec);
    if (!p_.storage.remove(entryPath(id))) return Status::StorageError;
    slots_.erase(std::find_if(slots_.begin(), slots_.end(), [id](const Slot& x) { return x.id == id; }));
    if (burned) *burned = true;
    return Status::Ok;
  }
  if (s == Status::Ok) {
    if (now != 0) rec.lastUsed = now;
    s = store(rec);
  }
  wipe(rec);
  return s;
}

Status Vault::changePassphrase(const std::string& cur, const std::string& next, uint32_t* retryAfterMs) {
  std::lock_guard<std::mutex> g(m_);
  if (Status s = requireUnlocked(); s != Status::Ok) return s;
  if (next.empty()) return Status::Invalid;
  Key dek;
  Status s = attempt(cur, dek, retryAfterMs);  // a wrong `cur` is throttled like unlock
  mem::zeroize(dek.data(), dek.size());
  if (s != Status::Ok) return s;
  Meta m = meta_;  // the recovery wrap (if any) stays
  s = wrapDek(next, meta_.pass.iterations, dek_, m.pass);  // only the DEK is re-wrapped
  if (s == Status::Ok) s = writeMeta(m);
  return s;
}

Status Vault::exportBackup(const std::string& backupPass, std::string& outJson) {
  std::lock_guard<std::mutex> g(m_);
  if (Status s = requireUnlocked(); s != Status::Ok) return s;
  if (!text::validUtf8(backupPass) || text::codePoints(backupPass) < kMinBackupPass)
    return Status::Invalid;

  SecureString plain;
  plain += '[';
  for (size_t i = 0; i < slots_.size(); ++i) {
    Entry e;
    bool ok = codec::decode(slots_[i].plain.data(), slots_[i].plain.size(), e);
    if (ok) {
      if (i) plain += ',';
      backup::writeEntry(plain, e);
    }
    wipe(e);
    if (!ok) {
      wipe(plain);
      return Status::Corrupt;
    }
  }
  plain += ']';

  backup::Envelope env;
  env.iterations = meta_.pass.iterations;
  Key key;
  Status s = p_.crypto.random(env.salt, 16) && p_.crypto.random(env.iv, 12) ? Status::Ok
                                                                             : Status::StorageError;
  if (s == Status::Ok) s = deriveKey(backupPass, env.salt, env.iterations, key);
  if (s == Status::Ok) {
    env.data.resize(plain.size() + 16);
    if (!p_.crypto.gcmSeal(key.data(), env.iv, nullptr, 0, bytes(plain.data()), plain.size(),
                           env.data.data()))
      s = Status::StorageError;
  }
  mem::zeroize(key.data(), key.size());
  wipe(plain);
  if (s == Status::Ok) outJson = backup::writeEnvelope(env);
  return s;
}

Status Vault::readBackup(const std::string& backupPass, const std::string& jsonText,
                         std::vector<Entry>& out) {
  out.clear();
  backup::Envelope env;
  if (!backup::readEnvelope(jsonText, env)) return Status::Invalid;

  json::Value root;
  {
    Key key;
    Status s = deriveKey(backupPass, env.salt, env.iterations, key);
    SecureBuf plain;
    if (s == Status::Ok && !plain.alloc(env.data.size() - 16)) s = Status::StorageError;
    if (s == Status::Ok) {
      switch (p_.crypto.gcmOpen(key.data(), env.iv, nullptr, 0, env.data.data(), env.data.size(),
                                plain.data())) {
        case Crypto::Open::Ok: break;
        case Crypto::Open::AuthFailed: s = Status::WrongPassphrase; break;
        case Crypto::Open::Error: s = Status::StorageError; break;
      }
    }
    mem::zeroize(key.data(), key.size());
    if (s != Status::Ok) return s;
    if (!json::parse(reinterpret_cast<const char*>(plain.data()), plain.size(), root) ||
        root.type != json::Value::Type::Array)
      return Status::Invalid;
  }
  if (root.items.size() > kMaxEntries) return Status::Full;

  out.resize(root.items.size());
  for (size_t i = 0; i < out.size(); ++i) {
    if (!backup::readEntry(root.items[i], out[i]) || !codec::valid(out[i])) {
      for (auto& e : out) wipe(e);
      out.clear();
      return Status::Invalid;
    }
  }
  return Status::Ok;
}

Status Vault::checkBackup(const std::string& backupPass, const std::string& jsonText) {
  std::lock_guard<std::mutex> g(m_);
  if (Status s = requireUnlocked(); s != Status::Ok) return s;
  std::vector<Entry> incoming;
  Status s = readBackup(backupPass, jsonText, incoming);
  for (auto& e : incoming) wipe(e);
  return s;
}

Status Vault::importBackup(const std::string& backupPass, const std::string& jsonText,
                           bool replace, size_t* added, size_t* updated) {
  std::lock_guard<std::mutex> g(m_);
  if (added) *added = 0;
  if (updated) *updated = 0;
  if (Status s = requireUnlocked(); s != Status::Ok) return s;

  // 1. Decrypt and validate everything before touching the vault.
  std::vector<Entry> incoming;
  if (Status s = readBackup(backupPass, jsonText, incoming); s != Status::Ok) return s;
  std::vector<Identity> index;  // merge view: existing entries + those this import adds
  auto cleanup = [&] {
    for (auto& e : incoming) wipe(e);
    for (auto& x : index) wipe(x.title), wipe(x.username), wipe(x.url);
  };

  // 2. Plan ids so the final count is known before anything is written.
  auto taken = [&](uint32_t id) {
    return std::any_of(index.begin(), index.end(), [id](const Identity& x) { return x.id == id; });
  };
  auto freshId = [&](uint32_t& id) {
    for (int tries = 0; tries < 32; ++tries) {
      if (!p_.crypto.random(reinterpret_cast<uint8_t*>(&id), sizeof id)) return false;
      if (id != 0 && !taken(id)) return true;
    }
    return false;
  };
  size_t nAdded = 0, nUpdated = 0;
  if (!replace) {
    index.reserve(slots_.size() + incoming.size());
    for (const auto& slot : slots_) {
      Entry e;
      if (!codec::decode(slot.plain.data(), slot.plain.size(), e)) {
        wipe(e);
        cleanup();
        return Status::Corrupt;
      }
      index.push_back(Identity{e.id, e.title, e.username, e.url});
      wipe(e);
    }
  }
  for (auto& e : incoming) {
    Identity* match = nullptr;
    if (!replace) {
      auto sameAccount = [&e](const Identity& x) {
        return x.title == e.title && x.username == e.username && x.url == e.url;
      };
      // An id alone is not enough: another vault may have used it for a different account.
      for (auto& x : index)
        if (e.id != 0 && x.id == e.id && sameAccount(x)) {
          match = &x;
          break;
        }
      if (!match)
        for (auto& x : index)
          if (sameAccount(x)) {
            match = &x;
            break;
          }
    }
    if (match) {
      e.id = match->id;
      continue;
    }
    if ((e.id == 0 || taken(e.id)) && !freshId(e.id)) {
      cleanup();
      return Status::StorageError;
    }
    index.push_back(Identity{e.id, e.title, e.username, e.url});
    ++nAdded;
  }
  if ((replace ? 0 : slots_.size()) + nAdded > kMaxEntries) {
    cleanup();
    return Status::Full;
  }

  // 3a. Replace: stage the new set beside the old one, then commit with one
  // atomic marker write (see settleRestore). A failure or power cut before the
  // marker leaves the old vault; from the marker on, the new one.
  if (replace) {
    Status s = removeStaged();
    std::vector<uint8_t> marker{kMarkerVersion};
    marker.reserve(1 + 4 * incoming.size());
    for (const auto& e : incoming) {
      if (s != Status::Ok) break;
      SecureBuf plain;
      s = codec::encode(e, plain) ? persist(stagedPath(e.id), e.id, plain) : Status::Full;
      marker.resize(marker.size() + 4);
      putLe(&marker[marker.size() - 4], e.id, 4);
    }
    if (s == Status::Ok) s = writeAtomic(kRestoreMarker, marker.data(), marker.size());
    cleanup();
    // Finishes or discards the restore on flash; RAM then mirrors the outcome.
    if (Status r = loadEntries(); r != Status::Ok) {
      wipeKeys();
      slots_.clear();
      unlocked_ = false;  // a written marker still completes it at the next init/unlock
      if (s == Status::Ok) s = r;
    }
    if (s != Status::Ok) return s;
    if (added) *added = nAdded;
    return Status::Ok;
  }

  // 3b. Merge. A storage failure from here on leaves a partial import (reported).
  Status s = Status::Ok;
  for (auto& e : incoming) {
    if (s != Status::Ok) break;
    Slot* slot = find(e.id);
    if (!slot) {
      s = store(e);
      continue;
    }
    Entry old;
    if (!codec::decode(slot->plain.data(), slot->plain.size(), old)) {
      s = Status::Corrupt;
    } else if (old.updated >= e.updated) {
      wipe(old);
      continue;  // the vault's copy is as new or newer: a stale backup never wins
    } else {
      // Like put(): the password being replaced goes into history, here in front
      // of the backup's own history, unless the backup already remembers it.
      const bool known = std::any_of(e.history.begin(), e.history.end(),
                                     [&old](const OldPassword& h) { return h.password == old.password; });
      if (!known) {
        std::swap(old.history, e.history);
        keepHistory(old, e);
      }
      if (e.created == 0) e.created = old.created;
      if (e.lastUsed == 0) e.lastUsed = old.lastUsed;
    }
    wipe(old);
    if (s == Status::Ok) s = store(e);
    if (s == Status::Ok) ++nUpdated;
  }
  cleanup();
  if (s != Status::Ok) {
    // RAM must reflect what actually reached flash.
    if (loadEntries() != Status::Ok) {
      wipeKeys();
      slots_.clear();
      unlocked_ = false;
    }
    return s;
  }
  if (added) *added = nAdded;
  if (updated) *updated = nUpdated;
  return Status::Ok;
}

Status Vault::factoryReset() {
  std::lock_guard<std::mutex> g(m_);
  wipeKeys();
  slots_.clear();
  slots_.shrink_to_fit();
  passkeys_.clear();
  passkeysLoaded_ = false;
  unlocked_ = initialized_ = ready_ = false;
  meta_ = Meta{};
  // format() destroys the whole partition (old ciphertext and wrapped keys
  // included), which also recovers a filesystem that no longer mounts.
  if (!p_.storage.format()) return Status::StorageError;
  if (!p_.counter.store(0)) return Status::StorageError;
  failures_ = 0;
  lockedUntilMs_ = 0;
  ready_ = true;
  return Status::Ok;
}

}  // namespace keyra::vault
