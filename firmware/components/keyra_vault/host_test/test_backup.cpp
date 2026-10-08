// Encrypted backup export/import: round trip, wrong passphrase, merge vs replace,
// validation before mutation.
#include <algorithm>

#include "check.hpp"
#include "core/json.hpp"
#include "core/text.hpp"
#include "rig.hpp"

using namespace keyra::vault;
using namespace keyra::vault::test;

static const std::string kBackupPass = "backup pass phrase";

static std::vector<Entry> sorted(Vault& v) {
  std::vector<Entry> all;
  v.list(all);
  std::sort(all.begin(), all.end(), [](const Entry& a, const Entry& b) { return a.title < b.title; });
  return all;
}

TEST(export_requires_long_pass_and_unlock) {
  auto r = Rig::ready();
  std::string out;
  CHECK((*r)->exportBackup("short", out) == Status::Invalid);
  CHECK((*r)->exportBackup("12345678901", out) == Status::Invalid);    // 11 chars
  CHECK((*r)->exportBackup("١٢٣٤٥٦٧٨٩٠١", out) == Status::Invalid);  // 11 Arabic digits (22 bytes)
  CHECK((*r)->exportBackup("١٢٣٤٥٦٧٨٩٠١٢", out) == Status::Ok);      // 12 chars
  (*r)->lock();
  CHECK((*r)->exportBackup(kBackupPass, out) == Status::Locked);
  size_t a, u;
  CHECK((*r)->importBackup(kBackupPass, out, false, &a, &u) == Status::Locked);
}

TEST(envelope_shape) {
  auto r = Rig::ready();
  std::string out;
  CHECK((*r)->exportBackup(kBackupPass, out) == Status::Ok);
  json::Value v;
  CHECK(json::parse(out.data(), out.size(), v));
  CHECK(v.find("format") && v.find("format")->s == "keyra-backup");
  CHECK(v.find("v") && v.find("v")->i == 2);
  const json::Value* kdf = v.find("kdf");
  CHECK(kdf && kdf->find("alg")->s == "pbkdf2-sha256" && kdf->find("iter")->i == kTestIterations);
  std::vector<uint8_t> salt;
  CHECK(text::base64Decode(std::string(kdf->find("salt")->s.c_str()), salt) && salt.size() == 16);
  CHECK(out.find("pw-") == std::string::npos);
}

TEST(round_trip_into_fresh_vault) {
  auto src = Rig::ready();
  Entry a = sample("حساب عربي", "مستخدم");
  a.totp = "otpauth://totp/X?secret=JBSWY3DPEHPK3PXP";
  a.favorite = true;
  a.lastUsed = 1750000000;
  a.notes = "multi\nline \"notes\" \\ \x01 ctrl";
  Entry b = sample("plain");
  CHECK((*src)->put(a) == Status::Ok && (*src)->put(b) == Status::Ok);
  std::string backup;
  CHECK((*src)->exportBackup(kBackupPass, backup) == Status::Ok);

  auto dst = Rig::ready();
  size_t added = 0, updated = 0;
  CHECK((*dst)->importBackup("not the pass phrase", backup, false, &added, &updated) ==
        Status::WrongPassphrase);
  CHECK((*dst)->importBackup(kBackupPass, backup, true, &added, &updated) == Status::Ok);
  CHECK(added == 2 && updated == 0);
  auto want = sorted(*src->v), got = sorted(*dst->v);
  CHECK(want.size() == got.size());
  for (size_t i = 0; i < want.size() && i < got.size(); ++i) CHECK(same(want[i], got[i]));

  // Survives a reboot of the destination.
  dst->reboot();
  CHECK((*dst)->init() == Status::Ok && (*dst)->unlock(kPass, nullptr) == Status::Ok);
  got = sorted(*dst->v);
  for (size_t i = 0; i < want.size() && i < got.size(); ++i) CHECK(same(want[i], got[i]));
}

