// What keeps the vault safe when the NVS partition is erased (SPEC §5
// "Settings storage reset"): the FIDO counter's reservation in fidoctr.bin,
// raised by a restore before any passkey is written, and the unlock delay that
// nvsErased() assumes for the lost failure count.
#include "check.hpp"
#include "rig.hpp"

using namespace keyra::vault;
using namespace keyra::vault::test;

namespace {

const std::string kBackupPass = "backup pass phrase";

uint32_t reserved(Vault& v) {
  uint32_t r = 0;
  CHECK(v.fidoCounterRead(r) == Status::Ok);
  return r;
}

TEST(reservationRecord) {
  auto r = Rig::ready();
  Vault& v = *r->v;
  CHECK(reserved(v) == 0);  // none yet (also every vault from before 0.3.1)
  CHECK(r->storage.files.count(kFidoCounterPath) == 0);
  CHECK(v.fidoCounterRaise(2047) == Status::Ok && reserved(v) == 2047);
  CHECK(v.fidoCounterRaise(10) == Status::Ok && reserved(v) == 2047);  // never lowered
  // Kept across a reboot, a passphrase change and authenticatorReset.
  r->reboot();
  CHECK(r->v->init() == Status::Ok && r->v->unlock(kPass, nullptr) == Status::Ok);
  CHECK(r->v->changePassphrase(kPass, "another passphrase") == Status::Ok);
  CHECK(r->v->passkeyReset() == Status::Ok);
  CHECK(reserved(*r->v) == 2047);
  r->v->lock();
  uint32_t x = 0;
  CHECK(r->v->fidoCounterRead(x) == Status::Locked && r->v->fidoCounterRaise(5000) == Status::Locked);
  // A new setup cannot read the old one (another DEK): it goes.
  CHECK(r->v->factoryReset() == Status::Ok);
  r->storage.files[kFidoCounterPath] = {1, 2, 3};  // a leftover of an interrupted reset
  CHECK(r->v->setup(kPass) == Status::Ok);
  CHECK(r->storage.files.count(kFidoCounterPath) == 0 && reserved(*r->v) == 0);
}

std::string backupWithPasskeys(uint32_t counter) {
  auto src = Rig::ready();
  uint32_t id = 0;
  CHECK(src->v->passkeyPut(id, std::vector<uint8_t>{1, 2, 3}) == Status::Ok);
  std::string json;
  CHECK(src->v->exportBackup(kBackupPass, json, true, counter) == Status::Ok);
  return json;
}

// The restore reserves before writing; a failed reservation leaves everything as it was.
TEST(restoreReservesFirst) {
  const std::string json = backupWithPasskeys(5000);
  for (const bool replace : {true, false}) {
    auto r = Rig::ready();
    Vault& v = *r->v;
    Entry local = sample("Local");
    CHECK(v.put(local) == Status::Ok);
    const auto before = r->storage.files;
    r->storage.failAfter = 0;  // the reservation's write fails
    PasskeyRestore pk;
    CHECK(v.importBackup(kBackupPass, json, replace, nullptr, nullptr, &pk) == Status::StorageError);
    CHECK(pk.present && pk.added == 0);
    CHECK(r->storage.files == before);  // nothing changed on flash
    std::vector<PasskeyRecord> recs;
    CHECK(v.passkeyList(recs) == Status::Ok && recs.empty());

    CHECK(v.importBackup(kBackupPass, json, replace, nullptr, nullptr, &pk) == Status::Ok);
    CHECK(pk.added == 1 && reserved(v) == fidoCounterAfterRestore(5000));
    CHECK(fidoCounterAfterRestore(5000) == 5000 + kFidoRestoreMargin + kFidoCounterBlock);
  }
}

// NVS erased: the failure count is assumed at its longest delay, survives a
// reboot, and a right passphrase still resets it.
TEST(nvsErasedKeepsTheDelay) {
  auto r = Rig::ready();
  r->v->lock();
  r->counter.value = 0;  // what an erased NVS reads
  CHECK(r->v->nvsErased() == Status::Ok);
  CHECK(r->counter.value == kFailuresAfterNvsLoss);
  uint32_t retry = 0;
  CHECK(r->v->unlock(kPass, &retry) == Status::RateLimited && retry == unlockDelayMs(kFailuresAfterNvsLoss));
  r->reboot();  // a reboot does not shorten it
  CHECK(r->v->init() == Status::Ok);
  CHECK(r->v->unlock(kPass, &retry) == Status::RateLimited && retry == 900 * 1000);
  r->clock.now += retry;
  CHECK(r->v->unlock(kPass, nullptr) == Status::Ok);
  CHECK(r->counter.value == 0 && r->v->failedBeforeUnlock() == kFailuresAfterNvsLoss);

  r->counter.value = 20;  // a higher stored count is kept
  CHECK(r->v->nvsErased() == Status::Ok && r->counter.value == 20);
  r->counter.failStore = true;
  r->counter.value = 0;
  CHECK(r->v->nvsErased() == Status::StorageError);
}

}  // namespace

TEST_MAIN()
