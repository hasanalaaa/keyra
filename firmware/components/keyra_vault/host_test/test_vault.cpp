// Lifecycle, unlock throttling, CRUD, limits, wiping, crash safety, tamper detection.
#include <cstdio>

#include "check.hpp"
#include "rig.hpp"

using namespace keyra::vault;
using namespace keyra::vault::test;

TEST(before_init_and_setup) {
  Rig r;
  uint32_t retry = 123;
  CHECK(r->setup(kPass) == Status::StorageError);  // init() not called yet
  CHECK(r->init() == Status::Ok);
  CHECK(!r->initialized() && !r->unlocked());
  CHECK(r->unlock(kPass, &retry) == Status::NotInitialized);
  CHECK(retry == 0);
  std::vector<Entry> out;
  CHECK(r->list(out) == Status::NotInitialized);
  CHECK(r->setup("") == Status::Invalid);
  CHECK(r->setup(kPass) == Status::Ok);
  CHECK(r->initialized() && r->unlocked());
  CHECK(r->setup(kPass) == Status::AlreadyInitialized);
  CHECK(r.storage.files.count("meta.bin") == 1);
  CHECK(r.storage.files["meta.bin"].size() == 88);  // version 2: one passphrase wrap
}

TEST(mount_failure_is_storage_error) {
  Rig r;
  r.storage.mountOk = false;
  CHECK(r->init() == Status::StorageError);
  CHECK(r->setup(kPass) == Status::StorageError);
}

TEST(unlock_and_wrong_passphrase) {
  auto r = Rig::ready();
  Entry e = sample("mail");
  CHECK((*r)->put(e) == Status::Ok);
  (*r)->lock();
  CHECK(!(*r)->unlocked());
  uint32_t retry = 99;
  CHECK((*r)->unlock("wrong", &retry) == Status::WrongPassphrase);
  CHECK(retry == 0);
  CHECK(r->counter.value == 1);
  CHECK((*r)->unlock(kPass, &retry) == Status::Ok);
  CHECK(r->counter.value == 0);
  Entry got;
  CHECK((*r)->get(e.id, got) == Status::Ok && same(got, e));
  // A second client proving the passphrase while unlocked keeps the state.
  CHECK((*r)->unlock(kPass, nullptr) == Status::Ok);
  CHECK((*r)->unlock("nope", nullptr) == Status::WrongPassphrase);
  CHECK((*r)->unlocked());

  r->reboot();
  CHECK((*r)->init() == Status::Ok);
  CHECK((*r)->initialized() && !(*r)->unlocked());
  CHECK((*r)->unlock(kPass, nullptr) == Status::Ok);
  CHECK((*r)->get(e.id, got) == Status::Ok && same(got, e));
}

TEST(rate_limit_schedule) {
  CHECK(unlockDelayMs(0) == 0 && unlockDelayMs(4) == 0);
  CHECK(unlockDelayMs(5) == 2000 && unlockDelayMs(6) == 4000 && unlockDelayMs(13) == 512000);
  CHECK(unlockDelayMs(14) == 900000 && unlockDelayMs(100) == 900000);
  CHECK(unlockDelayMs(UINT32_MAX) == 900000);

  auto r = Rig::ready();
  (*r)->lock();
  // Counter must already be persisted when the KDF starts.
  uint32_t expected = 0;
  r->crypto.onPbkdf2 = [&] { CHECK(r->counter.value == expected); };
  uint32_t retry = 0;
  for (uint32_t n = 1; n <= 16; ++n) {
    expected = n;
    CHECK((*r)->unlock("bad", &retry) == Status::WrongPassphrase);
    CHECK(retry == unlockDelayMs(n));
    if (retry) {
      int calls = r->crypto.pbkdf2Calls;
      uint32_t again = 0;
      r->clock.now += retry - 1;
      CHECK((*r)->unlock(kPass, &again) == Status::RateLimited);  // even the right one
      CHECK(again == 1);
      CHECK(r->crypto.pbkdf2Calls == calls);  // no KDF while throttled
      CHECK(r->counter.value == n);           // and nothing counted
      r->clock.now += 1;
    }
  }
  r->crypto.onPbkdf2 = nullptr;
  CHECK((*r)->unlock(kPass, &retry) == Status::Ok);
  CHECK(r->counter.value == 0);
  CHECK((*r)->unlock("bad", &retry) == Status::WrongPassphrase && retry == 0);
}

