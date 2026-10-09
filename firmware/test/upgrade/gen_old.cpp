// Built by regen.sh against the 3ce814a sources (the firmware on devices
// before 0.3.0); never part of the normal test build.
//   gen_old v020 <dir>          a Keyra as 3ce814a leaves it: <dir>/device (see
//                               common.hpp), backup.json, passkeys.txt, recovery.txt
//   gen_old rollback <in> <out> 3ce814a booting the state 0.3.0 wrote (after
//                               an update rolled back): it must read and use
//                               everything, then it writes a little and stops.
#include "common.hpp"

using namespace upgrade;

namespace {

uint32_t putEntry(vault::Entry e) {
  e.id = 0;
  CHECK(vault::put(e) == vault::Status::Ok);
  return e.id;
}

void update(uint32_t id, vault::Entry e) {
  e.id = id;
  CHECK(vault::put(e) == vault::Status::Ok);
}

int v020(const fs::path& out) {
  flash().clear();
  CHECK(vault::init() == vault::Status::Ok);
  CHECK(vault::setup(kPass) == vault::Status::Ok);

  const auto want = expectedEntries();
  // Gmail: three passwords, then typed (username only, so no burn).
  {
    vault::Entry e = want[0];
    e.history.clear();
    e.lastUsed = 0;
    e.password = "first-pass";
    e.updated = 1700000000;
    const uint32_t id = putEntry(e);
    e.password = "second-pass";
    e.updated = 1700100000;
    update(id, e);
    e.password = want[0].password;
    e.updated = want[0].updated;
    update(id, e);
    bool burned = true;
    CHECK(vault::touch(id, 1700300000, false, &burned) == vault::Status::Ok && !burned);
  }
  putEntry(want[1]);
  // Long: 12 passwords, so the history overflows its 10 places.
  {
    vault::Entry e = want[2];
    e.history.clear();
    e.password = "long-old-0";
    e.updated = 1720000000;
    const uint32_t id = putEntry(e);
    for (int i = 1; i <= 10; ++i) {
      e.password = "long-old-" + std::to_string(i);
      e.updated = 1720000000 + i;
      update(id, e);
    }
    e.password = want[2].password;
    e.updated = want[2].updated;
    update(id, e);
  }
  putEntry(want[3]);
  {
    vault::Entry e = want[4];
    e.burnAfter = 3;
    e.lastUsed = 0;
    const uint32_t id = putEntry(e);
    bool burned = true;
    CHECK(vault::touch(id, 1740000100, true, &burned) == vault::Status::Ok && !burned);
  }
  putEntry(want[5]);

  std::vector<vault::Entry> got;
  CHECK(vault::list(got) == vault::Status::Ok && got.size() == want.size() && entriesMatch(got, want));

  vault::RecoveryKey rk{};
  CHECK(vault::createRecovery(1700000500, rk) == vault::Status::Ok);
  writeText(out / "recovery.txt", hex(rk.data(), rk.size()) + "\n");

  // Passkeys: two discoverable, one not; the counter as after years of use.
  Fido f;
  f.counter.value = 1000;
  std::vector<Cred> creds = {newCred("github.com", 1, true), newCred("accounts.google.com", 2, false),
                             newCred("login.microsoft.com", 3, true)};
  std::string lines;
  for (auto& c : creds) {
    CHECK(makeCred(f, c));
    lines += credLine(c);
  }
  for (const auto& c : creds) CHECK(assertCred(f, c, false));
  CHECK(assertCred(f, creds[0], true));
  writeText(out / "passkeys.txt", lines);

  // The activity log, every kind 3ce814a knows.
  std::vector<activity::Event> events;
  for (const auto& e : expectedEvents()) activity::append(events, e);
  CHECK(vault::activityWrite(activity::encode(events)) == vault::Status::Ok);

  std::string backup;
  CHECK(vault::exportBackup(kBackupPass, backup, true, f.counter.value) == vault::Status::Ok);
  CHECK(vault::checkBackup(kBackupPass, backup, true) == vault::Status::Ok);
  writeText(out / "backup.json", backup);

  vault::lock();
  dump(out / "device", f.counter.value);
  return KEYRA_TEST_RESULT();
}

int rollback(const fs::path& in, const fs::path& out) {
  uint32_t counter = 0;
  load(in, counter);
  const auto before = flash();
  CHECK(vault::init() == vault::Status::Ok && vault::initialized());
  CHECK(vault::unlock(kPass, nullptr) == vault::Status::Ok);

  std::vector<vault::Entry> got;
  CHECK(vault::list(got) == vault::Status::Ok && entriesMatch(got, expectedEntries()));
  CHECK(byTitle(got, "Added on 0.3.0") != nullptr);

  // The log 0.3.0 extended (kinds this version does not know) still decodes.
  std::vector<activity::Event> events;
  CHECK(readEvents(events) && events.size() > expectedEvents().size());

  // Every passkey, the ones 0.3.0 made with hmac-secret / credProtect included.
  Fido f;
  f.counter.value = counter;
  const auto creds = readCreds(in / "passkeys.txt");
  for (const auto& c : creds) CHECK(assertCred(f, c, false));
  std::vector<fido::Passkey> listed;
  CHECK(fido::list(listed) == fido::Result::Ok && listed.size() == 3);

  // What this version does with its own data while rolled back.
  vault::Entry e;
  e.title = "Added after rollback";
  e.password = "rolled-back";
  CHECK(vault::put(e) == vault::Status::Ok);
  activity::Event lockEvent;
  lockEvent.kind = activity::Kind::Lock;
  lockEvent.at = 1760000000;
  activity::append(events, lockEvent);
  CHECK(vault::activityWrite(activity::encode(events)) == vault::Status::Ok);
  vault::lock();

  // Files this version does not know are left exactly as 0.3.0 wrote them.
  for (const char* p : {"tokens.bin", "tags.bin", "fidopin.bin"}) {
    CHECK(before.count(p) == 1 && flash().count(p) == 1);
    CHECK(before.count(p) && flash()[p] == before.at(p));
  }

  dump(out, f.counter.value);
  writeText(out / "passkeys.txt", readText(in / "passkeys.txt"));
  return KEYRA_TEST_RESULT();
}

}  // namespace

int main(int argc, char** argv) {
  const std::string mode = argc > 1 ? argv[1] : "";
  if (mode == "v020" && argc == 3) return v020(argv[2]);
  if (mode == "rollback" && argc == 4) return rollback(argv[2], argv[3]);
  std::fprintf(stderr, "usage: gen_old v020 <dir> | gen_old rollback <in> <out>\n");
  return 2;
}
