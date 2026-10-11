// Layout 2 itself (core/bucket.hpp, docs/SPEC.md §6.1): the file format, a
// damaged bucket, bucket rewrites across power cuts and I/O errors, and a
// vault of kMaxEntriesBuckets entries with its backup and restore.
#include <functional>
#include <memory>
#include <set>

#include "check.hpp"
#include "core/backup_format.hpp"
#include "rig.hpp"
#include "vault_state.hpp"

using namespace keyra::vault;
using namespace keyra::vault::test;

namespace {

using Files = std::map<std::string, std::vector<uint8_t>>;
constexpr char kBackupPass[] = "backup pass phrase";

std::unique_ptr<Rig> readyBuckets() {
  auto r = std::make_unique<Rig>();
  r->storage.files[kHomePath] = homeFile();
  if ((*r)->init() != Status::Ok || (*r)->setup(kPass) != Status::Ok || (*r)->maxEntries() != kMaxEntriesBuckets)
    std::abort();
  return r;
}

std::vector<uint8_t> raw(std::initializer_list<std::pair<uint32_t, std::vector<uint8_t>>> recs) {
  std::vector<uint8_t> b{1};
  auto le = [&b](uint32_t v) {
    for (int i = 0; i < 4; ++i) b.push_back(uint8_t(v >> (8 * i)));
  };
  le(uint32_t(recs.size()));
  for (const auto& [id, bytes] : recs) {
    le(id);
    le(uint32_t(bytes.size()));
    b.insert(b.end(), bytes.begin(), bytes.end());
  }
  return b;
}

// Ids of a bucket holding at least `n` entries, and one entry outside it.
struct Crowd {
  uint32_t k = 0;
  std::vector<uint32_t> ids;
  uint32_t outside = 0;
};
Crowd crowded(Vault& v, size_t n) {
  std::vector<Entry> all;
  CHECK(v.list(all) == Status::Ok);
  std::map<uint32_t, std::vector<uint32_t>> by;
  for (const auto& e : all) by[bucket::of(e.id)].push_back(e.id);
  Crowd c;
  for (const auto& [k, ids] : by)
    if (ids.size() >= n) {
      c.k = k;
      c.ids = ids;
      break;
    }
  CHECK(c.ids.size() >= n);
  for (const auto& e : all)
    if (bucket::of(e.id) != c.k) c.outside = e.id;
  for (auto& e : all) wipe(e);
  return c;
}

bool entriesWithin(const std::map<uint32_t, Entry>& got, const std::map<uint32_t, Entry>& before,
                   const std::map<uint32_t, Entry>& after) {
  std::set<uint32_t> ids;
  for (const auto& [id, e] : before) ids.insert(id);
  for (const auto& [id, e] : after) ids.insert(id);
  for (uint32_t id : ids) {
    const auto g = got.find(id), b = before.find(id), a = after.find(id);
    const bool asBefore = g == got.end() ? b == before.end() : b != before.end() && same(g->second, b->second);
    const bool asAfter = g == got.end() ? a == after.end() : a != after.end() && same(g->second, a->second);
    if (!asBefore && !asAfter) return false;
  }
  for (const auto& [id, e] : got)
    if (!ids.count(id)) return false;
  return true;
}

}  // namespace