TEST(rate_limit_survives_reboot_from_boot) {
  auto r = Rig::ready();
  (*r)->lock();
  for (int i = 0; i < 6; ++i, r->clock.now += 3600 * 1000) (*r)->unlock("bad", nullptr);
  CHECK(r->counter.value == 6);
  r->clock.now += 3600 * 1000;  // long after the old window: a reboot restarts it
  r->reboot();
  CHECK((*r)->init() == Status::Ok);
  uint32_t retry = 0;
  CHECK((*r)->unlock(kPass, &retry) == Status::RateLimited);
  CHECK(retry == 4000);
  r->clock.now += 4000;
  CHECK((*r)->unlock(kPass, &retry) == Status::Ok);
  CHECK(r->counter.value == 0);
}

TEST(counter_write_failure_blocks_kdf) {
  auto r = Rig::ready();
  (*r)->lock();
  r->counter.failStore = true;
  int calls = r->crypto.pbkdf2Calls;
  CHECK((*r)->unlock(kPass, nullptr) == Status::StorageError);
  CHECK(r->crypto.pbkdf2Calls == calls);
  CHECK(!(*r)->unlocked());
}

TEST(crud) {
  auto r = Rig::ready();
  Vault& v = *r->v;
  Entry a = sample("alpha");
  a.totp = "JBSWY3DPEHPK3PXP";
  a.favorite = true;
  CHECK(v.put(a) == Status::Ok);
  CHECK(a.id != 0);
  Entry b = sample("beta");
  CHECK(v.put(b) == Status::Ok && b.id != a.id);
  CHECK(r->storage.files.size() == 3);  // meta + 2 entries

  std::vector<Entry> all;
  CHECK(v.list(all) == Status::Ok && all.size() == 2);

  Entry got;
  CHECK(v.get(a.id, got) == Status::Ok && same(got, a));
  CHECK(v.get(12345, got) == Status::NotFound);

  // Update: zero created/lastUsed keep the stored values.
  CHECK(v.touch(a.id, 1800000000) == Status::Ok);
  Entry upd = a;
  upd.password = "new password";
  upd.created = 0;
  upd.lastUsed = 0;
  upd.updated = 1800000100;
  CHECK(v.put(upd) == Status::Ok);
  CHECK(upd.created == a.created && upd.lastUsed == 1800000000);
  CHECK(v.get(a.id, got) == Status::Ok && got.password == "new password" &&
        got.lastUsed == 1800000000 && got.updated == 1800000100);

  Entry ghost = sample("ghost");
  ghost.id = 0xdeadbeef;
  CHECK(v.put(ghost) == Status::NotFound);

  CHECK(v.remove(b.id) == Status::Ok);
  CHECK(v.remove(b.id) == Status::NotFound);
  CHECK(v.touch(b.id, 1) == Status::NotFound);
  CHECK(v.list(all) == Status::Ok && all.size() == 1);
  CHECK(r->storage.files.size() == 2);

  v.lock();
  CHECK(v.list(all) == Status::Locked);
  CHECK(v.get(a.id, got) == Status::Locked);
  CHECK(v.put(ghost) == Status::Locked);
  CHECK(v.remove(a.id) == Status::Locked);
  CHECK(v.touch(a.id, 1) == Status::Locked);
}

TEST(arabic_utf8_round_trip) {
  auto r = Rig::ready();
  Entry e = sample("حسابي في البنك", "حسن", "https://مثال.عراق/دخول");
  e.notes = "ملاحظة 🔐 — line 1\nline 2\t\"quoted\"\\";
  e.password = "كلمة-سر-🙂";
  CHECK((*r)->put(e) == Status::Ok);
  r->reboot();
  CHECK((*r)->init() == Status::Ok);
  CHECK((*r)->unlock(kPass, nullptr) == Status::Ok);
  Entry got;
  CHECK((*r)->get(e.id, got) == Status::Ok && same(got, e));
}

