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
constexpr char kMetaAad[] = "keyra/meta/v1";
constexpr uint8_t kMagic[4] = {'K', 'Y', 'R', '1'};
constexpr uint8_t kMetaVersion = 1, kEntryVersion = 1;
constexpr size_t kMetaSize = 4 + 1 + 4 + 16 + 12 + 48;
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

const uint8_t* bytes(const char* s) { return reinterpret_cast<const uint8_t*>(s); }

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
  for (const char* dir : {"", kEntryDir}) {
    std::vector<std::string> names;
    if (!p_.storage.list(dir, names)) return Status::StorageError;
    for (const auto& n : names) {
      if (!endsWith(n, ".tmp")) continue;
      std::string path = *dir ? std::string(dir) + "/" + n : n;
      if (!p_.storage.remove(path)) return Status::StorageError;
    }
  }
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

Status Vault::loadMeta() {
  std::vector<uint8_t> b;
  switch (p_.storage.read(kMetaPath, b)) {
    case Storage::Read::NotFound: return Status::NotInitialized;
    case Storage::Read::Error: return Status::StorageError;
    case Storage::Read::Ok: break;
  }
  if (b.size() != kMetaSize || std::memcmp(b.data(), kMagic, 4) != 0 || b[4] != kMetaVersion)
    return Status::Corrupt;
  Meta m;
  const uint8_t* p = b.data() + 5;
  m.iterations = uint32_t(p[0]) | uint32_t(p[1]) << 8 | uint32_t(p[2]) << 16 | uint32_t(p[3]) << 24;
  p += 4;
  if (m.iterations == 0 || m.iterations > backup::kMaxIterations) return Status::Corrupt;
  std::memcpy(m.salt, p, 16);
  std::memcpy(m.iv, p + 16, 12);
  std::memcpy(m.wrapped, p + 28, 48);
  meta_ = m;
  return Status::Ok;
}