TEST(bucket_format) {
  bucket::Records recs{{0x85, {1, 2, 3}}, {0x05, {4}}, {0x105, {5, 6}}};
  const auto bytes = bucket::encode(recs);
  CHECK(bytes == raw({{0x05, {4}}, {0x85, {1, 2, 3}}, {0x105, {5, 6}}}));  // sorted by id
  bucket::Records back;
  CHECK(bucket::parse(bytes, 5, back) && back.size() == 3 && back[1].id == 0x85 && back[1].sealed == recs[1].sealed);
  CHECK(!bucket::parse(bytes, 6, back));  // ids of another bucket
  auto bad = bytes;
  bad[0] = 2;
  CHECK(!bucket::parse(bad, 5, back));  // unknown version
  bad = bytes;
  bad.push_back(0);
  CHECK(!bucket::parse(bad, 5, back));  // trailing bytes
  bad = bytes;
  bad.pop_back();
  CHECK(!bucket::parse(bad, 5, back));  // truncated
  bad = bytes;
  bad[1] = 4;
  CHECK(!bucket::parse(bad, 5, back));  // count beyond the data
  bad = bytes;
  bad[1] = bad[2] = bad[3] = bad[4] = 0xff;
  CHECK(!bucket::parse(bad, 5, back));
  CHECK(!bucket::parse(raw({{0x85, {1}}, {0x05, {1}}}), 5, back));  // out of order
  CHECK(!bucket::parse(raw({{0x05, {1}}, {0x05, {1}}}), 5, back));  // duplicate
  CHECK(!bucket::parse(raw({{0, {1}}}), 0, back));                   // id 0
  CHECK(!bucket::parse(raw({{0x05, {}}}), 5, back));                 // empty record
  CHECK(!bucket::parse({}, 5, back));
  CHECK(bucket::parse(raw({}), 5, back) && back.empty());
  uint32_t k = 0;
  CHECK(bucket::parseName("7f.bin", k, ".bin") && k == 127);
  CHECK(bucket::parseName("00.new", k, ".new") && k == 0);
  CHECK(!bucket::parseName("80.bin", k, ".bin") && !bucket::parseName("7F.bin", k, ".bin") &&
        !bucket::parseName("7f.new", k, ".bin") && !bucket::parseName("7f.bin.tmp", k, ".bin"));
  CHECK(bucket::path(0x1f) == "b/1f.bin" && bucket::stagedPath(0x1f) == "b/1f.new");
}

// A bucket that does not parse fails the unlock (Corrupt, like a damaged entry
// file), is never rewritten (that would drop its other entries), and leaves
// every other bucket working.
TEST(damaged_bucket_is_corrupt_and_never_rewritten) {
  auto r = readyBuckets();
  for (int i = 0; i < 300; ++i) {
    Entry e = sample("e" + std::to_string(i));
    CHECK((*r)->put(e) == Status::Ok);
  }
  const auto before = stateOf(*r->v);
  const Crowd c = crowded(*r->v, 2);
  const std::string path = bucket::path(c.k);
  r->storage.files[path].push_back(0);  // damaged under the running vault
  const auto damaged = r->storage.files[path];
  Entry e;
  CHECK((*r)->get(c.ids[0], e) == Status::Ok);
  e.password = "changed";
  CHECK((*r)->put(e) == Status::Corrupt);
  CHECK((*r)->remove(c.ids[1]) == Status::Corrupt);
  CHECK((*r)->touch(c.ids[1], 1900000000, false, nullptr) == Status::Corrupt);
  CHECK(r->storage.files[path] == damaged);
  Entry other;
  CHECK((*r)->get(c.outside, other) == Status::Ok);
  other.notes = "elsewhere";
  CHECK((*r)->put(other) == Status::Ok);
  (*r)->lock();
  CHECK((*r)->unlock(kPass, nullptr) == Status::Corrupt && !(*r)->unlocked());
  r->storage.files[path].pop_back();
  CHECK((*r)->unlock(kPass, nullptr) == Status::Ok);
  auto now = stateOf(*r->v);
  CHECK(now.entries.size() == before.entries.size());
  CHECK(now.entries.at(c.outside).notes == "elsewhere" && same(now.entries.at(c.ids[0]), before.entries.at(c.ids[0])));
  // A damaged record (here its tag) is caught by its own authentication.
  r->storage.files[path][r->storage.files[path].size() - 1] ^= 1;
  (*r)->lock();
  CHECK((*r)->unlock(kPass, nullptr) == Status::Corrupt);
  wipe(e), wipe(other);
}

