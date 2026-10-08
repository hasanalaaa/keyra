// Passkeys in backups (docs/research/PASSKEY-BACKUP.md): backup v3, the wrap
// key list in fido.bin v2, merge/replace limits and the staged commit.
#include <algorithm>
#include <cstring>
#include <map>

#include "check.hpp"
#include "core/text.hpp"
#include "rig.hpp"

using namespace keyra::vault;
using namespace keyra::vault::test;

namespace {

const std::string kBackupPass = "backup pass phrase";
using Key = std::array<uint8_t, 32>;

std::vector<uint8_t> blob(const std::string& s) { return std::vector<uint8_t>(s.begin(), s.end()); }

std::vector<Key> keysOf(Vault& v) {
  uint8_t raw[kMaxPasskeyWrapKeys][32];
  size_t n = 0;
  CHECK(v.passkeyWrapKeys(raw, n) == Status::Ok);
  std::vector<Key> out(n);
  for (size_t i = 0; i < n; ++i) std::memcpy(out[i].data(), raw[i], 32);
  return out;
}

std::vector<std::vector<uint8_t>> recordsOf(Vault& v) {
  std::vector<PasskeyRecord> recs;
  CHECK(v.passkeyList(recs) == Status::Ok);
  std::vector<std::vector<uint8_t>> out;
  for (auto& r : recs) out.push_back(r.data);
  std::sort(out.begin(), out.end());
  return out;
}

void addRecords(Vault& v, const std::string& prefix, size_t n) {
  for (size_t i = 0; i < n; ++i) {
    uint32_t id = 0;
    CHECK(v.passkeyPut(id, blob(prefix + std::to_string(i))) == Status::Ok);
  }
}

// A vault with one entry, `n` passkey records and its wrap key, exported with passkeys.
struct Source {
  std::unique_ptr<Rig> rig;
  std::string backup;
  std::vector<Key> keys;
  std::vector<std::vector<uint8_t>> records;
};

Source source(const std::string& prefix, size_t n, uint32_t counter = 77) {
  Source s{Rig::ready(), {}, {}, {}};
  Entry e = sample("src " + prefix);
  CHECK((*s.rig)->put(e) == Status::Ok);
  addRecords(*s.rig->v, prefix, n);
  s.keys = keysOf(*s.rig->v);
  s.records = recordsOf(*s.rig->v);
  CHECK((*s.rig)->exportBackup(kBackupPass, s.backup, true, counter) == Status::Ok);
  return s;
}

// Builds a v3 backup around arbitrary plaintext with the test crypto.
std::string seal(Crypto& c, const std::string& plain, int version = 3) {
  uint8_t salt[16] = {1}, iv[12] = {2}, key[32];
  c.pbkdf2Sha256(kBackupPass, salt, 16, 1000, key, 32);
  std::vector<uint8_t> data(plain.size() + 16);
  c.gcmSeal(key, iv, nullptr, 0, reinterpret_cast<const uint8_t*>(plain.data()), plain.size(), data.data());
  return "{\"format\":\"keyra-backup\",\"v\":" + std::to_string(version) +
         ",\"kdf\":{\"alg\":\"pbkdf2-sha256\",\"iter\":1000,\"salt\":\"" + text::base64Encode(salt, 16) +
         "\"},\"iv\":\"" + text::base64Encode(iv, 12) + "\",\"data\":\"" +
         text::base64Encode(data.data(), data.size()) + "\"}";
}

std::string b64(const std::vector<uint8_t>& v) { return text::base64Encode(v.data(), v.size()); }
std::string keyB64(uint8_t fill) { return b64(std::vector<uint8_t>(32, fill)); }

std::string section(const std::vector<std::string>& keys, const std::vector<std::string>& records,
                    const std::string& counter = "5") {
  std::string s = "{\"entries\":[],\"passkeys\":{\"keys\":[";
  for (size_t i = 0; i < keys.size(); ++i) s += (i ? ",\"" : "\"") + keys[i] + "\"";
  s += "],\"records\":[";
  for (size_t i = 0; i < records.size(); ++i) s += (i ? ",\"" : "\"") + records[i] + "\"";
  return s + "],\"counter\":" + counter + "}}";
}

}  // namespace