TEST(field_limits) {
  auto r = Rig::ready();
  Vault& v = *r->v;
  struct F {
    std::string Entry::* m;
    size_t max;
  } fields[] = {{&Entry::title, kMaxTitle},       {&Entry::url, kMaxUrl},
                {&Entry::username, kMaxUsername}, {&Entry::password, kMaxPassword},
                {&Entry::totp, kMaxTotp},         {&Entry::notes, kMaxNotes}};
  for (const auto& f : fields) {
    Entry e = sample("x");
    e.*f.m = std::string(f.max, 'a');
    CHECK(v.put(e) == Status::Ok);
    Entry big = sample("y");
    big.*f.m = std::string(f.max + 1, 'a');
    CHECK(v.put(big) == Status::Invalid);
  }
  // Limits are bytes: 64 Arabic letters (2 bytes each) fit a 128-byte title, 65 do not.
  std::string ar;
  for (int i = 0; i < 64; ++i) ar += "ع";
  Entry ok = sample(ar);
  CHECK(v.put(ok) == Status::Ok);
  Entry over = sample(ar + "ع");
  CHECK(v.put(over) == Status::Invalid);
  Entry bad = sample(std::string("\xC3\x28"));  // invalid UTF-8
  CHECK(v.put(bad) == Status::Invalid);
  Entry surrogate = sample(std::string("\xED\xA0\x80"));
  CHECK(v.put(surrogate) == Status::Invalid);
}

TEST(entry_count_limit) {
  auto r = Rig::ready();
  Vault& v = *r->v;
  for (size_t i = 0; i < kMaxEntries; ++i) {
    Entry e;
    e.title = std::to_string(i);
    if (v.put(e) != Status::Ok) {
      CHECK(false);
      break;
    }
  }
  Entry extra;
  extra.title = "one too many";
  CHECK(v.put(extra) == Status::Full);
  std::vector<Entry> all;
  CHECK(v.list(all) == Status::Ok && all.size() == kMaxEntries);
  // Updates still work when full.
  all[0].notes = "still editable";
  CHECK(v.put(all[0]) == Status::Ok);
}

TEST(lock_wipes_secrets) {
  HeapStats before = heapStats();
  {
    auto r = Rig::ready();
    for (int i = 0; i < 20; ++i) {
      Entry e = sample("secret " + std::to_string(i));
      CHECK((*r)->put(e) == Status::Ok);
    }
    CHECK(heapStats().liveBlocks > before.liveBlocks);
    (*r)->lock();
    // Every plaintext block is released, and each one was zero when freed.
    CHECK(heapStats().liveBlocks == before.liveBlocks);
    CHECK(heapStats().dirtyFrees == 0);
    CHECK((*r)->unlock(kPass, nullptr) == Status::Ok);
    Entry e = sample("overwrite me");
    CHECK((*r)->put(e) == Status::Ok);
    e.password = "changed";
    CHECK((*r)->put(e) == Status::Ok);  // old plaintext block freed (wiped)
    CHECK((*r)->remove(e.id) == Status::Ok);
  }
  CHECK(heapStats().liveBlocks == before.liveBlocks);
  CHECK(heapStats().dirtyFrees == 0);

  std::string s = "a password that is longer than SSO";
  s = "short";
  wipe(s);
  CHECK(s.empty());
  const char* raw = s.data();
  bool zero = true;
  for (size_t i = 0; i < s.capacity(); ++i) zero &= raw[i] == 0;
  CHECK(zero);
}

TEST(atomic_write_crash_keeps_old_state) {
  for (int crashAt = 0; crashAt < 2; ++crashAt) {  // 0: torn tmp write, 1: before rename
    auto r = Rig::ready();
    Entry e = sample("bank");
    CHECK((*r)->put(e) == Status::Ok);
    Entry v2 = e;
    v2.password = "v2";
    r->storage.crashAfter = crashAt;
    CHECK((*r)->put(v2) == Status::StorageError);
    bool tmpLeft = false;
    for (auto& f : r->storage.files) tmpLeft |= f.first.size() > 4 && f.first.substr(f.first.size() - 4) == ".tmp";
    CHECK(tmpLeft);

    r->reboot();
    CHECK((*r)->init() == Status::Ok);
    for (auto& f : r->storage.files) CHECK(f.first.find(".tmp") == std::string::npos);
    CHECK((*r)->unlock(kPass, nullptr) == Status::Ok);
    Entry got;
    CHECK((*r)->get(e.id, got) == Status::Ok && same(got, e));
  }
}