// Power cut after every write, rename and remove of an edit, a delete, a
// burn-after-typing delete and a merge restore, in buckets shared with other
// entries: after the reboot every entry is as before or as after, never lost
// or mixed, and nothing else changed.
TEST(power_cut_and_io_error_during_bucket_rewrites) {
  auto base = readyBuckets();
  for (int i = 0; i < 120; ++i) {
    Entry e = sample("e" + std::to_string(i));
    if (i == 7) e.burnAfter = 1;
    CHECK((*base)->put(e) == Status::Ok);
  }
  const Crowd c = crowded(*base->v, 3);
  uint32_t burner = 0;
  for (const auto& [id, e] : entriesOf(*base->v))
    if (e.burnAfter == 1) burner = id;
  std::string backup;  // the merge: 40 entries edited later and 10 new ones
  {
    auto other = std::make_unique<Rig>();
    other->storage.files = base->storage.files;
    CHECK((*other)->init() == Status::Ok && (*other)->unlock(kPass, nullptr) == Status::Ok);
    int n = 0;
    for (auto [id, e] : entriesOf(*other->v)) {
      if (n++ == 40) break;
      e.password = "merged " + std::to_string(id);
      e.updated += 1000;
      CHECK((*other)->put(e) == Status::Ok);
    }
    for (int i = 0; i < 10; ++i) {
      Entry e = sample("new " + std::to_string(i));
      CHECK((*other)->put(e) == Status::Ok);
    }
    CHECK((*other)->exportBackup(kBackupPass, backup) == Status::Ok);
  }
  const Files files = base->storage.files;
  const auto before = entriesOf(*base->v);

  using Op = std::function<Status(Vault&)>;
  const std::vector<std::pair<const char*, Op>> ops = {
      {"edit", [&](Vault& v) {
         Entry e;
         CHECK(v.get(c.ids[1], e) == Status::Ok);
         e.password = "edited";
         e.updated += 5;
         return v.put(e);
       }},
      {"remove", [&](Vault& v) { return v.remove(c.ids[2]); }},
      {"burn", [&](Vault& v) { return v.touch(burner, 1900000000, true, nullptr); }},
      {"merge", [&](Vault& v) { return v.importBackup(kBackupPass, backup, false, nullptr, nullptr); }},
      {"replace", [&](Vault& v) { return v.importBackup(kBackupPass, backup, true, nullptr, nullptr); }},
  };
  for (const auto& [name, op] : ops) {
    Rig clean;
    clean.storage.files = files;
    CHECK(clean->init() == Status::Ok && clean->unlock(kPass, nullptr) == Status::Ok);
    CHECK(op(*clean.v) == Status::Ok);
    const auto after = entriesOf(*clean.v);
    CHECK(after.size() > 0);
    const bool atomic = std::string(name) == "replace";  // all old or all new
    bool done = false;
    for (int k = 0; !done; ++k) {
      for (bool cut : {true, false}) {
        Rig t;
        t.storage.files = files;
        CHECK(t->init() == Status::Ok && t->unlock(kPass, nullptr) == Status::Ok);
        (cut ? t.storage.crashAfter : t.storage.failAfter) = k;
        const Status s = op(*t.v);
        if (!cut && t->unlocked()) {  // RAM shows what reached flash
          const auto ram = entriesOf(*t.v);
          Rig check;
          check.storage.files = t.storage.files;
          CHECK(check->init() == Status::Ok && check->unlock(kPass, nullptr) == Status::Ok);
          CHECK(entriesWithin(ram, entriesOf(*check.v), entriesOf(*check.v)));
        }
        t.reboot();
        CHECK(t->init() == Status::Ok && t->unlock(kPass, nullptr) == Status::Ok);
        const auto got = entriesOf(*t.v);
        CHECK(entriesWithin(got, before, after));
        if (atomic) {
          auto sameAll = [](const std::map<uint32_t, Entry>& x, const std::map<uint32_t, Entry>& y) {
            return entriesWithin(x, y, y);
          };
          CHECK(sameAll(got, before) || sameAll(got, after));
        }
        for (const auto& f : t.storage.files) CHECK(f.first.find(".tmp") == std::string::npos);
        if (s == Status::Ok) {
          CHECK(entriesWithin(got, after, after));
          if (cut) std::printf("bucket rewrite %s: %d cut points\n", name, k);
          done = true;
        }
      }
      CHECK(k < 1000);
      if (k >= 1000) break;
    }
  }
}

