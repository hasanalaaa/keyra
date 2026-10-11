// The move of a vault from the "vault" partition to the tail of a 16 MB flash
// (docs/SPEC.md §6.1): everything moves byte for byte, and a power cut or an
// I/O error at any step leaves either the old vault, untouched and in charge,
// or the moved one, complete and committed.
#include <memory>

#include "check.hpp"
#include "rig.hpp"
#include "vault_state.hpp"

using namespace keyra::vault;
using namespace keyra::vault::test;

namespace {

constexpr char kFirmware[] = "0.4.0";
using Files = std::map<std::string, std::vector<uint8_t>>;
const std::vector<uint8_t> kTombstone{'K', 'Y', 'R', '1', 0x7f, 'm', 'o', 'v', 'e', 'd'};

// One flash: the partition and the tail share its power.
struct Board {
  PowerRail power;
  MemStorage part, tail;
  MemCounter counter;
  OpenSslCrypto crypto;
  FakeClock clock;
  bool withTail;
  std::unique_ptr<Vault> v;

  explicit Board(const Files& partFiles, const Files& tailFiles = {}, bool hasTail = true) : withTail(hasTail) {
    part.rail = tail.rail = &power;
    part.files = partFiles;
    tail.files = tailFiles;
    reboot();
  }
  Vault& reboot() {
    v.reset();
    part.reboot();
    tail.reboot();
    v = std::make_unique<Vault>(Platform{part, counter, crypto, clock, withTail ? &tail : nullptr},
                                Vault::Options{kTestIterations, kFirmware});
    return *v;
  }
  Vault* operator->() { return v.get(); }
  bool committed() const { return tail.files.count(kHomePath) == 1; }
};

// A vault as the current firmware leaves it in the partition (layout 1).
struct OldVault {
  Files files;
  VaultState state;
};

OldVault makeOld(size_t n) {
  Rig r;
  r.storage.files.erase(kHomePath);  // layout 1 whatever KEYRA_VAULT_LAYOUT says
  CHECK(r->init() == Status::Ok && r->setup(kPass) == Status::Ok);
  fillVault(*r.v, n);
  OldVault old;
  old.state = stateOf(*r.v);
  CHECK(old.state.ok && old.state.entries.size() == n);
  r.storage.files["e/readme.txt"] = {1, 2, 3};  // not ours: moves as it is
  old.files = r.storage.files;
  return old;
}

bool isEntryFile(const std::string& path, uint32_t& id) {
  if (path.size() != 14 || path.compare(0, 2, "e/") != 0 || path.compare(10, 4, ".bin") != 0) return false;
  id = uint32_t(std::stoul(path.substr(2, 8), nullptr, 16));
  return id != 0;
}

// The tail holds exactly the old files (entries inside their buckets, each
// record byte for byte), vault.home and old.erased; the partition only the tombstone.
void checkMoved(const Board& b, const OldVault& old) {
  CHECK(b.committed() && b.tail.files.count(kOldErasedPath) == 1);
  CHECK(b.part.files == (Files{{"meta.bin", kTombstone}}));
  std::vector<uint8_t> home{'K', 'Y', 'R', 'H', 1, uint8_t(sizeof kFirmware - 1)};
  home.insert(home.end(), kFirmware, kFirmware + sizeof kFirmware - 1);
  CHECK(b.tail.files.at(kHomePath) == home);
  size_t expected = 2;  // vault.home, old.erased
  std::map<uint32_t, bucket::Records> buckets;
  for (const auto& [path, bytes] : old.files) {
    uint32_t id;
    if (isEntryFile(path, id)) {
      const uint32_t k = bucket::of(id);
      if (!buckets.count(k)) {
        CHECK(b.tail.files.count(bucket::path(k)) == 1);
        if (!b.tail.files.count(bucket::path(k))) continue;
        CHECK(bucket::parse(b.tail.files.at(bucket::path(k)), k, buckets[k]));
        ++expected;
      }
      bool found = false;
      for (const auto& rec : buckets[k]) found |= rec.id == id && rec.sealed == bytes;
      CHECK(found);
      CHECK(b.tail.files.count(path) == 0);
    } else {
      CHECK(b.tail.files.count(path) == 1 && b.tail.files.at(path) == bytes);
      ++expected;
    }
  }
  size_t records = 0, entryFiles = 0;
  for (const auto& [k, recs] : buckets) records += recs.size();
  for (const auto& [path, bytes] : old.files) {
    uint32_t id;
    entryFiles += isEntryFile(path, id);
  }
  CHECK(records == entryFiles);
  CHECK(b.tail.files.size() == expected);
}

void checkReads(Board& b, const OldVault& old) {
  b.reboot();
  CHECK(b->init() == Status::Ok && b->unlock(kPass, nullptr) == Status::Ok);
  CHECK(sameState(stateOf(*b.v), old.state));
}

}  // namespace