TEST(merge_vs_replace) {
  auto src = Rig::ready();
  Entry x = sample("x"), y = sample("y"), z = sample("z");
  CHECK((*src)->put(x) == Status::Ok && (*src)->put(y) == Status::Ok && (*src)->put(z) == Status::Ok);

  // Destination shares x by id (restored onto the same device) and y only by
  // (title, username, url); z is new; w exists only in the destination.
  auto dst = Rig::ready();
  std::string backup;
  CHECK((*src)->exportBackup(kBackupPass, backup) == Status::Ok);
  CHECK((*dst)->importBackup(kBackupPass, backup, true, nullptr, nullptr) == Status::Ok);
  Entry xd;
  CHECK((*dst)->get(x.id, xd) == Status::Ok);
  xd.password = "local edit";
  CHECK((*dst)->put(xd) == Status::Ok);
  std::vector<Entry> cur;
  (*dst)->list(cur);
  for (auto& e : cur)
    if (e.title == "y") CHECK((*dst)->remove(e.id) == Status::Ok);
  Entry yLocal = sample("y");
  yLocal.password = "y local";
  Entry w = sample("w");
  CHECK((*dst)->put(yLocal) == Status::Ok && (*dst)->put(w) == Status::Ok);
  for (auto& e : cur)
    if (e.title == "z") CHECK((*dst)->remove(e.id) == Status::Ok);

  size_t added = 0, updated = 0;
  CHECK((*dst)->importBackup(kBackupPass, backup, false, &added, &updated) == Status::Ok);
  CHECK(added == 1 && updated == 0);  // x and y matched, but the local copies are no older
  auto all = sorted(*dst->v);
  CHECK(all.size() == 4);  // w, x, y, z
  Entry got;
  CHECK((*dst)->get(x.id, got) == Status::Ok && got.password == "local edit");
  CHECK((*dst)->get(yLocal.id, got) == Status::Ok && got.password == "y local");  // kept local id
  CHECK((*dst)->get(w.id, got) == Status::Ok);

  CHECK((*dst)->importBackup(kBackupPass, backup, true, &added, &updated) == Status::Ok);
  CHECK(added == 3 && updated == 0);
  all = sorted(*dst->v);
  CHECK(all.size() == 3);
  CHECK((*dst)->get(w.id, got) == Status::NotFound);
}

TEST(invalid_backups_change_nothing) {
  auto r = Rig::ready();
  Entry keep = sample("keep");
  CHECK((*r)->put(keep) == Status::Ok);
  auto snapshot = r->storage.files;
  size_t a = 7, u = 7;
  auto env = [](const char* v, const char* iter, const char* salt) {
    return std::string("{\"format\":\"keyra-backup\",\"v\":") + v +
           ",\"kdf\":{\"alg\":\"pbkdf2-sha256\",\"iter\":" + iter + ",\"salt\":\"" + salt +
           "\"},\"iv\":\"AAAAAAAAAAAAAAAA\",\"data\":\"AAAAAAAAAAAAAAAAAAAAAA==\"}";
  };
  const std::string bad[] = {
      "",
      "{}",
      "[]",
      "{\"format\":\"other\",\"v\":1}",
      env("3", "1000", "AAAAAAAAAAAAAAAAAAAAAA=="),  // from a future firmware
      env("0", "1000", "AAAAAAAAAAAAAAAAAAAAAA=="),
      env("1", "0", "AAAAAAAAAAAAAAAAAAAAAA=="),
      env("1", "10000001", "AAAAAAAAAAAAAAAAAAAAAA=="),
      env("1", "1000", "AAAA"),
  };
  for (const auto& b : bad) {
    CHECK((*r)->importBackup(kBackupPass, b, true, &a, &u) == Status::Invalid);
    CHECK(a == 0 && u == 0);
  }
  // Authentic envelope, tampered ciphertext → indistinguishable from a wrong pass.
  std::string backup;
  CHECK((*r)->exportBackup(kBackupPass, backup) == Status::Ok);
  size_t pos = backup.find("\"data\":\"") + 9;
  backup[pos] = backup[pos] == 'A' ? 'B' : 'A';
  CHECK((*r)->importBackup(kBackupPass, backup, true, &a, &u) == Status::WrongPassphrase);
  CHECK(r->storage.files == snapshot);
}

// Builds a backup around arbitrary plaintext with the test crypto.
static std::string seal(Crypto& c, const std::string& plain, int version = 1) {
  uint8_t salt[16] = {1}, iv[12] = {2}, key[32];
  c.pbkdf2Sha256(kBackupPass, salt, 16, 1000, key, 32);
  std::vector<uint8_t> data(plain.size() + 16);
  c.gcmSeal(key, iv, nullptr, 0, reinterpret_cast<const uint8_t*>(plain.data()), plain.size(),
            data.data());
  return "{\"format\":\"keyra-backup\",\"v\":" + std::to_string(version) +
         ",\"kdf\":{\"alg\":\"pbkdf2-sha256\",\"iter\":1000,"
         "\"salt\":\"" + text::base64Encode(salt, 16) + "\"},\"iv\":\"" + text::base64Encode(iv, 12) +
         "\",\"data\":\"" + text::base64Encode(data.data(), data.size()) + "\"}";
}

