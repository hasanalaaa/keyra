// Capacity bench for docs/research/CAPACITY.md. Runs the real vault core and
// entry codec over the real littlefs (the copy esp_littlefs 1.22.3 ships),
// configured like build/sdkconfig: block 4096, read/prog 128, cache 512,
// lookahead 128, block_cycles 512, one 8-byte mtime attribute per file. The
// flash is RAM that counts reads, programs and erases; device times are a model
// of those counts (see kModels), not measurements.
//
// keyra_capacity [capacity|unlock|ops|ram|backup|buckets|migrate|all]
#include <algorithm>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <functional>
#include <memory>
#include <new>
#include <string>
#include <vector>

#include <openssl/crypto.h>

#include "core/backup_format.hpp"
#include "core/bucket.hpp"
#include "core/entry_codec.hpp"
#include "core/secure_buf.hpp"
#include "core/vault_core.hpp"
#include "host_fakes.hpp"
#include "lfs.h"

using namespace keyra::vault;

// ---------------------------------------------------------------- heap meters
namespace {
size_t gSecureLive = 0, gSecurePeak = 0, gSecureAllocs = 0;
}
void* mem::alloc(size_t n) {
  auto* p = static_cast<size_t*>(std::malloc(n + sizeof(size_t)));
  if (!p) return nullptr;
  *p = n;
  gSecureLive += n;
  gSecurePeak = std::max(gSecurePeak, gSecureLive);
  ++gSecureAllocs;
  return p + 1;
}
void mem::free(void* q) {
  if (!q) return;
  auto* p = static_cast<size_t*>(q) - 1;
  gSecureLive -= *p;
  std::free(p);
}
void mem::zeroize(void* p, size_t n) { OPENSSL_cleanse(p, n); }
// api.cpp's free functions are linked for statusName/wipe; the bench builds its own Vaults.
namespace keyra::vault::detail {
Vault& instance();
Vault& instance() { std::abort(); }
}  // namespace keyra::vault::detail

namespace {

// ---------------------------------------------------------------- RAM flash
struct Counts {
  uint64_t reads = 0, readBytes = 0, progs = 0, progBytes = 0, erases = 0;
  uint64_t opens = 0, stats = 0, gcm = 0;
  Counts operator-(const Counts& o) const {
    return {reads - o.reads, readBytes - o.readBytes, progs - o.progs, progBytes - o.progBytes,
            erases - o.erases, opens - o.opens, stats - o.stats, gcm - o.gcm};
  }
};
Counts gCounts;

struct Flash {
  std::vector<uint8_t> mem;
  explicit Flash(size_t blocks) : mem(blocks * 4096, 0xFF) {}
};

int bdRead(const lfs_config* c, lfs_block_t b, lfs_off_t off, void* buf, lfs_size_t n) {
  auto* f = static_cast<Flash*>(c->context);
  std::memcpy(buf, &f->mem[size_t(b) * c->block_size + off], n);
  ++gCounts.reads, gCounts.readBytes += n;
  return 0;
}
int bdProg(const lfs_config* c, lfs_block_t b, lfs_off_t off, const void* buf, lfs_size_t n) {
  auto* f = static_cast<Flash*>(c->context);
  uint8_t* d = &f->mem[size_t(b) * c->block_size + off];
  const auto* s = static_cast<const uint8_t*>(buf);
  for (lfs_size_t i = 0; i < n; ++i) d[i] &= s[i];  // NOR: programming only clears bits
  ++gCounts.progs, gCounts.progBytes += n;
  return 0;
}
int bdErase(const lfs_config* c, lfs_block_t b) {
  auto* f = static_cast<Flash*>(c->context);
  std::memset(&f->mem[size_t(b) * c->block_size], 0xFF, c->block_size);
  ++gCounts.erases;
  return 0;
}
int bdSync(const lfs_config*) { return 0; }

// ---------------------------------------------------------------- Storage over littlefs
// What esp_littlefs + LittleFsStorage do per call (esp_littlefs.c, littlefs_storage.cpp):
//   read:   fopen → lfs_file_opencfg; fstat → lfs_stat(path); fread; fclose
//   write:  fopen("wb") → opencfg(CREAT|TRUNC) + lfs_file_sync; fwrite; fsync → sync; fclose
//   rename: lfs_rename; unlink: lfs_stat + lfs_remove; readdir: lfs_dir_read
// statOnRead=false models reading with open/lseek/read instead (no path lookup after open).
class LfsStorage : public Storage {
 public:
  LfsStorage(Flash& f, size_t blocks, int32_t inlineMax = 0) : flash_(f) {
    cfg_.context = &f;
    cfg_.read = bdRead, cfg_.prog = bdProg, cfg_.erase = bdErase, cfg_.sync = bdSync;
    cfg_.read_size = 128, cfg_.prog_size = 128, cfg_.block_size = 4096;
    cfg_.block_count = lfs_size_t(blocks);
    cfg_.cache_size = 512, cfg_.lookahead_size = 128, cfg_.block_cycles = 512;
    cfg_.inline_max = inlineMax;
  }
  ~LfsStorage() override {
    if (mounted_) lfs_unmount(&lfs_);
  }
  bool statOnRead = true;