// What the firmware on devices has written (every kind of file, 1000 entries)
// moves and reads back identically, without the passphrase.
TEST(upgrade_moves_a_full_vault_and_reads_it_identically) {
  const OldVault old = makeOld(kMaxEntriesFiles);
  Board b(old.files);
  CHECK(b->init() == Status::Ok && b->initialized() && !b->unlocked());
  CHECK(b->migrationPending() && b->maxEntries() == kMaxEntriesFiles && b->movedBy().empty());
  CHECK(b->migrate() == Status::Ok);
  CHECK(!b->migrationPending() && b->maxEntries() == kMaxEntriesBuckets && b->movedBy() == kFirmware);
  checkMoved(b, old);
  CHECK(b->unlock(kPass, nullptr) == Status::Ok);  // the running instance reads the tail too
  CHECK(sameState(stateOf(*b.v), old.state));
  checkReads(b, old);
  CHECK(b->movedBy() == kFirmware && !b->migrationPending());
  // It keeps working there, and the partition is never written again.
  Entry e = sample("after the move");
  CHECK(b->put(e) == Status::Ok);
  CHECK(b.part.files == (Files{{"meta.bin", kTombstone}}));
  b.reboot();
  CHECK(b->init() == Status::Ok && b->unlock(kPass, nullptr) == Status::Ok);
  Entry got;
  CHECK(b->get(e.id, got) == Status::Ok && same(got, e));
}

TEST(a_vault_never_set_up_moves_too) {
  Board b(Files{});
  CHECK(b->init() == Status::Ok && !b->initialized() && b->migrationPending());
  CHECK(b->migrate() == Status::Ok && b.committed());
  CHECK(b->setup(kPass) == Status::Ok);
  Entry e = sample("first");
  CHECK(b->put(e) == Status::Ok);
  CHECK(b.tail.files.count("meta.bin") == 1 && b.part.files == (Files{{"meta.bin", kTombstone}}));
}

TEST(no_tail_means_no_move) {
  const OldVault old = makeOld(3);
  Board b(old.files, {}, false);
  CHECK(b->init() == Status::Ok && !b->migrationPending());
  CHECK(b->migrate() == Status::Invalid);
  CHECK(b.part.files == old.files && b->maxEntries() == kMaxEntriesFiles);
}

// Power cut after every write, rename, remove and format of the move: after
// the reboot the vault is the old one, byte for byte where it was, or the
// moved one, complete; the next boot's move then completes it.
TEST(power_cut_at_every_step_of_the_move) {
  const OldVault old = makeOld(40);
  int innerCuts = 0;
  for (int k = 0;; ++k) {
    Board b(old.files);
    CHECK(b->init() == Status::Ok);
    b.power.crashAfter = k;
    const Status s = b->migrate();
    const Files partAtCut = b.part.files, tailAtCut = b.tail.files;
    b.reboot();
    CHECK(b->init() == Status::Ok);
    if (b.committed()) {
      checkMoved(b, old);
      CHECK(!b->migrationPending());
      // Cut short again while init tombstones and erases the partition.
      if (partAtCut != Files{{"meta.bin", kTombstone}} || tailAtCut.count(kOldErasedPath) == 0) {
        for (int j = 0;; ++j) {
          Board c(partAtCut, tailAtCut);
          c.power.crashAfter = j;
          const Status is = c->init();
          c.reboot();
          CHECK(c->init() == Status::Ok);
          checkMoved(c, old);
          ++innerCuts;
          if (is == Status::Ok) break;
          CHECK(j < 20);
        }
      }
    } else {
      CHECK(b.part.files == old.files);  // untouched and in charge
      CHECK(b->migrationPending());
      CHECK(b->unlock(kPass, nullptr) == Status::Ok && sameState(stateOf(*b.v), old.state));
      b->lock();
      CHECK(b->migrate() == Status::Ok);  // the next boot
      checkMoved(b, old);
    }
    checkReads(b, old);
    if (s == Status::Ok) {
      CHECK(k > 40);  // the sweep covered the copy, the commit and the erase
      std::printf("power_cut_at_every_step_of_the_move: %d cut points, %d more inside init\n", k, innerCuts);
      break;
    }
    CHECK(k < 400);
  }
  CHECK(innerCuts > 3);
}