TEST(invalid_entries_reject_whole_import) {
  auto r = Rig::ready();
  Entry keep = sample("keep");
  CHECK((*r)->put(keep) == Status::Ok);
  auto snapshot = r->storage.files;
  const std::string tooLong(kMaxTitle + 1, 'a');
  const std::string cases[] = {
      "{\"title\":\"ok\"}",                                       // not an array
      "[{\"title\":\"ok\"},{\"title\":\"" + tooLong + "\"}]",     // over limit
      "[{\"title\":5}]",                                          // wrong type
      "[{\"title\":\"ok\",\"favorite\":\"yes\"}]",
      "[{\"id\":-1}]",
      "[{\"id\":4294967296}]",
      "[{\"title\":\"\\ud800\"}]",                                // lone surrogate
      "[{\"title\":\"ok\"},]",
  };
  for (const auto& c : cases)
    CHECK((*r)->importBackup(kBackupPass, seal(r->crypto, c), true, nullptr, nullptr) ==
          Status::Invalid);
  CHECK(r->storage.files == snapshot);

  // Minimal objects: missing members default, unknown members are ignored.
  size_t a = 0, u = 0;
  CHECK((*r)->importBackup(kBackupPass,
                           seal(r->crypto, "[{\"title\":\"min\",\"future\":[1,{\"x\":null}]}]"),
                           false, &a, &u) == Status::Ok);
  CHECK(a == 1 && u == 0);
}

TEST(import_respects_entry_limit) {
  auto r = Rig::ready();
  std::string many = "[";
  for (size_t i = 0; i < kMaxEntries; ++i) many += (i ? "," : "") + std::string("{\"title\":\"t") + std::to_string(i) + "\"}";
  many += "]";
  Entry existing = sample("existing");
  CHECK((*r)->put(existing) == Status::Ok);
  auto snapshot = r->storage.files;
  CHECK((*r)->importBackup(kBackupPass, seal(r->crypto, many), false, nullptr, nullptr) ==
        Status::Full);
  CHECK(r->storage.files == snapshot);
  size_t a = 0;
  CHECK((*r)->importBackup(kBackupPass, seal(r->crypto, many), true, &a, nullptr) == Status::Ok);
  CHECK(a == kMaxEntries);
  std::string tooMany = many.substr(0, many.size() - 1) + ",{\"title\":\"extra\"}]";
  CHECK((*r)->importBackup(kBackupPass, seal(r->crypto, tooMany), true, nullptr, nullptr) ==
        Status::Full);
}

static std::vector<Entry> byId(Vault& v) {
  std::vector<Entry> all;
  v.list(all);
  std::sort(all.begin(), all.end(), [](const Entry& a, const Entry& b) { return a.id < b.id; });
  return all;
}

static bool sameSet(const std::vector<Entry>& a, const std::vector<Entry>& b) {
  if (a.size() != b.size()) return false;
  for (size_t i = 0; i < a.size(); ++i)
    if (!same(a[i], b[i])) return false;
  return true;
}

// A vault whose entries differ from `backup` in every way a replace must handle:
// an entry only in the backup, one only in the vault, and an id in both with
// different contents.
struct ReplaceCase {
  std::map<std::string, std::vector<uint8_t>> files;  // the vault on flash
  std::string backup;
  std::vector<Entry> oldSet, newSet;
};

static ReplaceCase replaceCase() {
  ReplaceCase c;
  auto r = Rig::ready();
  Entry a = sample("a"), b = sample("b"), cc = sample("c"), d = sample("d");
  CHECK((*r)->put(a) == Status::Ok && (*r)->put(b) == Status::Ok && (*r)->put(cc) == Status::Ok);
  CHECK((*r)->exportBackup(kBackupPass, c.backup) == Status::Ok);
  c.newSet = byId(*r->v);
  CHECK((*r)->remove(a.id) == Status::Ok);
  b.password = "b changed";
  b.updated += 10;
  CHECK((*r)->put(b) == Status::Ok && (*r)->put(d) == Status::Ok);
  c.oldSet = byId(*r->v);
  c.files = r->storage.files;
  return c;
}

