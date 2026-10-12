#include "vault_core.hpp"

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <unordered_map>

#include "backup_format.hpp"
#include "backup_stream.hpp"
#include "bucket.hpp"
#include "entry_codec.hpp"
#include "json.hpp"
#include "text.hpp"

namespace keyra::vault {
namespace {

constexpr char kMetaPath[] = "meta.bin";
constexpr char kEntryDir[] = "e";
constexpr char kRestoreMarker[] = "restore.commit";
constexpr uint8_t kMarkerVersion = 1, kMarkerVersionPasskeys = 2;
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
constexpr uint8_t kHomeMagic[4] = {'K', 'Y', 'R', 'H'};
constexpr uint8_t kHomeVersion = 1;
// meta.bin left in the partition after the move: "KYR1" with a version no
// firmware knows, so every one of them (this one too) reports Corrupt instead
// of offering a fresh setup or opening a stale copy.
constexpr uint8_t kTombstone[] = {'K', 'Y', 'R', '1', 0x7f, 'm', 'o', 'v', 'e', 'd'};

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

bool isTombstone(const std::vector<uint8_t>& b) {
  return b.size() == sizeof kTombstone && std::memcmp(b.data(), kTombstone, sizeof kTombstone) == 0;
}

std::vector<uint8_t> homeFile(const std::string& firmware) {
  std::vector<uint8_t> b(kHomeMagic, kHomeMagic + 4);
  b.push_back(kHomeVersion);
  const size_t n = std::min<size_t>(firmware.size(), 255);
  b.push_back(uint8_t(n));
  b.insert(b.end(), firmware.begin(), firmware.begin() + long(n));
  return b;
}

bool parseHome(const std::vector<uint8_t>& b, std::string& firmware) {
  if (b.size() < 6 || std::memcmp(b.data(), kHomeMagic, 4) != 0 || b[4] != kHomeVersion ||
      b.size() != 6 + size_t(b[5]))
    return false;
  firmware.assign(b.begin() + 6, b.end());
  return true;
}

std::string join(const char* dir, const std::string& name) { return *dir ? std::string(dir) + "/" + name : name; }

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

std::string identityKey(const std::string& title, const std::string& username, const std::string& url) {
  std::string key;
  key.reserve(title.size() + username.size() + url.size() + 30);
  for (const std::string* field : {&title, &username, &url}) {
    key += std::to_string(field->size());
    key += ':';
    key += *field;
  }
  return key;
}

class Base64Sink {
 public:
  explicit Base64Sink(const std::function<bool(const char*, size_t)>& sink) : sink_(sink) {}
  bool write(const uint8_t* data, size_t n) {
    static constexpr char abc[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    for (size_t i = 0; i < n; ++i) {
      carry_[used_++] = data[i];
      if (used_ == 3) {
        const uint32_t x = (uint32_t(carry_[0]) << 16) | (uint32_t(carry_[1]) << 8) | carry_[2];
        out_[count_++] = abc[(x >> 18) & 63];
        out_[count_++] = abc[(x >> 12) & 63];
        out_[count_++] = abc[(x >> 6) & 63];
        out_[count_++] = abc[x & 63];
        used_ = 0;
        if (count_ == sizeof out_ && !flush()) return false;
      }
    }
    return true;
  }
  bool finish() {
    static constexpr char abc[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    if (used_) {
      const uint32_t x = (uint32_t(carry_[0]) << 16) | (used_ == 2 ? uint32_t(carry_[1]) << 8 : 0);
      out_[count_++] = abc[(x >> 18) & 63];
      out_[count_++] = abc[(x >> 12) & 63];
      out_[count_++] = used_ == 2 ? abc[(x >> 6) & 63] : '=';
      out_[count_++] = '=';
      used_ = 0;
    }
    return flush();
  }
 private:
  bool flush() {
    if (!count_) return true;
    const bool ok = sink_(out_, count_);
    count_ = 0;
    return ok;
  }
  const std::function<bool(const char*, size_t)>& sink_;
  uint8_t carry_[3] = {};
  size_t used_ = 0, count_ = 0;
  char out_[4096] = {};
};

}  // namespace

struct BackupImport::Impl {
  backup::Entries entries;
  backup::Passkeys passkeys;
  ~Impl() {
    for (auto& e : entries) wipe(e);
    for (auto& key : passkeys.keys) mem::zeroize(key.data(), key.size());
    for (auto& record : passkeys.records)
      if (!record.empty()) mem::zeroize(record.data(), record.size());
  }
};

BackupImport::BackupImport() = default;
BackupImport::~BackupImport() = default;
BackupImport::BackupImport(BackupImport&&) noexcept = default;
BackupImport& BackupImport::operator=(BackupImport&&) noexcept = default;

uint32_t unlockDelayMs(uint32_t failures) {
  if (failures <= 4) return 0;
  uint32_t exp = failures - 4;
  uint32_t seconds = exp >= 10 ? 900 : std::min<uint32_t>(900, 1u << exp);
  return seconds * 1000;
}

Vault::Vault(Platform platform, Options options) : p_(platform), opt_(std::move(options)), fs_(&p_.storage) {}

Vault::~Vault() { lock(); }

Status Vault::ready() const { return ready_ ? Status::Ok : Status::StorageError; }

Status Vault::storageFailure(const Storage& st) {
  return st.lastFailure() == Storage::Failure::NoSpace ? Status::Full : Status::StorageError;
}

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