// The same with one failing operation on either filesystem while the device keeps running.
TEST(io_error_at_every_step_of_the_move) {
  const OldVault old = makeOld(25);
  for (bool onTail : {false, true}) {
    for (int k = 0;; ++k) {
      Board b(old.files);
      CHECK(b->init() == Status::Ok);
      (onTail ? b.tail : b.part).failAfter = k;
      const Status s = b->migrate();
      (onTail ? b.tail : b.part).failAfter = -1;
      if (s == Status::Ok) {
        checkMoved(b, old);
        checkReads(b, old);
        CHECK(k > (onTail ? 30 : 2));
        break;
      }
      if (!b.committed()) {
        CHECK(b.part.files == old.files && b->migrationPending());
      } else {
        CHECK(!b->migrationPending());  // committed: the tail is the vault from here on
      }
      b.reboot();
      CHECK(b->init() == Status::Ok);
      if (b->migrationPending()) CHECK(b->migrate() == Status::Ok);
      checkMoved(b, old);
      checkReads(b, old);
      CHECK(k < 400);
    }
  }
}

// A replace-restore cut short anywhere in the partition is settled by init
// before the move, so the moved vault is the old set or the new one, never a mix.
TEST(a_restore_cut_short_before_the_move_settles_first) {
  Rig src;
  src.storage.files.erase(kHomePath);
  CHECK(src->init() == Status::Ok && src->setup(kPass) == Status::Ok);
  Entry a = sample("a"), b2 = sample("b"), c = sample("c");
  CHECK(src->put(a) == Status::Ok && src->put(b2) == Status::Ok && src->put(c) == Status::Ok);
  std::string backup;
  CHECK(src->exportBackup("backup pass phrase", backup) == Status::Ok);
  const auto newSet = stateOf(*src.v).entries;
  CHECK(src->remove(a.id) == Status::Ok);
  b2.password = "edited";
  b2.updated += 50;
  CHECK(src->put(b2) == Status::Ok);
  Entry d = sample("d");
  CHECK(src->put(d) == Status::Ok);
  const auto oldSet = stateOf(*src.v).entries;
  const Files before = src.storage.files;
  auto sameEntries = [](const std::map<uint32_t, Entry>& x, const std::map<uint32_t, Entry>& y) {
    if (x.size() != y.size()) return false;
    for (const auto& [id, e] : x)
      if (!y.count(id) || !same(e, y.at(id))) return false;
    return true;
  };
  bool sawOld = false, sawNew = false;
  for (int k = 0;; ++k) {
    Rig r;
    r.storage.files = before;
    CHECK(r->init() == Status::Ok && r->unlock(kPass, nullptr) == Status::Ok);
    r.storage.crashAfter = k;
    const Status s = r->importBackup("backup pass phrase", backup, true, nullptr, nullptr);
    r.storage.reboot();
    Board bd(r.storage.files);
    CHECK(bd->init() == Status::Ok && bd->migrate() == Status::Ok);
    for (const auto& [path, bytes] : bd.tail.files)
      CHECK(path != "restore.commit" && path.find(".new") == std::string::npos && path.find(".tmp") == std::string::npos);
    bd.reboot();
    CHECK(bd->init() == Status::Ok && bd->unlock(kPass, nullptr) == Status::Ok);
    const auto got = stateOf(*bd.v).entries;
    const bool isOld = sameEntries(got, oldSet), isNew = sameEntries(got, newSet);
    CHECK(isOld || isNew);
    sawOld |= isOld;
    sawNew |= isNew;
    if (s == Status::Ok) break;
    CHECK(k < 100);
  }
  CHECK(sawOld && sawNew);
}