// kMaxEntriesBuckets typical entries: the limit, a reboot, and a backup that
// fits the restore body cap and restores (streamed, replace) identically.
TEST(five_thousand_entries) {
  auto r = readyBuckets();
  // CAPACITY.md's "typical" account: TOTP, a 120-byte note, two old passwords.
  // Filled by a replace-restore (one write per bucket) to keep the test quick.
  SecureString plain = "{\"entries\":[";
  for (size_t i = 0; i < kMaxEntriesBuckets; ++i) {
    Entry e = sample("Account title " + std::to_string(i), "someone" + std::to_string(i) + "@mail.example",
                     "https://login.site" + std::to_string(i % 300) + ".example/signin");
    e.id = uint32_t(i + 1) * 2654435761u;  // distinct, scattered over the buckets
    e.totp = "otpauth://totp/Site:someone?secret=JBSWY3DPEHPK3PXPJBSWY3DPEHPK3PXP&issuer=Site&digits=6&period=30";
    e.notes = std::string(120, char('a' + i % 26));
    e.history = {{"older one " + std::to_string(i), 1690000000}, {"oldest " + std::to_string(i), 1680000000}};
    if (i) plain += ',';
    backup::writeEntry(plain, e);
    wipe(e);
  }
  plain += "]}";
  backup::Envelope env;
  env.iterations = kTestIterations;
  CHECK(r->crypto.random(env.salt, sizeof env.salt) && r->crypto.random(env.iv, sizeof env.iv));
  uint8_t key[32];
  CHECK(r->crypto.pbkdf2Sha256(kBackupPass, env.salt, sizeof env.salt, env.iterations, key, sizeof key));
  env.data.resize(plain.size() + 16);
  CHECK(r->crypto.gcmSeal(key, env.iv, nullptr, 0, reinterpret_cast<const uint8_t*>(plain.data()), plain.size(),
                          env.data.data()));
  wipe(plain);
  size_t filled = 0;
  CHECK((*r)->importBackup(kBackupPass, backup::writeEnvelope(env), true, &filled, nullptr) == Status::Ok);
  CHECK(filled == kMaxEntriesBuckets);
  Entry extra = sample("one too many");
  CHECK((*r)->put(extra) == Status::Full);
  const auto all = stateOf(*r->v);
  CHECK(all.entries.size() == kMaxEntriesBuckets);
  size_t buckets = 0;
  for (const auto& f : r->storage.files) buckets += f.first.compare(0, 2, "b/") == 0;
  CHECK(buckets == bucket::kCount);
  r->reboot();
  CHECK((*r)->init() == Status::Ok && (*r)->unlock(kPass, nullptr) == Status::Ok);
  CHECK(sameState(stateOf(*r->v), all));

  std::string backup;
  CHECK((*r)->exportBackup(kBackupPass, backup) == Status::Ok);
  std::printf("five_thousand_entries: backup %.2f MB\n", backup.size() / 1e6);
  CHECK(backup.size() < 8u * 1024 * 1024);  // keyra_api http::kMaxRestoreBody
  auto dst = readyBuckets();
  BackupImport prepared;
  size_t pos = 0;
  CHECK((*dst)->prepareBackupStream(kBackupPass, [&](char* out, size_t cap, size_t& got) {
          got = std::min({size_t(4096), cap, backup.size() - pos});
          std::copy_n(backup.data() + pos, got, out);
          pos += got;
          return true;
        }, prepared) == Status::Ok);
  size_t added = 0;
  CHECK((*dst)->commitPreparedBackup(prepared, true, &added, nullptr, nullptr) == Status::Ok);
  CHECK(added == kMaxEntriesBuckets);
  dst->reboot();
  CHECK((*dst)->init() == Status::Ok && (*dst)->unlock(kPass, nullptr) == Status::Ok);
  const auto restored = stateOf(*dst->v);
  CHECK(restored.entries.size() == all.entries.size());
  for (const auto& [id, e] : all.entries) CHECK(restored.entries.count(id) && same(restored.entries.at(id), e));
  // Merging the same backup back changes nothing.
  size_t a = 9, u = 9;
  CHECK((*r)->importBackup(kBackupPass, backup, false, &a, &u) == Status::Ok && a == 0 && u == 0);
  // The limit holds after the restore too.
  CHECK((*dst)->remove(all.entries.begin()->first) == Status::Ok);
  Entry e1 = sample("x1"), e2 = sample("x2");
  CHECK((*dst)->put(e1) == Status::Ok && (*dst)->put(e2) == Status::Full);
}

TEST_MAIN()