  bool mount() override {
    if (mounted_) return true;
    if (lfs_mount(&lfs_, &cfg_) != 0) {
      if (lfs_format(&lfs_, &cfg_) != 0 || lfs_mount(&lfs_, &cfg_) != 0) return false;
    }
    mounted_ = true;
    for (const char* d : {"e", "f", "b"}) {
      int r = lfs_mkdir(&lfs_, d);
      if (r != 0 && r != LFS_ERR_EXIST) return false;
    }
    return true;
  }
  void unmount() {
    if (mounted_) lfs_unmount(&lfs_);
    mounted_ = false;
  }

  Read read(const std::string& path, std::vector<uint8_t>& out) override {
    out.clear();
    lfs_file_t f;
    ++gCounts.opens;
    int r = lfs_file_opencfg(&lfs_, &f, path.c_str(), LFS_O_RDONLY, fileCfg());
    if (r == LFS_ERR_NOENT) return Read::NotFound;
    if (r != 0) return Read::Error;
    lfs_soff_t size;
    if (statOnRead) {
      lfs_info info;
      ++gCounts.stats;
      size = lfs_stat(&lfs_, path.c_str(), &info) == 0 ? lfs_soff_t(info.size) : -1;
    } else {
      size = lfs_file_size(&lfs_, &f);
    }
    bool ok = size >= 0;
    if (ok) {
      out.resize(size_t(size));
      ok = lfs_file_read(&lfs_, &f, out.data(), lfs_size_t(size)) == size;
    }
    ok = lfs_file_close(&lfs_, &f) == 0 && ok;
    return ok ? Read::Ok : Read::Error;
  }

  bool write(const std::string& path, const uint8_t* data, size_t n) override {
    lfs_file_t f;
    ++gCounts.opens;
    if (lfs_file_opencfg(&lfs_, &f, path.c_str(), LFS_O_WRONLY | LFS_O_CREAT | LFS_O_TRUNC, fileCfg()) != 0)
      return false;
    bool ok = lfs_file_sync(&lfs_, &f) == 0;  // esp_littlefs syncs right after opening for write
    ok = ok && lfs_file_write(&lfs_, &f, data, lfs_size_t(n)) == lfs_ssize_t(n) && lfs_file_sync(&lfs_, &f) == 0;
    ok = lfs_file_close(&lfs_, &f) == 0 && ok;
    return ok;
  }
  bool rename(const std::string& from, const std::string& to) override {
    return lfs_rename(&lfs_, from.c_str(), to.c_str()) == 0;
  }
  bool remove(const std::string& path) override {
    lfs_info info;
    ++gCounts.stats;
    int r = lfs_stat(&lfs_, path.c_str(), &info);
    if (r == LFS_ERR_NOENT) return true;
    return r == 0 && lfs_remove(&lfs_, path.c_str()) == 0;
  }
  bool list(const std::string& dir, std::vector<std::string>& names) override {
    names.clear();
    lfs_dir_t d;
    int r = lfs_dir_open(&lfs_, &d, dir.empty() ? "/" : dir.c_str());
    if (r == LFS_ERR_NOENT) return true;
    if (r != 0) return false;
    lfs_info info;
    while ((r = lfs_dir_read(&lfs_, &d, &info)) > 0)
      if (info.type == LFS_TYPE_REG) names.emplace_back(info.name);
    lfs_dir_close(&lfs_, &d);
    return r == 0;
  }
  bool format() override {
    unmount();
    std::fill(flash_.mem.begin(), flash_.mem.end(), 0xFF);
    return lfs_format(&lfs_, &cfg_) == 0 && mount();
  }
  size_t usedBlocks() { return size_t(lfs_fs_size(&lfs_)); }
  size_t blockCount() const { return cfg_.block_count; }

