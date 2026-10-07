// Activity log storage (vault_activity.cpp) and the failed-unlock count (SPEC §15).
#include <algorithm>
#include <cstring>

#include "check.hpp"
#include "rig.hpp"

using namespace keyra::vault;
using namespace keyra::vault::test;

namespace {
std::vector<uint8_t> blob(const char* s) { return std::vector<uint8_t>(s, s + std::strlen(s)); }
}  // namespace

TEST(activity_needs_unlock) {
  auto r = Rig::ready();
  (*r)->lock();
  std::vector<uint8_t> out;
  CHECK((*r)->activityRead(out) == Status::Locked);
  CHECK((*r)->activityWrite(blob("x")) == Status::Locked);
}

TEST(activity_round_trip_encrypted_and_survives_reboot) {
  auto r = Rig::ready();
  std::vector<uint8_t> out;
  CHECK((*r)->activityRead(out) == Status::Ok && out.empty());  // nothing yet
  CHECK((*r)->activityWrite(blob("unlocked;typed GitHub")) == Status::Ok);
  const auto& file = r->storage.files["activity.bin"];
  CHECK(std::search(file.begin(), file.end(), std::begin("GitHub"), std::end("GitHub") - 1) == file.end());

  r->reboot();
  CHECK((*r)->init() == Status::Ok);
  CHECK((*r)->unlock(kPass, nullptr) == Status::Ok);
  CHECK((*r)->activityRead(out) == Status::Ok && out == blob("unlocked;typed GitHub"));
  CHECK((*r)->activityWrite({}) == Status::Ok);  // empty removes it
  CHECK(r->storage.files.count("activity.bin") == 0);
  CHECK((*r)->activityRead(out) == Status::Ok && out.empty());
}

TEST(activity_limits_and_tamper) {
  auto r = Rig::ready();
  CHECK((*r)->activityWrite(std::vector<uint8_t>(kMaxActivityBytes, 'a')) == Status::Ok);
  CHECK((*r)->activityWrite(std::vector<uint8_t>(kMaxActivityBytes + 1, 'a')) == Status::Invalid);
  auto& file = r->storage.files["activity.bin"];
  file[20] ^= 1;
  std::vector<uint8_t> out;
  CHECK((*r)->activityRead(out) == Status::Corrupt && out.empty());
  // A new setup starts with an empty log.
  auto fresh = Rig::ready();
  CHECK(fresh->storage.files.count("activity.bin") == 0);
}

TEST(failed_unlocks_before_success_are_counted) {
  auto r = Rig::ready();
  (*r)->lock();
  CHECK((*r)->failedBeforeUnlock() == 0);
  CHECK((*r)->unlock("wrong one", nullptr) == Status::WrongPassphrase);
  CHECK((*r)->unlock("wrong two", nullptr) == Status::WrongPassphrase);
  CHECK((*r)->unlock(kPass, nullptr) == Status::Ok);
  CHECK((*r)->failedBeforeUnlock() == 2);
  (*r)->lock();
  CHECK((*r)->unlock(kPass, nullptr) == Status::Ok);
  CHECK((*r)->failedBeforeUnlock() == 0);
  // Survives a reboot between the failures and the success (the counter is persisted).
  (*r)->lock();
  CHECK((*r)->unlock("wrong", nullptr) == Status::WrongPassphrase);
  r->reboot();
  CHECK((*r)->init() == Status::Ok);
  CHECK((*r)->unlock(kPass, nullptr) == Status::Ok);
  CHECK((*r)->failedBeforeUnlock() == 1);
}

TEST_MAIN()