static bool restoreLeftovers(const MemStorage& s) {
  for (const auto& f : s.files)
    if (f.first == "restore.commit" || f.first.find(".new") != std::string::npos) return true;
  return false;
}

// Power cut at every mutating storage operation of a replace: after reboot the
// vault holds exactly the old entries or exactly the backup's, never a mix.
TEST(replace_is_atomic_across_power_cuts) {
  const ReplaceCase c = replaceCase();
  bool sawOld = false, sawNew = false;
  for (int k = 0; k < 100; ++k) {
    Rig t;
    t.storage.files = c.files;
    CHECK(t->init() == Status::Ok && t->unlock(kPass, nullptr) == Status::Ok);
    t.storage.crashAfter = k;
    const Status s = t->importBackup(kBackupPass, c.backup, true, nullptr, nullptr);
    if (s != Status::Ok) CHECK(!t->unlocked());  // never shows RAM that flash may not match
    t.reboot();
    CHECK(t->init() == Status::Ok && t->unlock(kPass, nullptr) == Status::Ok);
    CHECK(!restoreLeftovers(t.storage));
    const auto got = byId(*t.v);
    const bool isOld = sameSet(got, c.oldSet), isNew = sameSet(got, c.newSet);
    CHECK(isOld || isNew);
    sawOld |= isOld;
    sawNew |= isNew;
    if (s == Status::Ok) {
      CHECK(isNew);
      CHECK(k > 10);  // the sweep really covered the staging and the commit
      break;
    }
    CHECK(k < 99);
  }
  CHECK(sawOld && sawNew);
}

// The same sweep with a single failing operation (the device keeps running).
TEST(replace_is_atomic_across_io_errors) {
  const ReplaceCase c = replaceCase();
  for (int k = 0; k < 100; ++k) {
    Rig t;
    t.storage.files = c.files;
    CHECK(t->init() == Status::Ok && t->unlock(kPass, nullptr) == Status::Ok);
    t.storage.failAfter = k;
    const Status s = t->importBackup(kBackupPass, c.backup, true, nullptr, nullptr);
    if (t->unlocked()) {
      const auto ram = byId(*t.v);
      CHECK(s == Status::Ok ? sameSet(ram, c.newSet) : sameSet(ram, c.oldSet) || sameSet(ram, c.newSet));
    }
    t.reboot();
    CHECK(t->init() == Status::Ok && t->unlock(kPass, nullptr) == Status::Ok);
    CHECK(!restoreLeftovers(t.storage));
    const auto got = byId(*t.v);
    CHECK(sameSet(got, c.oldSet) || sameSet(got, c.newSet));
    if (s == Status::Ok) {
      CHECK(sameSet(got, c.newSet));
      break;
    }
    CHECK(k < 99);
  }
}

TEST(full_storage_during_replace_keeps_old_vault) {
  const ReplaceCase c = replaceCase();
  Rig t;
  t.storage.files = c.files;
  CHECK(t->init() == Status::Ok && t->unlock(kPass, nullptr) == Status::Ok);
  t.storage.failAfter = 2;  // the second staged entry does not fit
  size_t a = 9;
  CHECK(t->importBackup(kBackupPass, c.backup, true, &a, nullptr) == Status::StorageError);
  CHECK(a == 0);
  CHECK(t.storage.files == c.files);  // staged files are gone again
  CHECK(t->unlocked() && sameSet(byId(*t.v), c.oldSet));
  // With the medium gone entirely, the vault locks rather than show stale RAM.
  t.storage.crashAfter = 0;
  CHECK(t->importBackup(kBackupPass, c.backup, true, nullptr, nullptr) == Status::StorageError);
  CHECK(!t->unlocked());
  t.reboot();
  CHECK(t->init() == Status::Ok && t->unlock(kPass, nullptr) == Status::Ok);
  CHECK(sameSet(byId(*t.v), c.oldSet));
}

TEST(replace_with_empty_backup_empties_vault) {
  auto r = Rig::ready();
  Entry e = sample("gone");
  CHECK((*r)->put(e) == Status::Ok);
  size_t a = 9;
  CHECK((*r)->importBackup(kBackupPass, seal(r->crypto, "[]"), true, &a, nullptr) == Status::Ok && a == 0);
  CHECK(byId(*r->v).empty());
  r->reboot();
  CHECK((*r)->init() == Status::Ok && (*r)->unlock(kPass, nullptr) == Status::Ok);
  CHECK(byId(*r->v).empty() && !restoreLeftovers(r->storage));
}