TEST(crash_during_setup_and_passphrase_change) {
  {
    Rig r;
    CHECK(r->init() == Status::Ok);
    // Setup does: clear entries dir (nothing), write meta tmp, rename.
    r.storage.crashAfter = 1;
    CHECK(r->setup(kPass) == Status::StorageError);
    r.reboot();
    CHECK(r->init() == Status::Ok);
    CHECK(!r->initialized());
    CHECK(r.storage.files.empty());
    CHECK(r->setup(kPass) == Status::Ok);
  }
  {
    auto r = Rig::ready();
    Entry e = sample("x");
    CHECK((*r)->put(e) == Status::Ok);
    r->storage.crashAfter = 1;  // meta tmp written, rename lost
    CHECK((*r)->changePassphrase(kPass, "a brand new passphrase") == Status::StorageError);
    r->reboot();
    CHECK((*r)->init() == Status::Ok);
    CHECK((*r)->unlock("a brand new passphrase", nullptr) == Status::WrongPassphrase);
    CHECK((*r)->unlock(kPass, nullptr) == Status::Ok);
    Entry got;
    CHECK((*r)->get(e.id, got) == Status::Ok && same(got, e));
  }
}

TEST(tamper_is_corrupt_not_wrong_passphrase) {
  auto mutate = [](std::vector<uint8_t>& f, int which) {
    switch (which) {
      case 0: f[f.size() - 1] ^= 1; break;  // tag
      case 1: f[20] ^= 0x80; break;         // ciphertext
      case 2: f[3] ^= 1; break;             // iv
      case 3: f[0] = 9; break;              // version
      case 4: f.resize(10); break;          // truncated
    }
  };
  for (int which = 0; which < 5; ++which) {
    auto r = Rig::ready();
    Entry e = sample("target");
    CHECK((*r)->put(e) == Status::Ok);
    (*r)->lock();
    for (auto& f : r->storage.files)
      if (f.first != "meta.bin") mutate(f.second, which);
    CHECK((*r)->unlock(kPass, nullptr) == Status::Corrupt);
    CHECK(!(*r)->unlocked());
    CHECK(r->counter.value == 0);  // the passphrase was right; not a failed attempt
  }
  {
    // Swapping two entry files is caught by the id-bound AAD.
    auto r = Rig::ready();
    Entry a = sample("a"), b = sample("b");
    CHECK((*r)->put(a) == Status::Ok && (*r)->put(b) == Status::Ok);
    (*r)->lock();
    char pa[32], pb[32];
    std::snprintf(pa, sizeof pa, "e/%08x.bin", a.id);
    std::snprintf(pb, sizeof pb, "e/%08x.bin", b.id);
    std::swap(r->storage.files[pa], r->storage.files[pb]);
    CHECK((*r)->unlock(kPass, nullptr) == Status::Corrupt);
  }
  {
    auto r = Rig::ready();
    r->storage.files["meta.bin"][0] = 'X';
    r->reboot();
    CHECK((*r)->init() == Status::Corrupt);
    CHECK((*r)->factoryReset() == Status::Ok);  // the way out
    CHECK(!(*r)->initialized());
    CHECK((*r)->setup(kPass) == Status::Ok);
  }
  {
    auto r = Rig::ready();
    r->storage.files["meta.bin"].pop_back();
    r->reboot();
    CHECK((*r)->init() == Status::Corrupt);
  }
}

TEST(change_passphrase) {
  auto r = Rig::ready();
  Entry e = sample("x");
  CHECK((*r)->put(e) == Status::Ok);
  std::vector<uint8_t> entryFile;
  for (auto& f : r->storage.files)
    if (f.first != "meta.bin") entryFile = f.second;

  CHECK((*r)->changePassphrase("wrong", "next passphrase") == Status::WrongPassphrase);
  CHECK(r->counter.value == 1);
  CHECK((*r)->changePassphrase(kPass, "") == Status::Invalid);
  CHECK((*r)->changePassphrase(kPass, "next passphrase") == Status::Ok);
  for (auto& f : r->storage.files)  // only the DEK was re-wrapped
    if (f.first != "meta.bin") CHECK(f.second == entryFile);
  (*r)->lock();
  CHECK((*r)->changePassphrase(kPass, "x") == Status::Locked);
  CHECK((*r)->unlock(kPass, nullptr) == Status::WrongPassphrase);
  CHECK((*r)->unlock("next passphrase", nullptr) == Status::Ok);
  Entry got;
  CHECK((*r)->get(e.id, got) == Status::Ok && same(got, e));
}