// Anything in flight in the partition (here a stray bucket directory) and the
// move waits, changing nothing.
TEST(move_refuses_what_it_does_not_understand) {
  OldVault old = makeOld(3);
  old.files["b/00.bin"] = {1, 0, 0, 0, 0};
  Board b(old.files);
  CHECK(b->init() == Status::Ok && b->migrate() == Status::Invalid);
  CHECK(b.part.files == old.files && !b.committed() && b->migrationPending());
}

// Downgrade story: a firmware that does not know the tail finds the tombstone
// and reports Corrupt; it can neither show the stale vault (erased) nor offer
// a fresh setup over it. Its factory reset clears only the partition: the
// moved vault is still there for a firmware that knows the tail.
TEST(an_older_firmware_sees_corrupt_not_a_stale_vault) {
  const OldVault old = makeOld(5);
  Board b(old.files);
  CHECK(b->init() == Status::Ok && b->migrate() == Status::Ok);
  Board older(b.part.files, {}, false);  // no tail: what the firmware before the move does
  CHECK(older->init() == Status::Corrupt && !older->initialized());
  CHECK(older->setup(kPass) == Status::StorageError);
  CHECK(older->factoryReset() == Status::Ok && older.part.files.empty());
  // Back on a firmware with the tail: the vault is the moved one; the partition is left alone.
  older.part.files["meta.bin"] = {'K', 'Y', 'R', '1', 2, 0};  // whatever the older one wrote since
  Board back(older.part.files, b.tail.files);
  CHECK(back->init() == Status::Ok && back->unlock(kPass, nullptr) == Status::Ok);
  CHECK(sameState(stateOf(*back.v), old.state));
  CHECK(back.part.files == older.part.files);
}

// The tail lost after the commit: Corrupt, never a silent fall-back to the
// partition; a factory reset starts a fresh vault on the tail.
TEST(lost_tail_is_corrupt_until_a_factory_reset) {
  const OldVault old = makeOld(5);
  for (bool unmountable : {false, true}) {
    Board b(old.files);
    CHECK(b->init() == Status::Ok && b->migrate() == Status::Ok);
    if (unmountable) {
      b.tail.mountOk = false;
    } else {
      b.tail.files.clear();
    }
    b.reboot();
    CHECK(b->init() == Status::Corrupt && !b->initialized() && !b->migrationPending());
    b.tail.mountOk = true;
    CHECK(b->factoryReset() == Status::Ok);
    CHECK(b.committed() && b.tail.files.count(kOldErasedPath) == 1 && b.tail.files.size() == 2);
    CHECK(b.part.files == (Files{{"meta.bin", kTombstone}}));
    b.reboot();
    CHECK(b->init() == Status::Ok && !b->initialized() && b->maxEntries() == kMaxEntriesBuckets);
    CHECK(b->setup(kPass) == Status::Ok && b.tail.files.count("meta.bin") == 1);
  }
}

// Factory reset of a moved vault cut short anywhere: the old vault, or Corrupt
// until the reset is repeated, or the reset done; never the partition as a vault.
TEST(power_cut_during_factory_reset_after_the_move) {
  const OldVault old = makeOld(5);
  Board moved(old.files);
  CHECK(moved->init() == Status::Ok && moved->migrate() == Status::Ok);
  for (int k = 0;; ++k) {
    Board b(moved.part.files, moved.tail.files);
    CHECK(b->init() == Status::Ok);
    b.power.crashAfter = k;
    const Status s = b->factoryReset();
    b.reboot();
    const Status is = b->init();
    CHECK(is == Status::Ok || is == Status::Corrupt);
    if (is == Status::Corrupt) CHECK(b->factoryReset() == Status::Ok);
    b.reboot();
    CHECK(b->init() == Status::Ok && !b->migrationPending());
    CHECK(b.part.files == (Files{{"meta.bin", kTombstone}}));
    if (b->initialized()) {
      CHECK(b->unlock(kPass, nullptr) == Status::Ok && sameState(stateOf(*b.v), old.state));
    } else {
      CHECK(b.tail.files.size() == 2);  // vault.home and old.erased
    }
    if (s == Status::Ok) {
      CHECK(!b->initialized() && k > 5);
      break;
    }
    CHECK(k < 50);
  }
}

TEST_MAIN()