// A staged file without the commit marker (power cut before the commit) is discarded.
TEST(uncommitted_staging_is_discarded) {
  auto r = Rig::ready();
  Entry e = sample("kept");
  CHECK((*r)->put(e) == Status::Ok);
  const auto bin = std::find_if(r->storage.files.begin(), r->storage.files.end(),
                                [](const auto& f) { return f.first.compare(0, 2, "e/") == 0; });
  CHECK(bin != r->storage.files.end());
  r->storage.files["e/0000002a.new"] = bin->second;
  r->reboot();
  CHECK((*r)->init() == Status::Ok && !restoreLeftovers(r->storage));
  CHECK((*r)->unlock(kPass, nullptr) == Status::Ok && byId(*r->v).size() == 1);
}

// Merge: a backup older than the vault's copy never overwrites it.
TEST(merge_keeps_newer_local_entry) {
  auto r = Rig::ready();
  Entry e = sample("mail");
  CHECK((*r)->put(e) == Status::Ok);
  std::string backup;
  CHECK((*r)->exportBackup(kBackupPass, backup) == Status::Ok);
  e.password = "changed later";
  e.updated = 1800000000;
  CHECK((*r)->put(e) == Status::Ok);
  size_t a = 9, u = 9;
  CHECK((*r)->importBackup(kBackupPass, backup, false, &a, &u) == Status::Ok);
  CHECK(a == 0 && u == 0);
  Entry got;
  CHECK((*r)->get(e.id, got) == Status::Ok && got.password == "changed later" && got.updated == 1800000000);
  CHECK(got.history.size() == 1 && got.history[0].password == "pw-mail");
}

// Merge: a newer backup wins, and the local password it replaces is kept in history.
TEST(merge_newer_backup_keeps_local_password_in_history) {
  auto src = Rig::ready();
  Entry e = sample("bank");  // pw-bank, updated 1700000001
  CHECK((*src)->put(e) == Status::Ok);
  std::string older;
  CHECK((*src)->exportBackup(kBackupPass, older) == Status::Ok);
  e.password = "newer";
  e.updated = 1800000000;
  CHECK((*src)->put(e) == Status::Ok);
  std::string newer;  // history: [pw-bank]
  CHECK((*src)->exportBackup(kBackupPass, newer) == Status::Ok);

  // The local password is already in the backup's history: not repeated.
  auto dst = Rig::ready();
  CHECK((*dst)->importBackup(kBackupPass, older, true, nullptr, nullptr) == Status::Ok);
  size_t a = 9, u = 9;
  CHECK((*dst)->importBackup(kBackupPass, newer, false, &a, &u) == Status::Ok && a == 0 && u == 1);
  Entry got;
  CHECK((*dst)->get(e.id, got) == Status::Ok && got.password == "newer" && got.updated == 1800000000);
  CHECK(got.history.size() == 1 && got.history[0].password == "pw-bank");

  // A local password the backup never saw goes in front of the backup's history.
  CHECK((*dst)->importBackup(kBackupPass, older, true, nullptr, nullptr) == Status::Ok);
  Entry local;
  CHECK((*dst)->get(e.id, local) == Status::Ok);
  local.password = "local only";
  local.updated = 1750000000;  // newer than `older`, older than `newer`
  CHECK((*dst)->put(local) == Status::Ok);
  CHECK((*dst)->importBackup(kBackupPass, newer, false, &a, &u) == Status::Ok && a == 0 && u == 1);
  CHECK((*dst)->get(e.id, got) == Status::Ok && got.password == "newer");
  CHECK(got.history.size() == 2 && got.history[0].password == "local only" &&
        got.history[0].changedAt == 1800000000 && got.history[1].password == "pw-bank");
}