TEST(factory_reset) {
  auto r = Rig::ready();
  Entry e = sample("x");
  CHECK((*r)->put(e) == Status::Ok);
  (*r)->lock();
  for (int i = 0; i < 7; ++i, r->clock.now += 3600 * 1000) (*r)->unlock("bad", nullptr);
  CHECK(r->counter.value == 7);
  CHECK((*r)->factoryReset() == Status::Ok);
  CHECK(r->storage.files.empty());
  CHECK(r->counter.value == 0);
  CHECK(!(*r)->initialized() && !(*r)->unlocked());
  CHECK((*r)->setup("another passphrase") == Status::Ok);
  std::vector<Entry> all;
  CHECK((*r)->list(all) == Status::Ok && all.empty());
}

TEST(unknown_files_are_ignored) {
  auto r = Rig::ready();
  r->storage.files["e/readme.txt"] = {1, 2, 3};
  (*r)->lock();
  CHECK((*r)->unlock(kPass, nullptr) == Status::Ok);
  std::vector<Entry> all;
  CHECK((*r)->list(all) == Status::Ok && all.empty());
}

TEST(public_api_smoke) {
  // The free functions over the host instance (host_platform.cpp).
  CHECK(init() == Status::Ok);
  CHECK(!initialized());
  CHECK(setup(kPass) == Status::Ok);
  Entry e = sample("api");
  CHECK(put(e) == Status::Ok);
  std::string backup;
  CHECK(exportBackup("backup passphrase", backup) == Status::Ok);
  lock();
  CHECK(!unlocked());
  uint32_t retry = 0;
  CHECK(unlock(kPass, &retry) == Status::Ok);
  size_t added = 9, updated = 9;
  CHECK(importBackup("backup passphrase", backup, false, &added, &updated) == Status::Ok);
  CHECK(added == 0 && updated == 1);
  CHECK(touch(e.id, 5) == Status::Ok);
  CHECK(changePassphrase(kPass, "new one") == Status::Ok);
  CHECK(remove(e.id) == Status::Ok);
  CHECK(factoryReset() == Status::Ok);
  CHECK(std::string(statusName(Status::WrongPassphrase)) == "wrong_passphrase");
  char code[11];
  CHECK(keyra::totp::code("GEZDGNBVGY3TQOJQGEZDGNBVGY3TQOJQ", 59, code, nullptr, nullptr));
  CHECK(std::string(code) == "287082");
}

// SPEC §9.3: an update that changes the password keeps the old one, newest first.
TEST(password_history) {
  auto r = Rig::ready();
  Vault& v = *r->v;
  Entry e = sample("mail");
  e.history.push_back({"forged", 1});  // clients cannot plant history
  CHECK(v.put(e) == Status::Ok);
  Entry got;
  CHECK(v.get(e.id, got) == Status::Ok && got.history.empty());

  // Same password, or only other fields: nothing moves.
  got.notes = "edited";
  got.updated = 1800000000;
  CHECK(v.put(got) == Status::Ok);
  CHECK(v.get(e.id, got) == Status::Ok && got.history.empty());

  for (int i = 1; i <= 12; ++i) {
    got.password = "pw-" + std::to_string(i);
    got.updated = 1800000000 + i;
    got.history.clear();  // what the caller sends is ignored either way
    CHECK(v.put(got) == Status::Ok);
    CHECK(v.get(e.id, got) == Status::Ok);
  }
  CHECK(got.password == "pw-12");
  CHECK(got.history.size() == kMaxHistory);
  for (size_t i = 0; i < got.history.size(); ++i) {
    // pw-11 replaced at t+12 … pw-2 replaced at t+3; "pw-mail" and pw-1 fell off the end.
    CHECK(got.history[i].password == "pw-" + std::to_string(11 - i));
    CHECK(got.history[i].changedAt == int64_t(1800000012 - i));
  }

  // Clearing the password keeps the last one; an empty password is never kept.
  got.password.clear();
  got.updated = 1900000000;
  CHECK(v.put(got) == Status::Ok && v.get(e.id, got) == Status::Ok);
  CHECK(got.history[0].password == "pw-12" && got.history.size() == kMaxHistory);
  got.password = "fresh";
  CHECK(v.put(got) == Status::Ok && v.get(e.id, got) == Status::Ok);
  CHECK(got.history[0].password == "pw-12");

  // touch() keeps history, and it survives a reboot (it is inside the ciphertext).
  CHECK(v.touch(e.id, 1950000000) == Status::Ok);
  Entry before = got;
  before.lastUsed = 1950000000;
  r->reboot();
  CHECK((*r)->init() == Status::Ok && (*r)->unlock(kPass, nullptr) == Status::Ok);
  CHECK((*r)->get(e.id, got) == Status::Ok && same(got, before));
  for (const auto& f : r->storage.files) {
    const std::string bytes(f.second.begin(), f.second.end());
    CHECK(bytes.find("pw-1") == std::string::npos);  // no plaintext on flash
  }
}