TEST(restore_into_new_vault_carries_keys_records_and_counter) {
  const Source src = source("a", 3, 1234);
  auto dst = Rig::ready();
  size_t added = 0, updated = 0;
  PasskeyRestore pk;
  CHECK((*dst)->importBackup(kBackupPass, src.backup, true, &added, &updated, &pk) == Status::Ok);
  CHECK(added == 1 && pk.present && pk.added == 3 && pk.counter == 1234);
  CHECK(keysOf(*dst->v) == src.keys);  // the source's derived key, now stored as a list
  CHECK(recordsOf(*dst->v) == src.records);
  CHECK(dst->storage.files["fido.bin"][0] == 2);

  // Survives a reboot (and the vault holds no plaintext key on flash).
  dst->reboot();
  CHECK((*dst)->init() == Status::Ok && (*dst)->unlock(kPass, nullptr) == Status::Ok);
  CHECK(keysOf(*dst->v) == src.keys && recordsOf(*dst->v) == src.records);
  const auto& file = dst->storage.files["fido.bin"];
  CHECK(std::search(file.begin(), file.end(), src.keys[0].begin(), src.keys[0].end()) == file.end());
  CHECK(heapStats().dirtyFrees == 0);
}

TEST(backup_without_passkeys) {
  auto r = Rig::ready();
  addRecords(*r->v, "x", 2);
  std::string backup;
  CHECK((*r)->exportBackup(kBackupPass, backup, false) == Status::Ok);
  auto dst = Rig::ready();
  addRecords(*dst->v, "local", 1);
  const auto keys = keysOf(*dst->v);
  const auto fido = dst->storage.files["fido.bin"];
  PasskeyRestore pk;
  pk.added = 9;
  CHECK((*dst)->importBackup(kBackupPass, backup, true, nullptr, nullptr, &pk) == Status::Ok);
  CHECK(!pk.present && pk.added == 0);
  // Replace without a passkeys section leaves the local passkeys exactly as they were.
  CHECK(recordsOf(*dst->v).size() == 1 && keysOf(*dst->v) == keys && dst->storage.files["fido.bin"] == fido);
  CHECK(fido.size() == 17 && fido[0] == 1);  // still the v1 salt file
}

TEST(merge_keeps_local_key_first_and_skips_known_records) {
  const Source src = source("a", 2);
  auto dst = Rig::ready();
  addRecords(*dst->v, "b", 2);
  const auto local = keysOf(*dst->v);
  PasskeyRestore pk;
  CHECK((*dst)->importBackup(kBackupPass, src.backup, false, nullptr, nullptr, &pk) == Status::Ok);
  CHECK(pk.present && pk.added == 2);
  auto keys = keysOf(*dst->v);
  CHECK(keys.size() == 2 && keys[0] == local[0] && keys[1] == src.keys[0]);
  CHECK(recordsOf(*dst->v).size() == 4);

  // Again: nothing new, nothing rewritten.
  const auto files = dst->storage.files;
  CHECK((*dst)->importBackup(kBackupPass, src.backup, false, nullptr, nullptr, &pk) == Status::Ok);
  CHECK(pk.added == 0 && keysOf(*dst->v).size() == 2 && recordsOf(*dst->v).size() == 4);
  for (const auto& f : files)
    if (f.first.compare(0, 2, "f/") == 0 || f.first == "fido.bin") CHECK(dst->storage.files[f.first] == f.second);

  // Its own backup onto itself: the v1 salt file stays v1.
  auto self = Rig::ready();
  addRecords(*self->v, "s", 1);
  CHECK(keysOf(*self->v).size() == 1);  // makes the salt
  std::string mine;
  CHECK((*self)->exportBackup(kBackupPass, mine, true, 1) == Status::Ok);
  const auto salt = self->storage.files["fido.bin"];
  CHECK((*self)->importBackup(kBackupPass, mine, false, nullptr, nullptr, &pk) == Status::Ok);
  CHECK(pk.added == 0 && self->storage.files["fido.bin"] == salt);
}