// Merge: the same id for a different account (another vault) is a different entry.
TEST(merge_id_collision_keeps_both) {
  auto r = Rig::ready();
  CHECK((*r)->importBackup(kBackupPass, seal(r->crypto, "[{\"id\":42,\"title\":\"local\",\"password\":\"L\"}]"),
                           true, nullptr, nullptr) == Status::Ok);
  size_t a = 9, u = 9;
  CHECK((*r)->importBackup(kBackupPass,
                           seal(r->crypto, "[{\"id\":42,\"title\":\"other\",\"password\":\"O\",\"updated\":5}]"),
                           false, &a, &u) == Status::Ok);
  CHECK(a == 1 && u == 0);
  auto all = sorted(*r->v);
  CHECK(all.size() == 2);
  if (all.size() == 2) {
    CHECK(all[0].title == "local" && all[0].id == 42 && all[0].password == "L");
    CHECK(all[1].title == "other" && all[1].id != 42 && all[1].password == "O");
  }
}

TEST(check_backup_writes_nothing) {
  auto r = Rig::ready();
  Entry e = sample("keep");
  CHECK((*r)->put(e) == Status::Ok);
  std::string backup;
  CHECK((*r)->exportBackup(kBackupPass, backup) == Status::Ok);
  const auto snapshot = r->storage.files;
  CHECK((*r)->checkBackup(kBackupPass, backup) == Status::Ok);
  CHECK((*r)->checkBackup("not the pass phrase", backup) == Status::WrongPassphrase);
  CHECK((*r)->checkBackup(kBackupPass, "{}") == Status::Invalid);
  CHECK((*r)->checkBackup(kBackupPass, seal(r->crypto, "[{\"title\":5}]")) == Status::Invalid);
  std::string many = "[";
  for (size_t i = 0; i <= kMaxEntries; ++i) many += (i ? "," : "") + std::string("{}");
  CHECK((*r)->checkBackup(kBackupPass, seal(r->crypto, many + "]")) == Status::Full);
  CHECK(r->storage.files == snapshot);
  (*r)->lock();
  CHECK((*r)->checkBackup(kBackupPass, backup) == Status::Locked);
}

TEST(duplicates_inside_one_import_merge) {
  auto r = Rig::ready();
  size_t a = 0, u = 0;
  std::string plain =
      "[{\"title\":\"t\",\"username\":\"u\",\"password\":\"1\",\"updated\":1},"
      "{\"title\":\"t\",\"username\":\"u\",\"password\":\"2\",\"updated\":2},"
      "{\"title\":\"t\",\"username\":\"u\",\"password\":\"3\",\"updated\":2}]";
  CHECK((*r)->importBackup(kBackupPass, seal(r->crypto, plain), false, &a, &u) == Status::Ok);
  CHECK(a == 1 && u == 1);  // the newest copy wins; a tie keeps the one already there
  std::vector<Entry> all;
  CHECK((*r)->list(all) == Status::Ok && all.size() == 1 && all[0].password == "2");
}

// SPEC §9.3: history lives inside the encrypted entry, so it travels with backups.
TEST(history_round_trips_through_backup) {
  auto src = Rig::ready();
  Entry e = sample("bank");
  CHECK((*src)->put(e) == Status::Ok);
  for (int i = 1; i <= 3; ++i) {
    e.password = "bank-" + std::to_string(i);
    e.updated = 1800000000 + i;
    CHECK((*src)->put(e) == Status::Ok);
  }
  Entry want;
  CHECK((*src)->get(e.id, want) == Status::Ok && want.history.size() == 3);
  std::string backup;
  CHECK((*src)->exportBackup(kBackupPass, backup) == Status::Ok);
  CHECK(backup.find("bank-") == std::string::npos);  // history is inside the ciphertext

  auto dst = Rig::ready();
  size_t a = 0, u = 0;
  CHECK((*dst)->importBackup(kBackupPass, backup, true, &a, &u) == Status::Ok && a == 1);
  Entry got;
  CHECK((*dst)->get(e.id, got) == Status::Ok && same(got, want));
  CHECK(got.history[0].password == "bank-2" && got.history[0].changedAt == 1800000003);
  CHECK(got.history[2].password == "pw-bank" && got.history[2].changedAt == 1800000001);
}