// An entry file written by v1.0/v1.1 firmware (plaintext format 1, no history)
// is read as-is and rewritten in the current format on its next write.
TEST(v1_entry_on_flash_migrates_on_write) {
  auto r = Rig::ready();
  Entry e = sample("legacy");
  CHECK((*r)->put(e) == Status::Ok);

  // Recover the DEK the way unlock does (meta.bin v2 layout in vault_core.hpp) ...
  const auto& meta = r->storage.files.at("meta.bin");
  uint8_t kek[32], dek[32];
  CHECK(r->crypto.pbkdf2Sha256(kPass, meta.data() + 12, 16, kTestIterations, kek, 32));
  const std::string metaAad = "keyra/meta/v1";
  CHECK(r->crypto.gcmOpen(kek, meta.data() + 28, reinterpret_cast<const uint8_t*>(metaAad.data()),
                          metaAad.size(), meta.data() + 40, 48, dek) == Crypto::Open::Ok);
  // ... and overwrite the entry with a format-1 plaintext, as old firmware wrote it.
  std::vector<uint8_t> plain = {1};
  for (int i = 0; i < 4; ++i) plain.push_back(uint8_t(e.id >> (8 * i)));
  plain.push_back(0);
  for (int64_t t : {e.created, e.updated, int64_t(0)})
    for (int i = 0; i < 8; ++i) plain.push_back(uint8_t(uint64_t(t) >> (8 * i)));
  for (const std::string* f : {&e.title, &e.url, &e.username, &e.password, &e.totp, &e.notes}) {
    plain.push_back(uint8_t(f->size()));
    plain.push_back(uint8_t(f->size() >> 8));
    plain.insert(plain.end(), f->begin(), f->end());
  }
  char hex[9];
  std::snprintf(hex, sizeof hex, "%08x", static_cast<unsigned>(e.id));
  const std::string aad = std::string("keyra/e/v1/") + hex;
  std::vector<uint8_t> file(1 + 12 + plain.size() + 16, 0);
  file[0] = 1;
  CHECK(r->crypto.random(file.data() + 1, 12));
  CHECK(r->crypto.gcmSeal(dek, file.data() + 1, reinterpret_cast<const uint8_t*>(aad.data()), aad.size(),
                          plain.data(), plain.size(), file.data() + 13));
  r->storage.files[std::string("e/") + hex + ".bin"] = file;

  r->reboot();
  CHECK((*r)->init() == Status::Ok && (*r)->unlock(kPass, nullptr) == Status::Ok);
  Entry got;
  CHECK((*r)->get(e.id, got) == Status::Ok && same(got, e) && got.history.empty());
  got.password = "new one";
  got.updated = 1800000000;
  CHECK((*r)->put(got) == Status::Ok);
  r->reboot();
  CHECK((*r)->init() == Status::Ok && (*r)->unlock(kPass, nullptr) == Status::Ok);
  CHECK((*r)->get(e.id, got) == Status::Ok && got.password == "new one");
  CHECK(got.history.size() == 1 && got.history[0].password == e.password &&
        got.history[0].changedAt == 1800000000);
  // The file grew by the history block: it is format 2 now.
  CHECK(r->storage.files.at(std::string("e/") + hex + ".bin").size() > file.size());
}

TEST_MAIN()