Status Vault::writeMeta(const Meta& m) {
  uint8_t b[kMetaSize];
  std::memcpy(b, kMagic, 4);
  b[4] = kMetaVersion;
  for (int i = 0; i < 4; ++i) b[5 + i] = uint8_t(m.iterations >> (8 * i));
  std::memcpy(b + 9, m.salt, 16);
  std::memcpy(b + 25, m.iv, 12);
  std::memcpy(b + 37, m.wrapped, 48);
  return writeAtomic(kMetaPath, b, sizeof b);
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

Status Vault::wrapDek(const std::string& pass, uint32_t iters, const Key& dek, Meta& out) {
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
  if (!p_.counter.store(0)) return Status::StorageError;
  failures_ = 0;
  lockedUntilMs_ = 0;

  Key dek;
  if (!p_.crypto.random(dek.data(), dek.size())) return Status::StorageError;
  Meta m;
  Status s = wrapDek(passphrase, iters, dek, m);
  if (s == Status::Ok) s = writeMeta(m);
  if (s != Status::Ok) {
    mem::zeroize(dek.data(), dek.size());
    return s;
  }
  meta_ = m;
  dek_ = dek;
  mem::zeroize(dek.data(), dek.size());
  slots_.clear();
  initialized_ = unlocked_ = true;
  return Status::Ok;
}

Status Vault::attempt(const std::string& pass, Key& dek, uint32_t* retryAfterMs) {
  uint64_t now = p_.clock.monotonicMs();
  if (now < lockedUntilMs_) {
    if (retryAfterMs) *retryAfterMs = uint32_t(std::min<uint64_t>(lockedUntilMs_ - now, UINT32_MAX));
    return Status::RateLimited;
  }
  // Persist the attempt before doing any work, so cutting power mid-KDF still counts.
  uint32_t n = failures_ == UINT32_MAX ? failures_ : failures_ + 1;
  if (!p_.counter.store(n)) return Status::StorageError;
  failures_ = n;

  Key kek;
  Status s = deriveKey(pass, meta_.salt, meta_.iterations, kek);
  Crypto::Open r = Crypto::Open::Error;
  if (s == Status::Ok)
    r = p_.crypto.gcmOpen(kek.data(), meta_.iv, bytes(kMetaAad), std::strlen(kMetaAad),
                          meta_.wrapped, sizeof meta_.wrapped, dek.data());
  mem::zeroize(kek.data(), kek.size());
  if (r != Crypto::Open::Ok) mem::zeroize(dek.data(), dek.size());
  if (s != Status::Ok) return s;
  if (r == Crypto::Open::Error) return Status::StorageError;
  if (r == Crypto::Open::AuthFailed) {
    uint32_t delay = unlockDelayMs(n);
    lockedUntilMs_ = p_.clock.monotonicMs() + delay;
    if (retryAfterMs) *retryAfterMs = delay;
    return Status::WrongPassphrase;
  }
  if (!p_.counter.store(0)) {
    mem::zeroize(dek.data(), dek.size());
    return Status::StorageError;
  }
  failures_ = 0;
  lockedUntilMs_ = 0;
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
  if (unlocked_) {  // another client proving the passphrase; state is already loaded
    mem::zeroize(dek.data(), dek.size());
    return Status::Ok;
  }
  dek_ = dek;
  mem::zeroize(dek.data(), dek.size());
  s = loadEntries();
  if (s != Status::Ok) {
    wipeKeys();
    slots_.clear();
    return s;
  }
  unlocked_ = true;
  return Status::Ok;
}

Status Vault::loadEntries() {
  slots_.clear();
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

Status Vault::persist(uint32_t id, const SecureBuf& plain) {
  std::vector<uint8_t> file(kEntryOverhead + plain.size());
  file[0] = kEntryVersion;
  if (!p_.crypto.random(file.data() + 1, 12)) return Status::StorageError;
  const std::string aad = entryAad(id);
  if (!p_.crypto.gcmSeal(dek_.data(), file.data() + 1, bytes(aad.c_str()), aad.size(),
                         plain.data(), plain.size(), file.data() + 13))
    return Status::StorageError;
  return writeAtomic(entryPath(id), file.data(), file.size());
}

Status Vault::store(Entry& rec) {
  SecureBuf plain;
  if (!codec::encode(rec, plain)) return Status::Full;  // out of RAM for plaintext
  if (Status s = persist(rec.id, plain); s != Status::Ok) return s;
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
  } else if (Slot* slot = find(e.id)) {
    if (rec.created == 0 || rec.lastUsed == 0) {
      Entry old;
      if (!codec::decode(slot->plain.data(), slot->plain.size(), old)) s = Status::Corrupt;
      if (rec.created == 0) rec.created = old.created;
      if (rec.lastUsed == 0) rec.lastUsed = old.lastUsed;
      wipe(old);
    }
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

Status Vault::touch(uint32_t id, int64_t now) {
  std::lock_guard<std::mutex> g(m_);
  if (Status s = requireUnlocked(); s != Status::Ok) return s;
  Slot* slot = find(id);
  if (!slot) return Status::NotFound;
  Entry rec;
  Status s = codec::decode(slot->plain.data(), slot->plain.size(), rec) ? Status::Ok : Status::Corrupt;
  if (s == Status::Ok) {
    rec.lastUsed = now;
    s = store(rec);
  }
  wipe(rec);
  return s;
}

Status Vault::changePassphrase(const std::string& cur, const std::string& next) {
  std::lock_guard<std::mutex> g(m_);
  if (Status s = requireUnlocked(); s != Status::Ok) return s;
  if (next.empty()) return Status::Invalid;
  Key dek;
  Status s = attempt(cur, dek, nullptr);  // a wrong `cur` is throttled like unlock
  mem::zeroize(dek.data(), dek.size());
  if (s != Status::Ok) return s;
  Meta m;
  s = wrapDek(next, meta_.iterations, dek_, m);  // only the DEK is re-wrapped
  if (s == Status::Ok) s = writeMeta(m);
  if (s == Status::Ok) meta_ = m;
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
  env.iterations = meta_.iterations;
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

Status Vault::importBackup(const std::string& backupPass, const std::string& jsonText,
                           bool replace, size_t* added, size_t* updated) {
  std::lock_guard<std::mutex> g(m_);
  if (added) *added = 0;
  if (updated) *updated = 0;
  if (Status s = requireUnlocked(); s != Status::Ok) return s;

  backup::Envelope env;
  if (!backup::readEnvelope(jsonText, env)) return Status::Invalid;

  // 1. Decrypt and validate everything before touching the vault.
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

  std::vector<Entry> incoming(root.items.size());
  std::vector<Identity> index;  // merge view: existing entries + those this import adds
  auto cleanup = [&] {
    for (auto& e : incoming) wipe(e);
    for (auto& x : index) wipe(x.title), wipe(x.username), wipe(x.url);
  };
  for (size_t i = 0; i < incoming.size(); ++i) {
    if (!backup::readEntry(root.items[i], incoming[i]) || !codec::valid(incoming[i])) {
      cleanup();
      return Status::Invalid;
    }
  }
  root = json::Value{};

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
      for (auto& x : index)
        if (e.id != 0 && x.id == e.id) match = &x;
      if (!match)
        for (auto& x : index)
          if (x.title == e.title && x.username == e.username && x.url == e.url) match = &x;
    }
    if (match) {
      e.id = match->id;
      match->title = e.title, match->username = e.username, match->url = e.url;
      ++nUpdated;
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

  // 3. Apply. A storage failure from here on leaves a partial import (reported).
  Status s = Status::Ok;
  if (replace) {
    s = removeAllEntryFiles();
    slots_.clear();
  }
  for (auto& e : incoming) {
    if (s != Status::Ok) break;
    s = store(e);
  }
  cleanup();
  if (s != Status::Ok) {
    // RAM must reflect what actually reached flash (replace already dropped it).
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