TEST(replace_with_passkeys_drops_local_ones) {
  const Source src = source("a", 2);
  auto dst = Rig::ready();
  addRecords(*dst->v, "b", 3);
  CHECK((*dst)->importBackup(kBackupPass, src.backup, true, nullptr, nullptr) == Status::Ok);
  CHECK(keysOf(*dst->v) == src.keys && recordsOf(*dst->v) == src.records);

  // An empty passkeys section empties them (a fresh key is made on next use).
  CHECK((*dst)->importBackup(kBackupPass, seal(dst->crypto, section({}, {})), true, nullptr, nullptr) == Status::Ok);
  CHECK(dst->storage.files.count("fido.bin") == 0);
  CHECK(recordsOf(*dst->v).empty());
}

TEST(limits_refuse_before_any_change) {
  // Keys: a vault already holding four refuses a fifth on merge.
  auto dst = Rig::ready();
  CHECK(keysOf(*dst->v).size() == 1);
  CHECK((*dst)->importBackup(kBackupPass, seal(dst->crypto, section({keyB64(1), keyB64(2), keyB64(3)}, {})), false,
                             nullptr, nullptr) == Status::Ok);
  CHECK(keysOf(*dst->v).size() == 4);
  const std::string fifth = seal(dst->crypto, section({keyB64(9)}, {}));
  const auto files = dst->storage.files;
  CHECK((*dst)->checkBackup(kBackupPass, fifth, false) == Status::PasskeysFull);
  CHECK((*dst)->checkBackup(kBackupPass, fifth, true) == Status::Ok);  // replace: the backup's own four at most
  size_t a = 9;
  CHECK((*dst)->importBackup(kBackupPass, fifth, false, &a, nullptr) == Status::PasskeysFull && a == 0);
  CHECK(dst->storage.files == files);
  // Keys it already has do not count.
  CHECK((*dst)->importBackup(kBackupPass, seal(dst->crypto, section({keyB64(1)}, {})), false, nullptr, nullptr) ==
        Status::Ok);

  // Records: 30 + 30 different ones pass 50; entries in the same backup stay out too.
  const Source src = source("a", 30);
  auto full = Rig::ready();
  addRecords(*full->v, "b", 30);
  const auto before = full->storage.files;
  CHECK((*full)->checkBackup(kBackupPass, src.backup, false) == Status::PasskeysFull);
  CHECK((*full)->importBackup(kBackupPass, src.backup, false, &a, nullptr) == Status::PasskeysFull);
  CHECK(full->storage.files == before);
  std::vector<Entry> entries;
  CHECK((*full)->list(entries) == Status::Ok && entries.empty());
  CHECK((*full)->importBackup(kBackupPass, src.backup, true, nullptr, nullptr) == Status::Ok);
  CHECK(recordsOf(*full->v).size() == 30);
}

TEST(malformed_passkeys_sections_are_invalid) {
  auto r = Rig::ready();
  const std::string rec = b64(blob("record"));
  const std::string cases[] = {
      section({keyB64(1), keyB64(2), keyB64(3), keyB64(4), keyB64(5)}, {}),  // five keys
      section({keyB64(1), keyB64(1)}, {}),                                    // a duplicate
      section({b64(std::vector<uint8_t>(31, 1))}, {}),                        // wrong length
      section({}, {""}),                                                      // empty record
      section({}, {b64(std::vector<uint8_t>(kMaxPasskeyRecord + 1, 1))}),
      section({}, {}, "-1"),
      section({}, {}, "4294967296"),
      "{\"entries\":[],\"passkeys\":{\"keys\":[],\"records\":[]}}",  // no counter
      "{\"entries\":[],\"passkeys\":[]}",
      "{\"passkeys\":{\"keys\":[],\"records\":[],\"counter\":0}}",     // no entries
      "[]",                                                          // v3 is an object
  };
  for (const auto& c : cases) CHECK((*r)->checkBackup(kBackupPass, seal(r->crypto, c), false) == Status::Invalid);
  std::string many;
  for (size_t i = 0; i <= kMaxPasskeys; ++i) many += (i ? ",\"" : "\"") + b64(blob("r" + std::to_string(i))) + "\"";
  CHECK((*r)->checkBackup(kBackupPass,
                          seal(r->crypto, "{\"entries\":[],\"passkeys\":{\"keys\":[],\"records\":[" + many +
                                              "],\"counter\":0}}"),
                          true) == Status::Invalid);
  // A v1/v2 backup is the bare array; an object there is not.
  CHECK((*r)->checkBackup(kBackupPass, seal(r->crypto, section({}, {rec}), 2), true) == Status::Invalid);
  CHECK((*r)->checkBackup(kBackupPass, seal(r->crypto, "[{\"title\":\"old\"}]", 1), true) == Status::Ok);
  CHECK((*r)->checkBackup(kBackupPass, seal(r->crypto, "[{\"title\":\"old\"}]", 2), true) == Status::Ok);
  // Unknown members next to entries and passkeys are ignored (forward compatible).
  CHECK((*r)->checkBackup(kBackupPass, seal(r->crypto, "{\"entries\":[],\"later\":1}"), true) == Status::Ok);
}

