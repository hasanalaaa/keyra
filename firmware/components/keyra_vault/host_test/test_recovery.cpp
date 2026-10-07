// Vault meta wrap list (SPEC §12.1) and the recovery key (SPEC §12.2).
#include "check.hpp"
#include "rig.hpp"

using namespace keyra::vault;
using namespace keyra::vault::test;

namespace {
const std::string kNext = "a fresh passphrase";

std::vector<uint8_t>& meta(Rig& r) { return r.storage.files["meta.bin"]; }

// The 85-byte version 1 file the 0.1 firmware wrote, rebuilt from a v2 one.
std::vector<uint8_t> toV1(const std::vector<uint8_t>& v2) {
  std::vector<uint8_t> v1(v2.begin(), v2.begin() + 4);
  v1.push_back(1);
  v1.insert(v1.end(), v2.begin() + 8, v2.begin() + 88);
  return v1;
}
}  // namespace

TEST(v1_meta_unlocks_and_migrates) {
  auto r = Rig::ready();
  Entry e = sample("mail");
  CHECK((*r)->put(e) == Status::Ok);
  meta(*r) = toV1(meta(*r));
  CHECK(meta(*r).size() == 85);
  r->reboot();
  CHECK((*r)->init() == Status::Ok);
  CHECK(meta(*r).size() == 85);  // nothing is rewritten before an unlock proves it
  CHECK((*r)->unlock("wrong", nullptr) == Status::WrongPassphrase);
  CHECK(meta(*r).size() == 85);
  CHECK((*r)->unlock(kPass, nullptr) == Status::Ok);
  CHECK(meta(*r).size() == 88 && meta(*r)[4] == 2 && meta(*r)[5] == 1);
  r->reboot();
  CHECK((*r)->init() == Status::Ok);
  CHECK((*r)->unlock(kPass, nullptr) == Status::Ok);
  Entry got;
  CHECK((*r)->get(e.id, got) == Status::Ok && same(got, e));
}

TEST(recovery_round_trip_and_wrap_list) {
  auto r = Rig::ready();
  Entry e = sample("bank");
  CHECK((*r)->put(e) == Status::Ok);
  CHECK(!(*r)->recoveryInfo().enabled);
  RecoveryKey key{};
  CHECK((*r)->createRecovery(1700000000, key) == Status::Ok);
  CHECK(key != RecoveryKey{});
  CHECK((*r)->recoveryInfo().enabled && (*r)->recoveryInfo().created == 1700000000);
  CHECK(meta(*r).size() == 88 + 2 + 84 && meta(*r)[5] == 2);

  // Both wraps open the same vault after a reboot.
  r->reboot();
  CHECK((*r)->init() == Status::Ok);
  CHECK((*r)->recoveryInfo().enabled);
  CHECK((*r)->unlock(kPass, nullptr) == Status::Ok);
  (*r)->lock();

  // Forgotten passphrase: the recovery key sets a new one and opens the vault.
  RecoveryKey wrong = key;
  wrong[0] ^= 1;
  CHECK((*r)->recover(wrong, kNext, nullptr) == Status::WrongPassphrase);
  CHECK(r->counter.value == 1);  // counted like a wrong passphrase
  CHECK((*r)->recover(key, "", nullptr) == Status::Invalid);
  CHECK((*r)->recover(key, kNext, nullptr) == Status::Ok);
  CHECK((*r)->unlocked() && r->counter.value == 0);
  Entry got;
  CHECK((*r)->get(e.id, got) == Status::Ok && same(got, e));
  (*r)->lock();
  CHECK((*r)->unlock(kPass, nullptr) == Status::WrongPassphrase);  // the old one is gone
  CHECK((*r)->unlock(kNext, nullptr) == Status::Ok);
  // The recovery key keeps working, and changing the passphrase keeps it.
  CHECK((*r)->changePassphrase(kNext, "third passphrase") == Status::Ok);
  (*r)->lock();
  CHECK((*r)->checkRecovery(key, nullptr) == Status::Ok);
  CHECK(!(*r)->unlocked());
  CHECK((*r)->recover(key, kPass, nullptr) == Status::Ok);
}

