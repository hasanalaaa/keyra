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
  CHECK(v.find("v") && v.find("v")->i == 1);
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
  CHECK(added == 1 && updated == 2);
  auto all = sorted(*dst->v);
  CHECK(all.size() == 4);  // w, x, y, z
  Entry got;
  CHECK((*dst)->get(x.id, got) == Status::Ok && got.password == "pw-x");
  CHECK((*dst)->get(yLocal.id, got) == Status::Ok && got.password == "pw-y");  // kept local id
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
      env("2", "1000", "AAAAAAAAAAAAAAAAAAAAAA=="),
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
static std::string seal(Crypto& c, const std::string& plain) {
  uint8_t salt[16] = {1}, iv[12] = {2}, key[32];
  c.pbkdf2Sha256(kBackupPass, salt, 16, 1000, key, 32);
  std::vector<uint8_t> data(plain.size() + 16);
  c.gcmSeal(key, iv, nullptr, 0, reinterpret_cast<const uint8_t*>(plain.data()), plain.size(),
            data.data());
  return "{\"format\":\"keyra-backup\",\"v\":1,\"kdf\":{\"alg\":\"pbkdf2-sha256\",\"iter\":1000,"
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

TEST(failed_replace_resyncs_with_flash) {
  auto r = Rig::ready();
  Entry a = sample("a"), b = sample("b");
  CHECK((*r)->put(a) == Status::Ok && (*r)->put(b) == Status::Ok);
  std::string backup;
  CHECK((*r)->exportBackup(kBackupPass, backup) == Status::Ok);
  r->storage.failAfter = 1;  // first old file removed, the second removal fails
  CHECK((*r)->importBackup(kBackupPass, backup, true, nullptr, nullptr) == Status::StorageError);
  std::vector<Entry> all;
  CHECK((*r)->list(all) == Status::Ok && all.size() == 1);  // exactly what is on flash
  // With the medium gone entirely, the vault locks rather than show stale RAM.
  r->storage.crashAfter = 0;
  CHECK((*r)->importBackup(kBackupPass, backup, true, nullptr, nullptr) == Status::StorageError);
  CHECK(!(*r)->unlocked());
}

TEST(duplicates_inside_one_import_merge) {
  auto r = Rig::ready();
  size_t a = 0, u = 0;
  std::string plain =
      "[{\"title\":\"t\",\"username\":\"u\",\"password\":\"1\"},"
      "{\"title\":\"t\",\"username\":\"u\",\"password\":\"2\"}]";
  CHECK((*r)->importBackup(kBackupPass, seal(r->crypto, plain), false, &a, &u) == Status::Ok);
  CHECK(a == 1 && u == 1);
  std::vector<Entry> all;
  CHECK((*r)->list(all) == Status::Ok && all.size() == 1 && all[0].password == "2");
}

TEST_MAIN()