// A passkey-carrying replace cut at every storage operation: after the reboot
// the vault holds the old entries, records and keys, or all of the backup's.
TEST(replace_with_passkeys_is_atomic_across_power_cuts) {
  const Source src = source("new", 3);
  std::map<std::string, std::vector<uint8_t>> files;
  std::vector<Key> oldKeys;
  std::vector<std::vector<uint8_t>> oldRecords;
  {
    auto r = Rig::ready();
    Entry e = sample("old");
    CHECK((*r)->put(e) == Status::Ok);
    addRecords(*r->v, "old", 2);
    oldKeys = keysOf(*r->v);
    oldRecords = recordsOf(*r->v);
    files = r->storage.files;
  }
  auto titles = [](Vault& v) {
    std::vector<Entry> all;
    v.list(all);
    std::vector<std::string> t;
    for (auto& e : all) t.push_back(e.title);
    return t;
  };
  bool sawOld = false, sawNew = false;
  for (int k = 0; k < 200; ++k) {
    Rig t;
    t.storage.files = files;
    CHECK(t->init() == Status::Ok && t->unlock(kPass, nullptr) == Status::Ok);
    t.storage.crashAfter = k;
    const Status s = t->importBackup(kBackupPass, src.backup, true, nullptr, nullptr);
    t.reboot();
    CHECK(t->init() == Status::Ok && t->unlock(kPass, nullptr) == Status::Ok);
    for (const auto& f : t.storage.files)
      CHECK(f.first != "restore.commit" && f.first != "fido.new" && f.first.find(".new") == std::string::npos);
    const auto names = titles(*t.v);
    const auto keys = keysOf(*t.v);
    const auto recs = recordsOf(*t.v);
    const bool isOld = names == std::vector<std::string>{"old"} && keys == oldKeys && recs == oldRecords;
    const bool isNew = names == std::vector<std::string>{"src new"} && keys == src.keys && recs == src.records;
    CHECK(isOld || isNew);
    sawOld |= isOld;
    sawNew |= isNew;
    if (s == Status::Ok) {
      CHECK(isNew && k > 10);
      break;
    }
    CHECK(k < 199);
  }
  CHECK(sawOld && sawNew);
}

TEST(old_marker_and_salt_formats_still_work) {
  // A v1 commit marker (entries only), as an older firmware left it, still finishes.
  auto r = Rig::ready();
  addRecords(*r->v, "keep", 1);
  const auto keys = keysOf(*r->v);
  Entry e = sample("staged");
  CHECK((*r)->put(e) == Status::Ok);
  char bin[16], staged[16];
  std::snprintf(bin, sizeof bin, "e/%08x.bin", e.id);
  std::snprintf(staged, sizeof staged, "e/%08x.new", e.id);
  r->storage.files[staged] = r->storage.files[bin];
  r->storage.files.erase(bin);
  std::vector<uint8_t> marker{1};
  for (int i = 0; i < 4; ++i) marker.push_back(uint8_t(e.id >> (8 * i)));
  r->storage.files["restore.commit"] = marker;
  r->reboot();
  CHECK((*r)->init() == Status::Ok && (*r)->unlock(kPass, nullptr) == Status::Ok);
  Entry got;
  CHECK((*r)->get(e.id, got) == Status::Ok && got.title == "staged");
  CHECK(recordsOf(*r->v).size() == 1 && keysOf(*r->v) == keys);  // v1 never touches passkeys
}

TEST_MAIN()