 private:
  lfs_file_config* fileCfg() {
    attr_ = {'t', &mtime_, sizeof mtime_};
    fcfg_ = {};
    fcfg_.attrs = &attr_;
    fcfg_.attr_count = 1;
    return &fcfg_;
  }
  Flash& flash_;
  lfs_config cfg_{};
  lfs_t lfs_{};
  bool mounted_ = false;
  int64_t mtime_ = 1760000000;  // time_t is 64-bit on ESP-IDF 6
  lfs_attr attr_{};
  lfs_file_config fcfg_{};
};

// Counts AES-GCM opens/seals on top of the host OpenSSL crypto.
class CountingCrypto : public test::OpenSslCrypto {
 public:
  bool gcmSeal(const uint8_t key[32], const uint8_t iv[12], const uint8_t* aad, size_t aadLen, const uint8_t* in,
               size_t n, uint8_t* out) override {
    ++gCounts.gcm;
    return OpenSslCrypto::gcmSeal(key, iv, aad, aadLen, in, n, out);
  }
  Open gcmOpen(const uint8_t key[32], const uint8_t iv[12], const uint8_t* aad, size_t aadLen, const uint8_t* in,
               size_t n, uint8_t* out) override {
    ++gCounts.gcm;
    return OpenSslCrypto::gcmOpen(key, iv, aad, aadLen, in, n, out);
  }
};

// ---------------------------------------------------------------- device model
// Flash reads go through esp_partition_read in ≤512-byte calls (DIO, 80 MHz:
// 20 MB/s raw). Program/erase: typical NOR datasheet values (e.g. W25Q128JV
// tPP 0.4 ms per page, tSE 45 ms per 4 KiB sector). Crypto: one PSA key import
// + hardware AES-GCM per entry. All three are assumptions, stated in CAPACITY.md.
struct Model {
  const char* name;
  double readMBs, readCallUs, progUs, eraseMs, gcmUs, decodeUs;
};
constexpr Model kModels[] = {
    {"fast", 20.0, 5.0, 150.0, 30.0, 40.0, 5.0},
    {"slow", 8.0, 25.0, 800.0, 100.0, 150.0, 20.0},
};
double deviceMs(const Counts& c, const Model& m, uint64_t decodes = 0) {
  double us = c.reads * m.readCallUs + double(c.readBytes) / m.readMBs + c.progs * m.progUs +
              c.erases * m.eraseMs * 1000.0 + c.gcm * m.gcmUs + decodes * m.decodeUs;
  return us / 1000.0;
}
std::string modelRange(const Counts& c, uint64_t decodes = 0) {
  char b[64];
  std::snprintf(b, sizeof b, "%.1f–%.1f s", deviceMs(c, kModels[0], decodes) / 1000,
                deviceMs(c, kModels[1], decodes) / 1000);
  return b;
}

// ---------------------------------------------------------------- entries
struct Shape {
  const char* name;
  size_t title, url, user, pass, totp, notes, history;
  bool sequence;
};
// small: a login with nothing else; typical: + TOTP, a short note, 2 old
// passwords; notes: + a 600-byte note; max: every field at its limit.
constexpr Shape kSmall{"small", 14, 32, 24, 20, 0, 0, 0, false};
constexpr Shape kTypical{"typical", 20, 48, 28, 20, 103, 120, 2, false};
constexpr Shape kNotes{"notes600", 20, 48, 28, 20, 103, 600, 3, false};
constexpr Shape kMax{"max", kMaxTitle, kMaxUrl, kMaxUsername, kMaxPassword, kMaxTotp, kMaxNotes, kMaxHistory, true};

std::string filler(size_t n, uint32_t seed) {
  std::string s(n, 'a');
  for (size_t i = 0; i < n; ++i) {
    seed = seed * 1103515245u + 12345u;
    s[i] = char('a' + (seed >> 16) % 26);
  }
  return s;
}

Entry makeEntry(const Shape& sh, uint32_t id) {
  Entry e;
  e.id = id;
  e.title = filler(sh.title, id);
  e.url = filler(sh.url, id + 1);
  e.username = filler(sh.user, id + 2);
  e.password = filler(sh.pass, id + 3);
  e.totp = filler(sh.totp, id + 4);
  e.notes = filler(sh.notes, id + 5);
  e.created = 1700000000, e.updated = 1750000000, e.lastUsed = 1760000000;
  for (size_t i = 0; i < sh.history; ++i) e.history.push_back({filler(sh.pass, id + 10 + i), 1700000000});
  if (sh.sequence) e.sequence = std::string(250, 'x') + "{TAB}";
  return e;
}

constexpr size_t kOverhead = 1 + 12 + 16;  // e/<id>.bin: version | iv | ciphertext | tag

size_t plainSize(const Shape& sh) {
  Entry e = makeEntry(sh, 1);
  return codec::encodedSize(e);
}

std::string hex8(uint32_t id) {
  char b[9];
  std::snprintf(b, sizeof b, "%08x", id);
  return b;
}

// What Vault::persist + writeAtomic put on flash for one entry, without the
// vault's kMaxEntriesFiles check (so the bench can go past 1000).
struct Writer {
  Storage& st;
  Crypto& crypto;
  uint8_t dek[32];
  // The sealed record, as e/<id>.bin holds it and a bucket file embeds it.
  bool seal(const Entry& e, std::vector<uint8_t>& file) {
    if (!codec::valid(e)) return false;
    SecureBuf plain;
    if (!codec::encode(e, plain)) return false;
    file.assign(kOverhead + plain.size(), 0);
    file[0] = 1;
    crypto.random(file.data() + 1, 12);
    const std::string aad = "keyra/e/v1/" + hex8(e.id);
    return crypto.gcmSeal(dek, file.data() + 1, reinterpret_cast<const uint8_t*>(aad.data()), aad.size(),
                          plain.data(), plain.size(), file.data() + 13);
  }
  bool put(const Entry& e) {
    std::vector<uint8_t> file;
    if (!seal(e, file)) return false;
    const std::string path = "e/" + hex8(e.id) + ".bin";
    if (!st.write(path + ".tmp", file.data(), file.size())) return false;
    return st.rename(path + ".tmp", path);
  }
};

constexpr char kPass[] = "correct horse battery staple";
constexpr uint32_t kIters = 1000;

// meta.bin v2 with one passphrase wrap of `dek` (vault_core.hpp layout).
bool writeMeta(Storage& st, Crypto& crypto, const uint8_t dek[32]) {
  uint8_t salt[16], iv[12], kek[32], wrapped[48];
  crypto.random(salt, 16);
  crypto.random(iv, 12);
  if (!crypto.pbkdf2Sha256(kPass, salt, 16, kIters, kek, 32)) return false;
  const char aad[] = "keyra/meta/v1";
  if (!crypto.gcmSeal(kek, iv, reinterpret_cast<const uint8_t*>(aad), sizeof aad - 1, dek, 32, wrapped))
    return false;
  std::vector<uint8_t> b{'K', 'Y', 'R', '1', 2, 1, 1, 80};
  for (int i = 0; i < 4; ++i) b.push_back(uint8_t(kIters >> (8 * i)));
  b.insert(b.end(), salt, salt + 16);
  b.insert(b.end(), iv, iv + 12);
  b.insert(b.end(), wrapped, wrapped + 48);
  return st.write("meta.bin", b.data(), b.size());
}

// A flash with meta.bin and n entries of `sh`; returns how many fit (n if all did).
struct Rig {
  std::unique_ptr<Flash> flash;
  std::unique_ptr<LfsStorage> st;
  CountingCrypto crypto;
  test::MemCounter counter;
  test::FakeClock clock;
  uint8_t dek[32];
  size_t entries = 0;
  Counts lastPut;  // cost of the last entry written

