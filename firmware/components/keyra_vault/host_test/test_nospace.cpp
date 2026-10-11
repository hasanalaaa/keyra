#include <string>

#include "check.hpp"
#include "rig.hpp"

using namespace keyra::vault;
using namespace keyra::vault::test;

namespace {
constexpr char kBackupPass[] = "backup pass phrase";

void oneEntry(Vault& v, const std::string& title) {
  Entry e = sample(title);
  CHECK(v.put(e) == Status::Ok);
}
}

TEST(full_on_entry_write_and_rename_but_not_generic_io_error) {
  for (int after : {0, 1}) {
    auto r = Rig::ready();
    oneEntry(*r->v, "keep");
    r->storage.noSpaceAfter = after;
    Entry e = sample("new");
    CHECK((*r)->put(e) == Status::Full);
    std::vector<Entry> entries;
    CHECK((*r)->list(entries) == Status::Ok && entries.size() == 1 && entries[0].title == "keep");
  }
  auto r = Rig::ready();
  r->storage.failAfter = 0;
  Entry e = sample("new");
  CHECK((*r)->put(e) == Status::StorageError);
}

TEST(full_on_merge_import_and_replace_restore) {
  auto src = Rig::ready();
  oneEntry(*src->v, "incoming");
  std::string backup;
  CHECK((*src)->exportBackup(kBackupPass, backup) == Status::Ok);
  for (int after : {0, 1, 2, 3}) {
    auto dst = Rig::ready();
    oneEntry(*dst->v, "keep");
    dst->storage.noSpaceAfter = after;
    size_t added = 0, updated = 0;
    CHECK((*dst)->importBackup(kBackupPass, backup, true, &added, &updated) == Status::Full);
    std::vector<Entry> entries;
    CHECK((*dst)->list(entries) == Status::Ok && entries.size() == 1 && entries[0].title == "keep");
  }
  auto dst = Rig::ready();
  oneEntry(*dst->v, "keep");
  dst->storage.noSpaceAfter = 0;
  CHECK((*dst)->importBackup(kBackupPass, backup, false, nullptr, nullptr) == Status::Full);
  std::vector<Entry> entries;
  CHECK((*dst)->list(entries) == Status::Ok && entries.size() == 1 && entries[0].title == "keep");
}

TEST(full_on_passkey_and_sealed_record_saves) {
  auto r = Rig::ready();
  r->storage.noSpaceAfter = 0;
  uint32_t id = 0;
  CHECK((*r)->passkeyPut(id, std::vector<uint8_t>{1, 2, 3}) == Status::Full);
  r->storage.noSpaceAfter = 0;
  CHECK((*r)->tokensWrite(std::vector<uint8_t>{4, 5}) == Status::Full);
  r->storage.noSpaceAfter = 0;
  CHECK((*r)->tagsWrite(std::vector<uint8_t>{6, 7}) == Status::Full);
}

TEST_MAIN()