  if (Status s = selectHome(); s != Status::Ok) return s;
  for (const char* dir : {"", kEntryDir, "f", bucket::kDir}) {
    std::vector<std::string> names;
    if (!fs_->list(dir, names)) return Status::StorageError;
    for (const auto& n : names) {
      if (!endsWith(n, ".tmp")) continue;
      if (!fs_->remove(join(dir, n))) return storageFailure();
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
  switch (fs_->read(kMetaPath, b)) {
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

Status Vault::writeAtomic(Storage& st, const std::string& path, const uint8_t* data, size_t n) {
  const std::string tmp = path + ".tmp";
  if (!st.write(tmp, data, n)) {
    const Status failure = storageFailure(st);
    st.remove(tmp);  // cleanup must not hide the original ENOSPC
    return failure;
  }
  if (!st.rename(tmp, path)) {
    const Status failure = storageFailure(st);
    st.remove(tmp);  // best effort; init() also sweeps leftovers
    return failure;
  }
  return Status::Ok;
}

// Both entry directories in either layout: e/ may hold foreign files a move copied.
Status Vault::removeAllEntryFiles() {
  for (const char* dir : {kEntryDir, bucket::kDir}) {
    std::vector<std::string> names;
    if (!fs_->list(dir, names)) return Status::StorageError;
    for (const auto& n : names)
      if (!fs_->remove(join(dir, n))) return storageFailure();
  }
  return Status::Ok;
}

Status Vault::removeStaged() {
  for (const char* dir : {kEntryDir, bucket::kDir}) {
    std::vector<std::string> names;
    if (!fs_->list(dir, names)) return Status::StorageError;
    for (const auto& n : names)
      if (isStaged(n) && !fs_->remove(join(dir, n))) return storageFailure();
  }
  return removeStagedPasskeys();
}

// Idempotent, so a power cut anywhere in here is finished by the next call. The
// id list is what makes it so: once some staged files are renamed, the old and
// new e/<id>.bin can no longer be told apart by name.
Status Vault::settleRestore() {
  std::vector<uint8_t> marker;
  switch (fs_->read(kRestoreMarker, marker)) {
    case Storage::Read::NotFound: return removeStaged();  // never committed: roll back
    case Storage::Read::Error: return Status::StorageError;
    case Storage::Read::Ok: break;
  }
  std::vector<uint32_t> keep, keepPasskeys;
  bool passkeys = false, keys = false;
  if (!marker.empty() && marker[0] == kMarkerVersion) {
    if ((marker.size() - 1) % 4 != 0) return Status::Corrupt;
    for (size_t at = 1; at < marker.size(); at += 4) keep.push_back(uint32_t(getLe(&marker[at], 4)));
  } else if (!marker.empty() && marker[0] == kMarkerVersionPasskeys) {
    size_t at = 1;
    auto ids = [&](std::vector<uint32_t>& out) {
      if (at + 4 > marker.size()) return false;
      const uint64_t n = getLe(&marker[at], 4);
      at += 4;
      if (n > (marker.size() - at) / 4) return false;
      for (uint64_t i = 0; i < n; ++i, at += 4) out.push_back(uint32_t(getLe(&marker[at], 4)));
      return true;
    };
    if (!ids(keep) || !ids(keepPasskeys) || at + 1 != marker.size()) return Status::Corrupt;
    passkeys = true;
    keys = marker[at] != 0;
  } else {
    return Status::Corrupt;
  }
  std::vector<std::string> names;
  if (buckets_) {
    // The same per bucket: the new set's buckets are the ones its ids fall in,
    // and each of them was staged whole.
    std::vector<bool> kept(bucket::kCount, false);
    for (uint32_t id : keep) kept[bucket::of(id)] = true;
    if (!fs_->list(bucket::kDir, names)) return Status::StorageError;
    uint32_t k;
    for (const auto& n : names)
      if (bucket::parseName(n, k, ".bin") && !kept[k] && !fs_->remove(bucket::path(k))) return storageFailure();
    for (const auto& n : names)
      if (bucket::parseName(n, k, ".new") && !fs_->rename(bucket::stagedPath(k), bucket::path(k)))
        return storageFailure();
  } else {
    if (!fs_->list(kEntryDir, names)) return Status::StorageError;
    const std::string dir = std::string(kEntryDir) + "/";
    for (const auto& n : names) {
      uint32_t id;
      // An old entry whose id the backup reuses is replaced by the rename below.
      if (parseEntryName(n, id) && std::find(keep.begin(), keep.end(), id) == keep.end() &&
          !fs_->remove(dir + n))
        return storageFailure();
    }
    for (const auto& n : names) {
      if (endsWith(n, ".new") && !fs_->rename(dir + n, dir + n.substr(0, n.size() - 4) + ".bin"))
        return storageFailure();
    }
  }
  if (passkeys)
    if (Status s = settlePasskeys(keepPasskeys, keys); s != Status::Ok) return s;
  return fs_->remove(kRestoreMarker) ? Status::Ok : storageFailure();
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
  if (!fs_->remove(kActivityPath) || !fs_->remove(kActivityAsidePath) || !fs_->remove(kTokensPath) ||
      !fs_->remove(kTagsPath) || !fs_->remove(kFidoCounterPath))
    return storageFailure();
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

Status Vault::openEntry(uint32_t id, const std::vector<uint8_t>& file) {
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
  slots_.push_back(Slot{id, std::move(plain), ++revisions_});
  return Status::Ok;
}

Status Vault::loadEntries() {
  slots_.clear();
  if (Status s = settleRestore(); s != Status::Ok) return s;  // never read a half-done restore
  std::vector<std::string> names;
  std::vector<uint8_t> file;
  if (buckets_) {
    if (!fs_->list(bucket::kDir, names)) return Status::StorageError;
    bucket::Records records;
    for (const auto& name : names) {
      uint32_t k;
      if (!bucket::parseName(name, k, ".bin")) continue;  // not ours
      if (fs_->read(bucket::path(k), file) != Storage::Read::Ok) return Status::StorageError;
      // A damaged bucket fails the unlock like a damaged entry file: never skipped.
      if (!bucket::parse(file, k, records)) return Status::Corrupt;
      for (const auto& rec : records)
        if (Status s = openEntry(rec.id, rec.sealed); s != Status::Ok) return s;
    }
    return Status::Ok;
  }
  if (!fs_->list(kEntryDir, names)) return Status::StorageError;
  slots_.reserve(names.size());
  for (const auto& name : names) {
    uint32_t id;
    if (!parseEntryName(name, id)) continue;  // not ours; never decrypted, never listed
    if (fs_->read(entryPath(id), file) != Storage::Read::Ok) return Status::StorageError;
    if (Status s = openEntry(id, file); s != Status::Ok) return s;
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

Status Vault::forEach(const std::function<bool(const Entry&)>& fn) {
  std::lock_guard<std::mutex> g(m_);
  return forEachLocked(fn);
}

Status Vault::forEachLocked(const std::function<bool(const Entry&)>& fn) {
  if (Status s = requireUnlocked(); s != Status::Ok) return s;
  for (const auto& slot : slots_) {
    // A fresh copy each time: reusing one would let a longer field reallocate
    // and free the previous entry's buffer unwiped.
    Entry e;
    const bool ok = codec::decode(slot.plain.data(), slot.plain.size(), e);
    const bool more = ok && fn(e);
    wipe(e);
    if (!ok) return Status::Corrupt;
    if (!more) break;
  }
  return Status::Ok;
}

Status Vault::get(uint32_t id, Entry& out) {
  std::lock_guard<std::mutex> g(m_);
  if (Status s = requireUnlocked(); s != Status::Ok) return s;
  Slot* slot = find(id);
  if (!slot) return Status::NotFound;
  if (!codec::decode(slot->plain.data(), slot->plain.size(), out)) return Status::Corrupt;
  out.revision = slot->revision;
  return Status::Ok;
}

Status Vault::seal(uint32_t id, const SecureBuf& plain, std::vector<uint8_t>& file) {
  file.assign(kEntryOverhead + plain.size(), 0);
  file[0] = kEntryVersion;
  if (!p_.crypto.random(file.data() + 1, 12)) return Status::StorageError;
  const std::string aad = entryAad(id);
  if (!p_.crypto.gcmSeal(dek_.data(), file.data() + 1, bytes(aad.c_str()), aad.size(),
                         plain.data(), plain.size(), file.data() + 13))
    return Status::StorageError;
  return Status::Ok;
}

Status Vault::persist(const std::string& path, uint32_t id, const SecureBuf& plain) {
  std::vector<uint8_t> file;
  if (Status s = seal(id, plain, file); s != Status::Ok) return s;
  return writeAtomic(path, file.data(), file.size());
}

Status Vault::writeEntries(const std::vector<Change>& changes) {
  if (!buckets_) {
    for (const Change& c : changes) {
      if (c.plain) {
        if (Status s = persist(entryPath(c.id), c.id, *c.plain); s != Status::Ok) return s;
      } else if (!fs_->remove(entryPath(c.id))) {
        return storageFailure();
      }
    }
    return Status::Ok;
  }
  std::vector<std::vector<const Change*>> byBucket(bucket::kCount);
  for (const Change& c : changes) byBucket[bucket::of(c.id)].push_back(&c);
  std::vector<uint8_t> file;
  bucket::Records records;
  for (uint32_t k = 0; k < bucket::kCount; ++k) {
    if (byBucket[k].empty()) continue;
    switch (fs_->read(bucket::path(k), file)) {
      case Storage::Read::NotFound: records.clear(); break;
      case Storage::Read::Error: return Status::StorageError;
      case Storage::Read::Ok:
        // Rewriting a bucket that does not parse would drop its other entries.
        if (!bucket::parse(file, k, records)) return Status::Corrupt;
        break;
    }
    for (const Change* c : byBucket[k]) {
      const uint32_t id = c->id;
      records.erase(std::remove_if(records.begin(), records.end(),
                                   [id](const bucket::Record& r) { return r.id == id; }),
                    records.end());
      if (!c->plain) continue;
      records.push_back(bucket::Record{id, {}});
      if (Status s = seal(id, *c->plain, records.back().sealed); s != Status::Ok) return s;
    }
    if (records.empty()) {
      if (!fs_->remove(bucket::path(k))) return storageFailure();
      continue;
    }
    const std::vector<uint8_t> out = bucket::encode(records);
    if (Status s = writeAtomic(bucket::path(k), out.data(), out.size()); s != Status::Ok) return s;
  }
  return Status::Ok;
}

Status Vault::stageEntries(const backup::Entries& incoming) {
  if (!buckets_) {
    for (const auto& e : incoming) {
      SecureBuf plain;
      if (!codec::encode(e, plain)) return Status::Full;
      if (Status s = persist(stagedPath(e.id), e.id, plain); s != Status::Ok) return s;
    }
    return Status::Ok;
  }
  // Whole buckets: settleRestore renames them over the old ones.
  std::vector<std::vector<const Entry*>> byBucket(bucket::kCount);
  for (const auto& e : incoming) byBucket[bucket::of(e.id)].push_back(&e);
  for (uint32_t k = 0; k < bucket::kCount; ++k) {
    if (byBucket[k].empty()) continue;
    bucket::Records records;
    for (const Entry* e : byBucket[k]) {
      SecureBuf plain;
      if (!codec::encode(*e, plain)) return Status::Full;
      records.push_back(bucket::Record{e->id, {}});
      if (Status s = seal(e->id, plain, records.back().sealed); s != Status::Ok) return s;
    }
    const std::vector<uint8_t> out = bucket::encode(records);
    if (Status s = writeAtomic(bucket::stagedPath(k), out.data(), out.size()); s != Status::Ok) return s;
  }
  return Status::Ok;
}

Status Vault::store(Entry& rec, bool edited) {
  SecureBuf plain;
  if (!codec::encode(rec, plain)) return Status::Full;  // out of RAM for plaintext
  if (Status s = writeEntries({Change{rec.id, &plain}}); s != Status::Ok) return s;
  if (Slot* slot = find(rec.id)) {
    slot->plain = std::move(plain);
    if (edited) slot->revision = ++revisions_;
  } else {
    slots_.push_back(Slot{rec.id, std::move(plain), ++revisions_});
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
    if (slots_.size() >= maxEntries()) s = Status::Full;
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
  if (s == Status::Ok) s = store(rec, true);
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
  if (Status s = writeEntries({Change{id, nullptr}}); s != Status::Ok) return s;
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
    if (Status w = writeEntries({Change{id, nullptr}}); w != Status::Ok) return w;
    slots_.erase(std::find_if(slots_.begin(), slots_.end(), [id](const Slot& x) { return x.id == id; }));
    if (burned) *burned = true;
    return Status::Ok;
  }
  if (s == Status::Ok) {
    if (now != 0) rec.lastUsed = now;
    s = store(rec, false);  // what typing reads is unchanged
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

Status Vault::exportBackup(const std::string& backupPass, std::string& outJson, bool passkeys,
                           uint32_t counter) {
  outJson.clear();
  return exportBackupStream(backupPass, [&outJson](const char* bytes, size_t n) {
    outJson.append(bytes, n);
    return true;
  }, passkeys, counter);
}

Status Vault::exportBackupStream(const std::string& backupPass,
                                  const std::function<bool(const char*, size_t)>& sink,
                                  bool passkeys, uint32_t counter) {
  std::lock_guard<std::mutex> g(m_);
  if (Status s = requireUnlocked(); s != Status::Ok) return s;
  if (!text::validUtf8(backupPass) || text::codePoints(backupPass) < kMinBackupPass)
    return Status::Invalid;

  backup::Passkeys pk;
  if (passkeys) {
    pk.present = true;
    pk.counter = counter;
    if (Status s = loadPasskeysLocked(); s != Status::Ok) return s;
    if (Status s = readWrapKeysLocked(pk.keys, false); s != Status::Ok) return s;
    pk.records.reserve(passkeys_.size());
    for (const auto& p : passkeys_) pk.records.emplace_back(p.plain.data(), p.plain.data() + p.plain.size());
  }

  uint8_t salt[16] = {}, iv[12] = {};
  const uint32_t iterations = meta_.pass.iterations;
  Key key;
  Status s = p_.crypto.random(salt, sizeof salt) && p_.crypto.random(iv, sizeof iv)
                 ? Status::Ok : Status::StorageError;
  if (s == Status::Ok) s = deriveKey(backupPass, salt, iterations, key);
  auto op = s == Status::Ok ? p_.crypto.gcmStream(key.data(), iv, true) : nullptr;
  mem::zeroize(key.data(), key.size());
  if (!op) return Status::StorageError;

  // These bytes and their order are identical to backup::writeEnvelope().
  const std::string prefix = "{\"format\":\"keyra-backup\",\"v\":3,\"kdf\":{\"alg\":\"pbkdf2-sha256\",\"iter\":" +
      std::to_string(iterations) + ",\"salt\":\"" + text::base64Encode(salt, sizeof salt) +
      "\"},\"iv\":\"" + text::base64Encode(iv, sizeof iv) + "\",\"data\":\"";
  if (!sink(prefix.data(), prefix.size())) return Status::StorageError;
  auto b64 = std::make_unique<Base64Sink>(sink);
  SecureBuf cipher;
  if (!cipher.alloc(4096 + 16)) return Status::StorageError;
  auto emitPlain = [&](const char* data, size_t n) {
    for (size_t off = 0; off < n; off += 4096) {
      const size_t take = std::min<size_t>(4096, n - off);
      size_t produced = 0;
      if (!op->update(bytes(data + off), take, cipher.data(), produced) ||
          !b64->write(cipher.data(), produced)) return false;
    }
    return true;
  };
  if (!emitPlain("{\"entries\":[", 12)) return Status::StorageError;
  bool sendFailed = false;
  size_t i = 0;
  const Status traversed = forEachLocked([&](const Entry& e) {
    SecureString item;
    if (i++) item += ',';
    backup::writeEntry(item, e);
    const bool sent = emitPlain(item.data(), item.size());
    wipe(item);
    if (!sent) sendFailed = true;
    return sent;
  });
  if (traversed != Status::Ok) return traversed;
  if (sendFailed) return Status::StorageError;
  if (!emitPlain("]", 1)) return Status::StorageError;
  if (pk.present) {
    SecureString section;
    backup::writePasskeys(section, pk);
    const bool sent = emitPlain(section.data(), section.size());
    wipe(section);
    if (!sent) return Status::StorageError;
  }
  if (!emitPlain("}", 1)) return Status::StorageError;
  uint8_t tail[16] = {}, tag[16] = {};
  size_t produced = 0;
  if (!op->finish(tail, produced, tag) || !b64->write(tail, produced) || !b64->write(tag, sizeof tag) ||
      !b64->finish() || !sink("\"}", 2)) return Status::StorageError;
  return Status::Ok;
}

Status Vault::readBackup(const std::string& backupPass, const std::string& jsonText,
                         backup::Entries& out, backup::Passkeys& passkeys) {
  out.clear();
  passkeys = backup::Passkeys{};
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
    if (!json::parse(reinterpret_cast<const char*>(plain.data()), plain.size(), root)) return Status::Invalid;
  }
  // v3 wraps the entry array in an object beside the passkeys; v1/v2 are the bare array.
  const json::Value* entries = &root;
  if (env.version >= 3) {
    if (root.type != json::Value::Type::Object) return Status::Invalid;
    entries = root.find("entries");
    const json::Value* pk = root.find("passkeys");
    if (pk && !backup::readPasskeys(*pk, passkeys)) return Status::Invalid;
  }
  if (!entries || entries->type != json::Value::Type::Array) return Status::Invalid;
  if (entries->items.size() > maxEntries()) return Status::Full;

  out.resize(entries->items.size());
  for (size_t i = 0; i < out.size(); ++i) {
    if (!backup::readEntry(entries->items[i], out[i]) || !codec::valid(out[i])) {
      for (auto& e : out) wipe(e);
      out.clear();
      return Status::Invalid;
    }
  }
  return Status::Ok;
}

Status Vault::checkBackup(const std::string& backupPass, const std::string& jsonText, bool replace) {
  std::lock_guard<std::mutex> g(m_);
  if (Status s = requireUnlocked(); s != Status::Ok) return s;
  backup::Entries incoming;
  backup::Passkeys pk;
  PasskeyPlan plan;
  Status s = readBackup(backupPass, jsonText, incoming, pk);
  if (s == Status::Ok) s = planPasskeys(pk, replace, plan);
  for (auto& e : incoming) wipe(e);
  return s;
}

Status Vault::prepareBackupStream(const std::string& backupPass, const BackupSource& source,
                                  BackupImport& out) {
  std::lock_guard<std::mutex> g(m_);
  if (Status s = requireUnlocked(); s != Status::Ok) return s;
  out.impl_ = std::make_unique<BackupImport::Impl>();
  const Status s = backup::readStream(p_.crypto, backupPass, source, maxEntries(),
                                      out.impl_->entries, out.impl_->passkeys);
  if (s != Status::Ok) out.impl_.reset();
  return s;
}

Status Vault::checkPreparedBackup(const BackupImport& backup, bool replace) {
  std::lock_guard<std::mutex> g(m_);
  if (Status s = requireUnlocked(); s != Status::Ok) return s;
  if (!backup.impl_) return Status::Invalid;
  PasskeyPlan plan;
  return planPasskeys(backup.impl_->passkeys, replace, plan);
}

Status Vault::commitPreparedBackup(BackupImport& backup, bool replace, size_t* added,
                                   size_t* updated, PasskeyRestore* passkeys) {
  std::lock_guard<std::mutex> g(m_);
  if (Status s = requireUnlocked(); s != Status::Ok) return s;
  if (!backup.impl_) return Status::Invalid;
  auto prepared = std::move(backup.impl_);  // the authenticated snapshot can be used only once
  return importPreparedLocked(prepared->entries, prepared->passkeys,
                              replace, added, updated, passkeys);
}

Status Vault::importBackup(const std::string& backupPass, const std::string& jsonText,
                           bool replace, size_t* added, size_t* updated, PasskeyRestore* passkeys) {
  std::lock_guard<std::mutex> g(m_);
  if (added) *added = 0;
  if (updated) *updated = 0;
  if (passkeys) *passkeys = PasskeyRestore{};
  if (Status s = requireUnlocked(); s != Status::Ok) return s;

  // 1. Decrypt and validate everything before touching the vault.
  backup::Entries incoming;
  backup::Passkeys pk;
  if (Status s = readBackup(backupPass, jsonText, incoming, pk); s != Status::Ok) return s;
  return importPreparedLocked(incoming, pk, replace, added, updated, passkeys);
}

Status Vault::importPreparedLocked(backup::Entries& incoming, backup::Passkeys& pk,
                                    bool replace, size_t* added, size_t* updated,
                                    PasskeyRestore* passkeys) {
  if (added) *added = 0;
  if (updated) *updated = 0;
  if (passkeys) *passkeys = PasskeyRestore{};
  // Reported whatever happens next.
  if (passkeys) passkeys->present = pk.present, passkeys->counter = pk.counter;
  PasskeyPlan plan;
  if (Status s = planPasskeys(pk, replace, plan); s != Status::Ok) {
    for (auto& e : incoming) wipe(e);
    return s;
  }
  std::vector<Identity> index;  // merge view: existing entries + those this import adds
  std::unordered_map<uint32_t, size_t> byId;
  std::unordered_map<std::string, size_t> byAccount;
  auto cleanup = [&] {
    for (auto& e : incoming) wipe(e);
    for (auto& x : index) wipe(x.title), wipe(x.username), wipe(x.url);
  };

  // 2. Plan ids so the final count is known before anything is written.
  auto taken = [&](uint32_t id) { return byId.find(id) != byId.end(); };
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
    byId.reserve(slots_.size() + incoming.size());
    byAccount.reserve(slots_.size() + incoming.size());
    for (const auto& slot : slots_) {
      Entry e;
      if (!codec::decode(slot.plain.data(), slot.plain.size(), e)) {
        wipe(e);
        cleanup();
        return Status::Corrupt;
      }
      index.push_back(Identity{e.id, e.title, e.username, e.url});
      byId.emplace(e.id, index.size() - 1);
      byAccount.emplace(identityKey(e.title, e.username, e.url), index.size() - 1);
      wipe(e);
    }
  }
  for (auto& e : incoming) {
    Identity* match = nullptr;
    if (!replace) {
      const std::string account = identityKey(e.title, e.username, e.url);
      // An id alone is not enough: another vault may have used it for a different account.
      const auto idIt = byId.find(e.id);
      if (e.id != 0 && idIt != byId.end()) {
        Identity& candidate = index[idIt->second];
        if (candidate.title == e.title && candidate.username == e.username && candidate.url == e.url)
          match = &candidate;
      }
      if (!match) {
        const auto accountIt = byAccount.find(account);
        if (accountIt != byAccount.end()) match = &index[accountIt->second];
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
    byId.emplace(e.id, index.size() - 1);
    byAccount.emplace(identityKey(e.title, e.username, e.url), index.size() - 1);
    ++nAdded;
  }
  if ((replace ? 0 : slots_.size()) + nAdded > maxEntries()) {
    cleanup();
    return Status::Full;
  }

  // The restored passkeys' sites have seen counters up to the backup's (and
  // more, if the Keyra it came from signed since): reserve past them before any
  // of them can reach flash. Nothing is written yet if this fails.
  if (pk.present)
    if (Status s = fidoCounterRaiseLocked(fidoCounterAfterRestore(pk.counter)); s != Status::Ok) {
      cleanup();
      return s;
    }

  // 3a. Replace: stage the new set beside the old one, then commit with one
  // atomic marker write (see settleRestore). A failure or power cut before the
  // marker leaves the old vault; from the marker on, the new one.
  // With a passkeys section the records and wrap keys are staged too and the
  // marker (v2) lists them, so they switch in the same commit.
  if (replace) {
    Status s = removeStaged();
    std::vector<uint8_t> marker{pk.present ? kMarkerVersionPasskeys : kMarkerVersion};
    auto putId = [&marker](uint32_t v) {
      marker.resize(marker.size() + 4);
      putLe(&marker[marker.size() - 4], v, 4);
    };
    if (pk.present) putId(uint32_t(incoming.size()));
    for (const auto& e : incoming) putId(e.id);
    if (s == Status::Ok) s = stageEntries(incoming);
    if (s == Status::Ok && pk.present) {
      std::vector<uint32_t> ids;
      s = stagePasskeys(pk, ids);
      putId(uint32_t(ids.size()));
      for (uint32_t id : ids) putId(id);
      marker.push_back(pk.keys.empty() ? 0 : 1);
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
    if (passkeys) passkeys->added = pk.records.size();
    return Status::Ok;
  }

  // 3b. Merge. Every write is planned first and then made in one batch (one
  // rewrite per bucket in layout 2). A storage failure from here on leaves a
  // partial import (reported).
  struct Write {
    uint32_t id;
    SecureBuf plain;
  };
  std::vector<Write> writes;
  writes.reserve(incoming.size());  // `current` below points into it
  std::unordered_map<uint32_t, size_t> planned;  // id → index in writes
  Status s = Status::Ok;
  for (auto& e : incoming) {
    if (s != Status::Ok) break;
    // What the vault holds for this id at this point of the import: an earlier
    // duplicate in the same backup may already have replaced the stored copy.
    const auto p = planned.find(e.id);
    const Slot* slot = find(e.id);
    const SecureBuf* current = p != planned.end() ? &writes[p->second].plain : slot ? &slot->plain : nullptr;
    auto planWrite = [&](const Entry& rec) {
      SecureBuf plain;
      if (!codec::encode(rec, plain)) return Status::Full;  // out of RAM for plaintext
      if (p != planned.end()) {
        writes[p->second].plain = std::move(plain);
      } else {
        planned.emplace(rec.id, writes.size());
        writes.push_back(Write{rec.id, std::move(plain)});
      }
      return Status::Ok;
    };
    if (!current) {
      s = planWrite(e);
      continue;
    }
    Entry old;
    if (!codec::decode(current->data(), current->size(), old)) {
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
    if (s == Status::Ok) s = planWrite(e);
    if (s == Status::Ok) ++nUpdated;
  }
  cleanup();
  if (s == Status::Ok) {
    std::vector<Change> changes;
    changes.reserve(writes.size());
    for (const Write& w : writes) changes.push_back(Change{w.id, &w.plain});
    s = writeEntries(changes);
  }
  if (s == Status::Ok) {
    for (Write& w : writes) {
      if (Slot* slot = find(w.id)) {
        slot->plain = std::move(w.plain);
        slot->revision = ++revisions_;
      } else {
        slots_.push_back(Slot{w.id, std::move(w.plain), ++revisions_});
      }
    }
  }
  if (s == Status::Ok) s = mergePasskeys(plan);
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
  if (passkeys) passkeys->added = plan.records.size();
  return Status::Ok;
}

Status Vault::nvsErased() {
  std::lock_guard<std::mutex> g(m_);
  uint32_t stored = 0;
  if (!p_.counter.load(stored)) return Status::StorageError;
  if (stored < kFailuresAfterNvsLoss && !p_.counter.store(kFailuresAfterNvsLoss)) return Status::StorageError;
  failures_ = std::max(stored, kFailuresAfterNvsLoss);
  lockedUntilMs_ = p_.clock.monotonicMs() + unlockDelayMs(failures_);
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
  if (onTail_) {
    // Both regions. A power cut part-way leaves either the old vault (the
    // first step makes init redo the partition's erase) or a tail without
    // vault.home beside the tombstone: Corrupt, and another reset finishes.
    const std::string floor = opt_.firmware.empty() ? floor_ : opt_.firmware;
    (void)p_.tail->remove(kOldErasedPath);  // may not even mount; the formats decide
    if (!p_.storage.format() ||
        writeAtomic(p_.storage, kMetaPath, kTombstone, sizeof kTombstone) != Status::Ok ||
        !p_.tail->format())
      return Status::StorageError;
    const std::vector<uint8_t> home = homeFile(floor);
    const uint8_t erased = 1;
    if (writeAtomic(*p_.tail, kHomePath, home.data(), home.size()) != Status::Ok ||
        writeAtomic(*p_.tail, kOldErasedPath, &erased, 1) != Status::Ok)
      return Status::StorageError;
    fs_ = p_.tail;
    buckets_ = true;
    floor_ = floor;
  } else {
    if (!fs_->format()) return Status::StorageError;
    if (buckets_) {  // a single filesystem in layout 2 (host tests, the bench) keeps it
      const std::vector<uint8_t> home = homeFile(floor_);
      if (writeAtomic(kHomePath, home.data(), home.size()) != Status::Ok) return Status::StorageError;
    }
  }
  if (!p_.counter.store(0)) return Status::StorageError;
  failures_ = 0;
  lockedUntilMs_ = 0;
  ready_ = true;
  return Status::Ok;
}

// ---------- where the vault lives (docs/SPEC.md §6.1) ----------

Status Vault::selectHome() {
  fs_ = &p_.storage;
  onTail_ = false;
  buckets_ = false;
  floor_.clear();
  std::vector<uint8_t> b;
  if (!p_.tail) {
    // One filesystem: its vault.home alone says the layout (host tests and the
    // bench use layout 2 this way; on a device only the tail has one).
    if (!fs_->mount()) return Status::StorageError;
    switch (fs_->read(kHomePath, b)) {
      case Storage::Read::NotFound: return Status::Ok;
      case Storage::Read::Error: return Status::StorageError;
      case Storage::Read::Ok: break;
    }
    if (!parseHome(b, floor_)) return Status::Corrupt;
    buckets_ = true;
    return Status::Ok;
  }
  // The tail is the vault once it holds vault.home (the move's commit). A tail
  // that does not mount, or has none, was never committed, unless the
  // partition says the vault moved: then it was lost (or a factory reset was
  // cut short) and only another factory reset goes on, never a guess.
  const bool tailUp = p_.tail->mount();
  const Storage::Read home = tailUp ? p_.tail->read(kHomePath, b) : Storage::Read::NotFound;
  if (home == Storage::Read::Error) return Status::StorageError;
  if (home == Storage::Read::Ok) {
    fs_ = p_.tail;
    onTail_ = buckets_ = true;
    if (!parseHome(b, floor_)) return Status::Corrupt;
    return finishMoveOut();
  }
  if (!p_.storage.mount()) return Status::StorageError;
  if (p_.storage.read(kMetaPath, b) == Storage::Read::Ok && isTombstone(b)) {
    fs_ = p_.tail;
    onTail_ = buckets_ = true;
    return Status::Corrupt;
  }
  return Status::Ok;
}

// Redone at every init until old.erased exists, so a power cut anywhere in it
// is finished at the next boot. The tombstone goes first (one atomic rename),
// then every sector is erased: the DEK never changes, so stale ciphertext
// would still open entries deleted since. Nothing writes to the vault on the
// tail before this has run, so an unmountable tail beside an intact old
// vault can only be a moment after the commit, while both were identical.
Status Vault::finishMoveOut() {
  std::vector<uint8_t> b;
  switch (p_.tail->read(kOldErasedPath, b)) {
    case Storage::Read::Ok: return Status::Ok;
    case Storage::Read::Error: return Status::StorageError;
    case Storage::Read::NotFound: break;
  }
  if (p_.storage.mount() &&
      writeAtomic(p_.storage, kMetaPath, kTombstone, sizeof kTombstone) != Status::Ok)
    return Status::StorageError;
  if (!p_.storage.format()) return Status::StorageError;
  if (Status s = writeAtomic(p_.storage, kMetaPath, kTombstone, sizeof kTombstone); s != Status::Ok) return s;
  const uint8_t erased = 1;
  return writeAtomic(*p_.tail, kOldErasedPath, &erased, 1);
}

std::string Vault::movedBy() {
  std::lock_guard<std::mutex> g(m_);
  return onTail_ ? floor_ : std::string();
}

// Copies ciphertext only (no key needed), one bucket at a time so memory stays
// bounded by the largest bucket. Until vault.home is written on the tail the
// partition is the vault and nothing on it has changed; vault.home is the
// single commit point; finishMoveOut() follows.
Status Vault::migrate(const std::function<void()>& tick) {
  std::lock_guard<std::mutex> g(m_);
  if (!migrationPending()) return Status::Invalid;
  Storage& from = p_.storage;
  Storage& to = *p_.tail;
  auto yield = [&tick] {
    if (tick) tick();
  };

  // 1. What there is. init() has settled any restore and swept *.tmp; anything
  // still in flight means this is not a settled vault, so the move waits.
  std::vector<std::string> copies;                        // paths copied as they are
  std::vector<std::vector<uint32_t>> ids(bucket::kCount);  // e/<id>.bin, by bucket
  for (const char* dir : {"", kEntryDir, "f"}) {
    std::vector<std::string> names;
    if (!from.list(dir, names)) return Status::StorageError;
    for (const auto& n : names) {
      if (endsWith(n, ".tmp") || endsWith(n, ".new") || n == kRestoreMarker || n == kHomePath ||
          n == kOldErasedPath)
        return Status::Invalid;
      uint32_t id;
      if (std::strcmp(dir, kEntryDir) == 0 && parseEntryName(n, id)) {
        ids[bucket::of(id)].push_back(id);
      } else {
        copies.push_back(join(dir, n));  // meta, passkeys, logs, tokens, tags, counter, foreign files
      }
    }
  }
  std::vector<std::string> stray;
  if (!from.list(bucket::kDir, stray)) return Status::StorageError;
  if (!stray.empty()) return Status::Invalid;

  // 2. A clean tail: also drops whatever an interrupted attempt left.
  if (!to.format()) return Status::StorageError;
  yield();

  // 3. Copy, reading each file back at once; keep a digest of every file for
  // the final pass.
  struct Written {
    std::string path;
    std::array<uint8_t, 32> digest;
  };
  std::vector<Written> written;
  std::vector<uint8_t> back;
  auto digest = [this](const std::vector<uint8_t>& b, std::array<uint8_t, 32>& out) {
    static const uint8_t label[] = "keyra/move/v1";
    uint8_t mac[64];
    size_t n = 0;
    const bool ok = p_.crypto.hmac(Hash::Sha256, label, sizeof label - 1, b.data(), b.size(), mac, &n) && n == 32;
    if (ok) std::memcpy(out.data(), mac, 32);
    return ok;
  };
  auto put = [&](const std::string& path, const std::vector<uint8_t>& bytes) {
    if (!to.write(path, bytes.data(), bytes.size())) return storageFailure(to);
    if (to.read(path, back) != Storage::Read::Ok) return Status::StorageError;
    if (back != bytes) return Status::Corrupt;
    written.push_back(Written{path, {}});
    if (!digest(bytes, written.back().digest)) return Status::StorageError;
    yield();
    return Status::Ok;
  };
  std::vector<uint8_t> bytes;
  for (const auto& path : copies) {
    if (from.read(path, bytes) != Storage::Read::Ok) return Status::StorageError;
    yield();
    if (Status s = put(path, bytes); s != Status::Ok) return s;
  }
  bucket::Records records, again;
  for (uint32_t k = 0; k < bucket::kCount; ++k) {
    if (ids[k].empty()) continue;
    records.clear();
    for (uint32_t id : ids[k]) {
      records.push_back(bucket::Record{id, {}});
      if (from.read(entryPath(id), records.back().sealed) != Storage::Read::Ok) return Status::StorageError;
      yield();
    }
    bytes = bucket::encode(records);
    // Each record must come back exactly as its entry file was.
    if (!bucket::parse(bytes, k, again) || again.size() != records.size()) return Status::Corrupt;
    for (size_t i = 0; i < again.size(); ++i)
      if (again[i].id != records[i].id || again[i].sealed != records[i].sealed) return Status::Corrupt;
    if (Status s = put(bucket::path(k), bytes); s != Status::Ok) return s;
  }

  // 4. Read everything back once more: exactly these files, each as written.
  size_t seen = 0;
  for (const char* dir : {"", kEntryDir, "f", bucket::kDir}) {
    std::vector<std::string> names;
    if (!to.list(dir, names)) return Status::StorageError;
    for (const auto& n : names) {
      const std::string path = join(dir, n);
      const auto w =
          std::find_if(written.begin(), written.end(), [&path](const Written& x) { return x.path == path; });
      if (w == written.end()) return Status::Corrupt;
      std::array<uint8_t, 32> d;
      if (to.read(path, back) != Storage::Read::Ok) return Status::StorageError;
      if (!digest(back, d)) return Status::StorageError;
      if (d != w->digest) return Status::Corrupt;
      ++seen;
      yield();
    }
  }
  if (seen != written.size()) return Status::Corrupt;

  // 5. Commit.
  const std::vector<uint8_t> home = homeFile(opt_.firmware);
  if (Status s = writeAtomic(to, kHomePath, home.data(), home.size()); s != Status::Ok) return s;
  fs_ = &to;
  onTail_ = buckets_ = true;
  floor_ = opt_.firmware;
  // 6. The partition: tombstone and erase (redone by init if cut short).
  return finishMoveOut();
}

}  // namespace keyra::vault