  Rig(size_t blocks, int32_t inlineMax = 0) : flash(new Flash(blocks)), st(new LfsStorage(*flash, blocks, inlineMax)) {
    st->mount();
    crypto.random(dek, 32);
    writeMeta(*st, crypto, dek);
  }
  size_t fill(const Shape& sh, size_t n, std::function<void(size_t)> each = nullptr) {
    Writer w{*st, crypto, {}};
    std::memcpy(w.dek, dek, 32);
    for (size_t i = 0; i < n; ++i) {
      // Odd multiplier: distinct non-zero ids in a scattered order, like random ones.
      Entry e = makeEntry(sh, uint32_t(entries + 1) * 2654435761u);
      const Counts before = gCounts;
      if (!w.put(e)) return i;
      lastPut = gCounts - before;
      ++entries;
      if (each) each(entries);
    }
    return n;
  }
  Vault vault() { return Vault(Platform{*st, counter, crypto, clock}, Vault::Options{kIters}); }
};

double hostMs(const std::function<void()>& f) {
  auto t0 = std::chrono::steady_clock::now();
  f();
  return std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count();
}

// ---------------------------------------------------------------- 1. capacity
constexpr size_t kVaultBlocks = 0x1E0000 / 4096, kTailBlocks = 0x800000 / 4096,
                 kBothBlocks = (0x1E0000 + 0x800000) / 4096;

void capacity() {
  std::printf("\n## 1. Capacity (entries until the first write fails)\n\n");
  std::printf("| shape | plaintext B | file B | inline? | 1.875 MB (480 blk) | 8 MB (2048 blk) | 9.875 MB (2528 blk) |\n");
  std::printf("|---|---|---|---|---|---|---|\n");
  for (const Shape* sh : {&kSmall, &kTypical, &kNotes, &kMax}) {
    const size_t file = plainSize(*sh) + kOverhead;
    std::printf("| %s | %zu | %zu | %s |", sh->name, plainSize(*sh), file, file <= 512 ? "yes" : "no");
    for (size_t blocks : {kVaultBlocks, kTailBlocks, kBothBlocks}) {
      Rig rig(blocks);
      const size_t n = rig.fill(*sh, 1000000, [&](size_t k) {
        if (std::getenv("CAP_TRACE") && k % 500 == 0)
          std::fprintf(stderr, "  %zu entries, %zu/%zu blocks\n", k, rig.st->usedBlocks(), blocks);
      });
      std::printf(" %zu |", n);
      std::fflush(stdout);
    }
    std::printf("\n");
  }
  std::printf("\nInline files off (inline_max = -1), for comparison:\n\n");
  std::printf("| shape | 1.875 MB | 8 MB |\n|---|---|---|\n");
  for (const Shape* sh : {&kSmall, &kTypical}) {
    std::printf("| %s |", sh->name);
    for (size_t blocks : {kVaultBlocks, kTailBlocks}) {
      Rig rig(blocks, -1);
      std::printf(" %zu |", rig.fill(*sh, 1000000));
      std::fflush(stdout);
    }
    std::printf("\n");
  }
}

// ---------------------------------------------------------------- 2/3. unlock and per-op costs
void unlockAt(const Shape& sh, size_t n, size_t blocks) {
  Rig rig(blocks);
  if (rig.fill(sh, n) != n) {
    std::printf("| %s | %zu | (does not fit) |\n", sh.name, n);
    return;
  }
  const Counts put = rig.lastPut;
  for (bool stat : {true, false}) {
    rig.st->statOnRead = stat;
    rig.st->unmount();
    Vault v = rig.vault();
    if (v.init() != Status::Ok) std::abort();
    gSecurePeak = gSecureLive;
    const size_t liveBefore = gSecureLive;
    const Counts before = gCounts;
    uint32_t retry = 0;
    Status s = Status::Ok;
    const double ms = hostMs([&] { s = v.unlock(kPass, &retry); });
    const Counts c = gCounts - before;
    if (s != Status::Ok) {
      std::printf("unlock failed: %s\n", statusName(s));
      std::abort();
    }
    std::printf("| %s | %zu | %s | %llu | %.1f MB | %llu | %.0f ms | %s | %.0f KB |", sh.name, n,
                stat ? "open+stat (now)" : "open only", (unsigned long long)c.reads, c.readBytes / 1e6,
                (unsigned long long)c.gcm, ms, modelRange(c, n).c_str(), (gSecureLive - liveBefore) / 1024.0);
    if (stat) {
      std::printf(" put #%zu: %.0f KB read, %llu progs, %llu erases → %s |\n", n, put.readBytes / 1e3,
                  (unsigned long long)put.progs, (unsigned long long)put.erases, modelRange(put).c_str());
    } else {
      std::printf(" |\n");
    }
    v.lock();
  }
}

void unlock() {
  std::printf("\n## 2. Unlock: reading every entry (PBKDF2 excluded: %u iterations here)\n\n", kIters);
  std::printf("| shape | n | read path | read calls | bytes read | GCM | host | device model | slots RAM | next put |\n");
  std::printf("|---|---|---|---|---|---|---|---|---|---|\n");
  for (size_t n : {100, 500, 1000}) unlockAt(kTypical, n, kVaultBlocks);
  for (size_t n : {1000, 2000, 5000, 10000}) unlockAt(kSmall, n, kBothBlocks);
  for (size_t n : {2000, 5000}) unlockAt(kTypical, n, kBothBlocks);
  unlockAt(kNotes, 400, kVaultBlocks);
  unlockAt(kNotes, 2000, kBothBlocks);
}

// ---------------------------------------------------------------- 4. RAM of list() and summaries
// Device layout (GCC/libstdc++ on Xtensa, 32-bit): std::string is 24 bytes with
// 15 chars inline; vault::Entry is 224 bytes, OldPassword 32. A heap block costs
// its size rounded to 4 plus ~8 bytes of TLSF header.
size_t heapBlock(size_t n) { return ((n + 3) & ~size_t(3)) + 8; }
size_t strHeap(const std::string& s) { return s.size() > 15 ? heapBlock(s.size() + 1) : 0; }
size_t deviceEntryHeap(const Entry& e) {
  size_t n = 0;
  for (const std::string* s : {&e.title, &e.url, &e.username, &e.password, &e.totp, &e.notes, &e.sequence})
    n += strHeap(*s);
  if (!e.history.empty()) n += heapBlock(e.history.size() * 32);
  for (const auto& h : e.history) n += strHeap(h.password);
  return n;
}
size_t deviceSummaryHeap(const Entry& e) { return strHeap(e.title) + strHeap(e.url) + strHeap(e.username); }

void ram() {
  std::printf("\n## 3. RAM per entry (device model, bytes)\n\n");
  std::printf("| shape | slot (PSRAM) | list(): Entry + its heap | of which <16 KB blocks (internal RAM first) | summary (no secrets) |\n");
  std::printf("|---|---|---|---|---|\n");
  for (const Shape* sh : {&kSmall, &kTypical, &kNotes, &kMax}) {
    Entry e = makeEntry(*sh, 0x12345678);
    const size_t slot = 16 + heapBlock(codec::encodedSize(e));
    const size_t heap = deviceEntryHeap(e);
    std::printf("| %s | %zu | %zu | %zu | %zu |\n", sh->name, slot, 224 + heap, heap, 104 + deviceSummaryHeap(e));
  }
  std::printf("\nTotals (slots / list() copy):\n\n| shape | 1000 | 5000 | 10000 |\n|---|---|---|---|\n");
  for (const Shape* sh : {&kSmall, &kTypical, &kNotes}) {
    Entry e = makeEntry(*sh, 0x12345678);
    const size_t slot = 16 + heapBlock(codec::encodedSize(e)), list = 224 + deviceEntryHeap(e);
    std::printf("| %s |", sh->name);
    for (size_t n : {1000, 5000, 10000}) std::printf(" %.2f MB / %.2f MB |", n * slot / 1e6, n * list / 1e6);
    std::printf("\n");
  }
}

// ---------------------------------------------------------------- 5. backup size and memory
void backupBench() {
  std::printf("\n## 4. Backup (exportBackup through the real vault)\n\n");
  std::printf("| shape | n | backup file | peak secure heap during export | host time |\n|---|---|---|---|---|\n");
  for (const Shape* sh : {&kSmall, &kTypical, &kNotes}) {
    for (size_t n : {1000, 5000}) {
      Rig rig(kBothBlocks);
      if (rig.fill(*sh, n) != n) {
        std::printf("| %s | %zu | (does not fit) | | |\n", sh->name, n);
        continue;
      }
      Vault v = rig.vault();
      uint32_t retry;
      if (v.init() != Status::Ok || v.unlock(kPass, &retry) != Status::Ok) std::abort();
      std::string out;
      const size_t base = gSecureLive;
      gSecurePeak = gSecureLive;
      Status s = Status::Ok;
      const double ms = hostMs([&] { s = v.exportBackup("backup passphrase 1", out, false, 0); });
      if (s != Status::Ok) std::abort();
      std::printf("| %s | %zu | %.2f MB | %.2f MB | %.0f ms |\n", sh->name, n, out.size() / 1e6,
                  (gSecurePeak - base) / 1e6, ms);
      Status in = v.checkBackup("backup passphrase 1", out, true);
      if (n > kMaxEntriesFiles && in != Status::Full) std::abort();
    }
  }
}

// ---------------------------------------------------------------- 6. cost of one write as the vault grows
void ops() {
  std::printf("\n## 5. One entry write (tmp + rename) as the directory grows, small entries\n\n");
  std::printf("| entries already there | bytes read | progs | erases | device model |\n|---|---|---|---|---|\n");
  Rig rig(kBothBlocks);
  rig.fill(kSmall, 10000, [&](size_t n) {
    if (n == 100 || n == 1000 || n == 2000 || n == 5000 || n == 10000)
      std::printf("| %zu | %.0f KB | %llu | %llu | %s |\n", n - 1, rig.lastPut.readBytes / 1e3,
                  (unsigned long long)rig.lastPut.progs, (unsigned long long)rig.lastPut.erases,
                  modelRange(rig.lastPut).c_str());
  });
  // A remove (what touch with burnAfter, delete and a replace-restore do) at the end.
  Vault v = rig.vault();
  uint32_t retry;
  if (v.init() != Status::Ok || v.unlock(kPass, &retry) != Status::Ok) std::abort();
  std::vector<Entry> all;
  v.list(all);
  const Counts before = gCounts;
  if (v.remove(all.front().id) != Status::Ok) std::abort();
  const Counts c = gCounts - before;
  std::printf("\nremove at 10000: %.0f KB read, %llu progs → %s\n", c.readBytes / 1e3, (unsigned long long)c.progs,
              modelRange(c).c_str());
  for (auto& e : all) wipe(e);
}

// ---------------------------------------------------------------- 7. layout 2: buckets in the tail
// A tail filesystem (8 MB) holding meta.bin, vault.home and n entries packed
// into the bucket files exactly as Vault::writeEntries writes them.
struct TailRig {
  std::unique_ptr<Flash> flash;
  std::unique_ptr<LfsStorage> st;
  CountingCrypto crypto;
  test::MemCounter counter;
  test::FakeClock clock;
  uint8_t dek[32];