TEST(recovery_regenerate_and_revoke) {
  auto r = Rig::ready();
  RecoveryKey a{}, b{};
  CHECK((*r)->removeRecovery() == Status::NotFound);
  CHECK((*r)->createRecovery(1, a) == Status::Ok);
  CHECK((*r)->createRecovery(2, b) == Status::Ok);  // regenerate replaces
  CHECK(a != b && meta(*r).size() == 174);
  (*r)->lock();
  CHECK((*r)->checkRecovery(a, nullptr) == Status::WrongPassphrase);
  CHECK((*r)->checkRecovery(b, nullptr) == Status::Ok);
  CHECK((*r)->createRecovery(3, a) == Status::Locked);
  CHECK((*r)->removeRecovery() == Status::Locked);
  CHECK((*r)->unlock(kPass, nullptr) == Status::Ok);
  CHECK((*r)->removeRecovery() == Status::Ok);
  CHECK(!(*r)->recoveryInfo().enabled && meta(*r).size() == 88);
  (*r)->lock();
  CHECK((*r)->recover(b, kNext, nullptr) == Status::WrongPassphrase);  // revoked
  CHECK((*r)->unlock(kPass, nullptr) == Status::Ok);  // removal never touches the passphrase wrap
}

TEST(recovery_attempts_are_throttled) {
  auto r = Rig::ready();
  RecoveryKey key{}, bad{};
  CHECK((*r)->createRecovery(1, key) == Status::Ok);
  (*r)->lock();
  for (int i = 0; i < 4; ++i) CHECK((*r)->checkRecovery(bad, nullptr) == Status::WrongPassphrase);
  uint32_t retry = 0;
  CHECK((*r)->checkRecovery(bad, &retry) == Status::WrongPassphrase && retry == 2000);
  CHECK((*r)->recover(key, kNext, &retry) == Status::RateLimited);
  r->clock.now += retry;
  CHECK((*r)->recover(key, kNext, nullptr) == Status::Ok);
}

TEST(crash_during_recover_keeps_one_passphrase) {
  auto r = Rig::ready();
  RecoveryKey key{};
  CHECK((*r)->createRecovery(1, key) == Status::Ok);
  (*r)->lock();
  r->storage.crashAfter = 1;  // meta tmp written, rename lost
  CHECK((*r)->recover(key, kNext, nullptr) == Status::StorageError);
  CHECK(!(*r)->unlocked());
  r->reboot();
  CHECK((*r)->init() == Status::Ok);
  CHECK((*r)->unlock(kPass, nullptr) == Status::Ok);
  (*r)->lock();
  CHECK((*r)->checkRecovery(key, nullptr) == Status::Ok);
}

TEST(malformed_wrap_lists_are_corrupt) {
  auto base = Rig::ready();
  RecoveryKey key{};
  CHECK((*base)->createRecovery(1, key) == Status::Ok);
  const std::vector<uint8_t> good = meta(*base);
  auto load = [&](std::vector<uint8_t> m) {
    Rig r;
    r.storage.files["meta.bin"] = std::move(m);
    return r->init();
  };
  CHECK(load(good) == Status::Ok);
  auto m = good;
  m[88] = 3;  // the reserved device-bound kind: not understood yet, never dropped
  CHECK(load(m) == Status::Corrupt);
  m = good;
  m[88] = 1;  // duplicate passphrase wrap
  CHECK(load(m) == Status::Corrupt);
  m = good;
  m[5] = 1;  // count says one, trailing bytes remain
  CHECK(load(m) == Status::Corrupt);
  m = good;
  m[5] = 3;  // count says three
  CHECK(load(m) == Status::Corrupt);
  m = good;
  m.pop_back();
  CHECK(load(m) == Status::Corrupt);
  m = good;
  m[4] = 9;
  CHECK(load(m) == Status::Corrupt);
  // Only the recovery wrap: the passphrase wrap is mandatory.
  m.assign(good.begin(), good.begin() + 6);
  m[5] = 1;
  m.insert(m.end(), good.begin() + 88, good.end());
  CHECK(load(m) == Status::Corrupt);
}

TEST(tampered_recovery_wrap_fails_closed) {
  auto r = Rig::ready();
  RecoveryKey key{};
  CHECK((*r)->createRecovery(1, key) == Status::Ok);
  meta(*r).back() ^= 1;  // tag of the recovery wrap
  r->reboot();
  CHECK((*r)->init() == Status::Ok);
  CHECK((*r)->checkRecovery(key, nullptr) == Status::WrongPassphrase);
  CHECK((*r)->unlock(kPass, nullptr) == Status::Ok);
}

TEST(factory_reset_removes_recovery) {
  auto r = Rig::ready();
  RecoveryKey key{};
  CHECK((*r)->createRecovery(1, key) == Status::Ok);
  CHECK((*r)->factoryReset() == Status::Ok);
  CHECK(!(*r)->recoveryInfo().enabled);
  CHECK((*r)->setup(kPass) == Status::Ok);
  (*r)->lock();
  CHECK((*r)->checkRecovery(key, nullptr) == Status::WrongPassphrase);
}

TEST_MAIN()