TEST(sequence_round_trips_through_backup) {
  auto src = Rig::ready();
  Entry e = sample("site");
  e.sequence = "{USERNAME}{ENTER}{PRESS}{PASSWORD}{ENTER}";
  CHECK((*src)->put(e) == Status::Ok);
  Entry plain = sample("plain");
  CHECK((*src)->put(plain) == Status::Ok);
  std::string backup;
  CHECK((*src)->exportBackup(kBackupPass, backup) == Status::Ok);
  auto dst = Rig::ready();
  size_t a = 0, u = 0;
  CHECK((*dst)->importBackup(kBackupPass, backup, true, &a, &u) == Status::Ok && a == 2);
  Entry got;
  CHECK((*dst)->get(e.id, got) == Status::Ok && got.sequence == e.sequence);
  CHECK((*dst)->get(plain.id, got) == Status::Ok && got.sequence.empty());

  // A restore carrying a chord-like or unknown token is refused as a whole.
  auto snapshot = dst->storage.files;
  for (const char* bad : {"[{\"title\":\"x\",\"sequence\":\"{CTRL}a\"}]", "[{\"title\":\"x\",\"sequence\":5}]",
                          "[{\"title\":\"x\",\"sequence\":\"{DELAY 99999}\"}]"})
    CHECK((*dst)->importBackup(kBackupPass, seal(dst->crypto, bad, 2), false, nullptr, nullptr) == Status::Invalid);
  CHECK(dst->storage.files == snapshot);
}

TEST(burn_after_round_trips_through_backup) {
  auto src = Rig::ready();
  Entry e = sample("one-time");
  e.burnAfter = 1;
  CHECK((*src)->put(e) == Status::Ok);
  std::string backup;
  CHECK((*src)->exportBackup(kBackupPass, backup) == Status::Ok);
  auto dst = Rig::ready();
  CHECK((*dst)->importBackup(kBackupPass, backup, true, nullptr, nullptr) == Status::Ok);
  Entry got;
  CHECK((*dst)->get(e.id, got) == Status::Ok && got.burnAfter == 1);
  for (const char* bad : {"[{\"title\":\"x\",\"burnAfter\":100}]", "[{\"title\":\"x\",\"burnAfter\":-1}]",
                          "[{\"title\":\"x\",\"burnAfter\":\"1\"}]"})
    CHECK((*dst)->importBackup(kBackupPass, seal(dst->crypto, bad, 2), false, nullptr, nullptr) == Status::Invalid);
}

TEST(older_and_newer_backup_versions) {
  auto r = Rig::ready();
  size_t a = 0, u = 0;
  // A v1 file (no history member) still imports; its entries start with no history.
  CHECK((*r)->importBackup(kBackupPass, seal(r->crypto, "[{\"title\":\"old\",\"password\":\"p1\"}]", 1),
                           false, &a, &u) == Status::Ok);
  CHECK(a == 1);
  std::vector<Entry> all;
  CHECK((*r)->list(all) == Status::Ok && all.size() == 1 && all[0].history.empty());
  // v2 with history.
  CHECK((*r)->importBackup(kBackupPass,
                           seal(r->crypto,
                                "[{\"title\":\"new\",\"password\":\"p3\",\"history\":["
                                "{\"password\":\"p2\",\"changedAt\":20},{\"password\":\"p1\"}]}]",
                                2),
                           false, &a, &u) == Status::Ok);
  CHECK((*r)->list(all) == Status::Ok && all.size() == 2);
  for (const Entry& e : all)
    if (e.title == "new")
      CHECK(e.history.size() == 2 && e.history[0].password == "p2" && e.history[0].changedAt == 20 &&
            e.history[1].password == "p1" && e.history[1].changedAt == 0);

  auto snapshot = r->storage.files;
  std::string eleven = "[{\"title\":\"x\",\"history\":[";
  for (int i = 0; i < 11; ++i) eleven += std::string(i ? "," : "") + "{\"password\":\"p\"}";
  eleven += "]}]";
  const std::string bad[] = {
      eleven,                                                            // over the cap
      "[{\"title\":\"x\",\"history\":{}}]",                              // not an array
      "[{\"title\":\"x\",\"history\":[\"p\"]}]",                         // item not an object
      "[{\"title\":\"x\",\"history\":[{\"password\":5}]}]",              // wrong type
      "[{\"title\":\"x\",\"history\":[{\"password\":\"p\",\"changedAt\":\"x\"}]}]",
      "[{\"title\":\"x\",\"history\":[{\"password\":\"" + std::string(kMaxPassword + 1, 'a') + "\"}]}]",
  };
  for (const auto& b : bad)
    CHECK((*r)->importBackup(kBackupPass, seal(r->crypto, b, 2), false, nullptr, nullptr) ==
          Status::Invalid);
  CHECK(r->storage.files == snapshot);
}

TEST_MAIN()