  explicit TailRig(size_t blocks) : flash(new Flash(blocks)), st(new LfsStorage(*flash, blocks)) {
    st->mount();
    crypto.random(dek, 32);
    writeMeta(*st, crypto, dek);
    const uint8_t home[] = {'K', 'Y', 'R', 'H', 1, 0};
    st->write("vault.home", home, sizeof home);
  }
  bool fill(const Shape& sh, size_t n) {
    Writer w{*st, crypto, {}};
    std::memcpy(w.dek, dek, 32);
    std::vector<bucket::Records> buckets(bucket::kCount);
    for (size_t i = 0; i < n; ++i) {
      Entry e = makeEntry(sh, uint32_t(i + 1) * 2654435761u);
      bucket::Record r{e.id, {}};
      if (!w.seal(e, r.sealed)) return false;
      buckets[bucket::of(e.id)].push_back(std::move(r));
    }
    for (uint32_t k = 0; k < bucket::kCount; ++k) {
      if (buckets[k].empty()) continue;
      const auto bytes = bucket::encode(buckets[k]);
      const std::string path = bucket::path(k);
      if (!st->write(path + ".tmp", bytes.data(), bytes.size()) || !st->rename(path + ".tmp", path)) return false;
    }
    return true;
  }
  Vault vault() { return Vault(Platform{*st, counter, crypto, clock}, Vault::Options{kIters}); }
};

void bucketsBench() {
  std::printf("\n## 6. Layout 2 (128 bucket files in the 8 MB tail): unlock and one write\n\n");
  std::printf("| shape | n | read calls | bytes read | GCM | host | device model | next put: read / progs / erases → model |\n");
  std::printf("|---|---|---|---|---|---|---|---|\n");
  for (const auto& [sh, n] : std::vector<std::pair<const Shape*, size_t>>{
           {&kTypical, 1000}, {&kTypical, 5000}, {&kSmall, 5000}, {&kNotes, 5000}}) {
    TailRig rig(kTailBlocks);
    if (!rig.fill(*sh, n)) {
      std::printf("| %s | %zu | (does not fit) |\n", sh->name, n);
      continue;
    }
    rig.st->statOnRead = false;  // LittleFsStorage::read since 0.3.2: open/lseek/read
    rig.st->unmount();
    Vault v = rig.vault();
    if (v.init() != Status::Ok || v.maxEntries() != kMaxEntriesBuckets) std::abort();
    const Counts before = gCounts;
    uint32_t retry = 0;
    Status s = Status::Ok;
    const double ms = hostMs([&] { s = v.unlock(kPass, &retry); });
    const Counts c = gCounts - before;
    if (s != Status::Ok) std::abort();
    Entry e;
    std::vector<Entry> all;
    v.list(all);
    e = all[all.size() / 2];
    e.password = "changed";
    const Counts b2 = gCounts;
    if (v.put(e) != Status::Ok) std::abort();
    const Counts p = gCounts - b2;
    std::printf("| %s | %zu | %llu | %.2f MB | %llu | %.0f ms | %s | %.0f KB / %llu / %llu → %s |\n", sh->name, n,
                (unsigned long long)c.reads, c.readBytes / 1e6, (unsigned long long)c.gcm, ms,
                modelRange(c, n).c_str(), p.readBytes / 1e3, (unsigned long long)p.progs,
                (unsigned long long)p.erases, modelRange(p).c_str());
    for (auto& x : all) wipe(x);
    wipe(e);
  }
}

// The one-time move: n entries in the partition (layout 1) into a fresh tail.
// LfsStorage::format() does not count the device's full-region erases, so they are
// listed apart: the tail (128) and then the partition (30) in 64 KiB block
// erases (esp_flash_erase_region; W25Q128JV tBE2 0.15 s typ., 2 s max).
void migrateBench() {
  std::printf("\n## 7. The move to the tail (Vault::migrate), partition → tail\n\n");
  std::printf("| shape | n | read calls | bytes read | progs | erases | host | device model (+ erasing both regions) |\n");
  std::printf("|---|---|---|---|---|---|---|---|\n");
  for (const auto& [sh, n] : std::vector<std::pair<const Shape*, size_t>>{
           {&kSmall, 1000}, {&kTypical, 1000}, {&kNotes, 440}}) {
    Rig old(kVaultBlocks);
    if (old.fill(*sh, n) != n) {
      std::printf("| %s | %zu | (does not fit) |\n", sh->name, n);
      continue;
    }
    old.st->statOnRead = false;
    old.st->unmount();
    Flash tailFlash(kTailBlocks);
    LfsStorage tail(tailFlash, kTailBlocks);
    tail.statOnRead = false;
    Vault v(Platform{*old.st, old.counter, old.crypto, old.clock, &tail}, Vault::Options{kIters, "bench"});
    if (v.init() != Status::Ok || !v.migrationPending()) std::abort();
    const Counts before = gCounts;
    Status s = Status::Ok;
    const double ms = hostMs([&] { s = v.migrate(); });
    const Counts c = gCounts - before;
    if (s != Status::Ok) {
      std::printf("migrate failed: %s\n", statusName(s));
      std::abort();
    }
    std::printf("| %s | %zu | %llu | %.1f MB | %llu | %llu | %.0f ms | %s + 24–316 s |\n", sh->name, n,
                (unsigned long long)c.reads, c.readBytes / 1e6, (unsigned long long)c.progs,
                (unsigned long long)c.erases, ms, modelRange(c).c_str());
    Vault after(Platform{*old.st, old.counter, old.crypto, old.clock, &tail}, Vault::Options{kIters, "bench"});
    uint32_t retry = 0;
    if (after.init() != Status::Ok || after.unlock(kPass, &retry) != Status::Ok) std::abort();
    std::vector<Entry> all;
    if (after.list(all) != Status::Ok || all.size() != n) std::abort();
    for (auto& x : all) wipe(x);
  }
}

}  // namespace

int main(int argc, char** argv) {
  const std::string what = argc > 1 ? argv[1] : "all";
  std::printf("# keyra capacity bench (littlefs %d.%d, disk %d.%d)\n", LFS_VERSION_MAJOR, LFS_VERSION_MINOR,
              LFS_DISK_VERSION_MAJOR, LFS_DISK_VERSION_MINOR);
  if (what == "capacity" || what == "all") capacity();
  if (what == "unlock" || what == "all") unlock();
  if (what == "ram" || what == "all") ram();
  if (what == "backup" || what == "all") backupBench();
  if (what == "ops" || what == "all") ops();
  if (what == "buckets" || what == "all") bucketsBench();
  if (what == "migrate" || what == "all") migrateBench();
  return 0;
}
