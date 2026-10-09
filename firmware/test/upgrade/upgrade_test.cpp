// Updating a Keyra from 3ce814a (the firmware on devices before 0.3.0) to this
// tree, against data that 3ce814a itself wrote (firmware/test/fixtures/
// v0.2.0-3ce814a, made by regen.sh):
//   device/          the vault partition and NVS values, opened with this code;
//   backup.json      restored with this code;
//   after-rollback/  what 0.3.0 wrote (write030 below) after 3ce814a booted it
//                    again, as when an update fails its probation and rolls back.
//
//   upgrade_test                      runs the checks
//   upgrade_test write030 <in> <out>  regen.sh: this version's writes, for the rollback step
#include "common.hpp"
#include "core/cred.hpp"
#include "core/pin.hpp"

using namespace upgrade;

namespace {

const fs::path kFixtures = KEYRA_UPGRADE_FIXTURES;

// What 0.3.0 keeps in files 3ce814a has never heard of (opaque to the vault;
// their encodings belong to keyra_api, and are new in 0.3.0).
const Bytes kTokensRecord = {'t', 'o', 'k', 'e', 'n', 's', 0, 1, 2, 0xFF, 0xFE};
const Bytes kTagsRecord = {'t', 'a', 'g', 's', 0, 0x80, 0x7F};
fido::pin::State pinState() {
  fido::pin::State s;
  s.retries = 7;
  for (uint8_t i = 0; i < 16; ++i) s.hash[i] = static_cast<uint8_t>(0xA0 + i);
  return s;
}

// The events 0.3.0 adds: every new kind (n at least 1: logged coalescing).
std::vector<activity::Event> newEvents() {
  std::vector<activity::Event> v;
  for (uint8_t k = 19; k <= 28; ++k) {
    activity::Event e;
    e.kind = static_cast<activity::Kind>(k);
    e.at = 1750000000 + k;
    e.id = 0x2000u + k;
    e.n = k == 21 ? 3 : 1;
    e.title = "agent \xd9\x88\xd9\x83\xd9\x8a\xd9\x84";  // "agent وكيل"
    v.push_back(e);
  }
  return v;
}

std::vector<vault::Entry> listAll() {
  std::vector<vault::Entry> v;
  CHECK(vault::list(v) == vault::Status::Ok);
  return v;
}

// "Reboot": nothing survives but the flash.
void reboot() {
  vault::lock();
  CHECK(vault::init() == vault::Status::Ok);
}

// Every account is the one 3ce814a stored, under the id its file is named after.
void checkEntries(size_t extra) {
  const auto got = listAll();
  CHECK(got.size() == expectedEntries().size() + extra);
  CHECK(entriesMatch(got, expectedEntries()));
  for (const auto& e : got) {
    char name[24];
    std::snprintf(name, sizeof name, "e/%08x.bin", static_cast<unsigned>(e.id));
    CHECK(flash().count(name) == 1);
    vault::Entry one;
    CHECK(vault::get(e.id, one) == vault::Status::Ok && sameEntry(one, e) && one.id == e.id);
  }
}

// Each credential still signs (checked with its public key from creation) and
// the signature counter carries on upward from where 3ce814a left it.
void checkPasskeys(Fido& f, const std::vector<Cred>& creds, uint32_t counter) {
  uint32_t last = counter;
  for (const auto& c : creds) {
    uint32_t n = 0;
    const bool ok = assertCred(f, c, false, &n);
    if (!ok) std::fprintf(stderr, "passkey for %s does not sign\n", c.rp.c_str());
    CHECK(ok);
    CHECK(n > last);
    last = n;
  }
}

void upgradeReadsEverything() {
  const fs::path dir = kFixtures / "device";
  uint32_t counter = 0;
  load(dir, counter);
  CHECK(counter == 1007);
  CHECK(vault::init() == vault::Status::Ok);
  CHECK(vault::initialized() && !vault::unlocked());
  CHECK(vault::unlock(kPass, nullptr) == vault::Status::Ok);
  checkEntries(0);

  const vault::RecoveryInfo ri = vault::recoveryInfo();
  CHECK(ri.enabled && ri.created == 1700000500);
  const Bytes key = unhex(readText(kFixtures / "recovery.txt"));
  vault::RecoveryKey rk{};
  CHECK(key.size() == rk.size());
  std::copy(key.begin(), key.end(), rk.begin());
  CHECK(vault::checkRecovery(rk, nullptr) == vault::Status::Ok);

  std::vector<activity::Event> events;
  CHECK(readEvents(events));
  const auto want = expectedEvents();
  CHECK(events.size() == want.size());
  for (size_t i = 0; i < want.size() && i < events.size(); ++i) CHECK(sameEvent(events[i], want[i]));

  // Nothing of 0.3.0's own yet: no tokens, tags or FIDO PIN.
  Bytes none;
  CHECK(vault::tokensRead(none) == vault::Status::Ok && none.empty());
  CHECK(vault::tagsRead(none) == vault::Status::Ok && none.empty());
  CHECK(!vault::fidoPinSet() && vault::fidoPinRead(none) == vault::Status::NotFound);

  // Passkeys: the list, every credential, and discoverable sign-in with names.
  const auto creds = readCreds(kFixtures / "passkeys.txt");
  CHECK(creds.size() == 3);
  std::vector<fido::Passkey> listed;
  CHECK(fido::list(listed) == fido::Result::Ok && listed.size() == 2);
  for (const auto& p : listed) {
    const bool gh = p.rpId == "github.com";
    CHECK(gh || p.rpId == "login.microsoft.com");
    CHECK(p.userName == userName(gh ? 1 : 3) && p.displayName == displayName(gh ? 1 : 3));
    CHECK(p.created == 1700000000);
  }
  uint8_t keys[vault::kMaxPasskeyWrapKeys][32];
  size_t nKeys = 0;
  CHECK(vault::passkeyWrapKeys(keys, nKeys) == vault::Status::Ok && nKeys == 1);
  Fido f;
  f.counter.value = counter;
  checkPasskeys(f, creds, counter);
  std::string name;
  uint32_t n = 0;
  CHECK(assertCred(f, creds[0], true, &n, &name) && name == userName(1) && n == f.counter.value);
  // A new passkey beside the old ones, with 0.3.0's extensions.
  Cred fresh = newCred("new.example", 9, true);
  CHECK(makeCred(f, fresh, 1 | (2 << 1)));
  CHECK(assertCred(f, fresh, false));
  checkPasskeys(f, creds, f.counter.value);

  // This version writes into the old log and vault; all of it survives a reboot.
  for (const auto& e : newEvents()) activity::append(events, e, true);
  CHECK(vault::activityWrite(activity::encode(events)) == vault::Status::Ok);
  vault::Entry added;
  added.title = "Added on 0.3.0";
  added.password = "new";
  CHECK(vault::put(added) == vault::Status::Ok);
  reboot();
  CHECK(vault::unlock(kPass, nullptr) == vault::Status::Ok);
  checkEntries(1);
  std::vector<activity::Event> again;
  CHECK(readEvents(again) && again.size() == want.size() + newEvents().size());
  for (size_t i = 0; i < want.size() && i < again.size(); ++i) CHECK(sameEvent(again[i], want[i]));

  // A passphrase change keeps everything (the DEK is re-wrapped, not replaced).
  CHECK(vault::changePassphrase(kPass, "a new passphrase") == vault::Status::Ok);
  reboot();
  CHECK(vault::unlock(kPass, nullptr) == vault::Status::WrongPassphrase);
  CHECK(vault::unlock("a new passphrase", nullptr) == vault::Status::Ok);
  checkEntries(1);
  checkPasskeys(f, creds, f.counter.value);
}

void oldBackupRestores() {
  const std::string backup = readText(kFixtures / "backup.json");
  const auto creds = readCreds(kFixtures / "passkeys.txt");
  for (const bool replace : {true, false}) {
    // A new Keyra (another DEK), with one account of its own for the merge.
    CHECK(vault::factoryReset() == vault::Status::Ok);
    CHECK(vault::setup("restore target passphrase") == vault::Status::Ok);
    vault::Entry local;
    local.title = "Local only";
    local.password = "local";
    CHECK(vault::put(local) == vault::Status::Ok);
    CHECK(vault::checkBackup(kBackupPass, backup, replace) == vault::Status::Ok);
    size_t added = 0, updated = 0;
    vault::PasskeyRestore pk;
    CHECK(vault::importBackup(kBackupPass, backup, replace, &added, &updated, &pk) == vault::Status::Ok);
    CHECK(added == expectedEntries().size() && updated == 0);
    CHECK(pk.present && pk.added == 2 && pk.counter == 1007);
    const auto got = listAll();
    CHECK(entriesMatch(got, expectedEntries()));
    CHECK((byTitle(got, "Local only") != nullptr) == !replace);
    Fido f;
    f.counter.value = fido::restoredCounter(0, pk.counter);
    checkPasskeys(f, creds, pk.counter);
  }
  CHECK(vault::importBackup("wrong backup passphrase", backup, true, nullptr, nullptr, nullptr) ==
        vault::Status::WrongPassphrase);
}

// 0.3.0 ran, then 3ce814a ran again (gen_old rollback), now 0.3.0 once more.
void rollbackKeepsWhat030Wrote() {
  const fs::path dir = kFixtures / "after-rollback";
  uint32_t counter = 0;
  load(dir, counter);
  CHECK(vault::init() == vault::Status::Ok);
  CHECK(vault::unlock(kPass, nullptr) == vault::Status::Ok);
  checkEntries(2);
  const auto got = listAll();
  CHECK(byTitle(got, "Added on 0.3.0") && byTitle(got, "Added on 0.3.0")->password == "new");
  CHECK(byTitle(got, "Added after rollback") && byTitle(got, "Added after rollback")->password == "rolled-back");

  Bytes b;
  CHECK(vault::tokensRead(b) == vault::Status::Ok && b == kTokensRecord);
  CHECK(vault::tagsRead(b) == vault::Status::Ok && b == kTagsRecord);
  CHECK(vault::fidoPinSet() && vault::fidoPinRead(b) == vault::Status::Ok);
  fido::pin::State st;
  CHECK(fido::pin::decode(b, st) && st.retries == pinState().retries &&
        std::equal(st.hash, st.hash + 16, pinState().hash));

  // The log: 3ce814a's events, 0.3.0's (unknown to 3ce814a, kept as they
  // were), then the lock 3ce814a logged while rolled back.
  std::vector<activity::Event> events;
  CHECK(readEvents(events));
  const auto old = expectedEvents(), added = newEvents();
  CHECK(events.size() == old.size() + added.size() + 1);
  if (events.size() == old.size() + added.size() + 1) {
    for (size_t i = 0; i < old.size(); ++i) CHECK(sameEvent(events[i], old[i]));
    for (size_t i = 0; i < added.size(); ++i) CHECK(sameEvent(events[old.size() + i], added[i]));
    CHECK(events.back().kind == activity::Kind::Lock && events.back().at == 1760000000);
  }

  // All passkeys, 0.3.0's hmac-secret + credProtect one included. With a PIN
  // set nothing is "verified" without it; named credentials still sign.
  const auto creds = readCreds(dir / "passkeys.txt");
  CHECK(creds.size() == 4);
  Fido f;
  f.counter.value = counter;
  checkPasskeys(f, creds, counter);
}

// regen.sh: what 0.3.0 writes on its first boot after the update, before
// failing its probation (the worst case for the rollback).
int write030(const fs::path& in, const fs::path& out) {
  uint32_t counter = 0;
  load(in, counter);
  CHECK(vault::init() == vault::Status::Ok && vault::unlock(kPass, nullptr) == vault::Status::Ok);
  vault::Entry added;
  added.title = "Added on 0.3.0";
  added.password = "new";
  CHECK(vault::put(added) == vault::Status::Ok);
  std::vector<activity::Event> events;
  CHECK(readEvents(events));
  for (const auto& e : newEvents()) activity::append(events, e, true);
  CHECK(vault::activityWrite(activity::encode(events)) == vault::Status::Ok);

  Fido f;
  f.counter.value = counter;
  auto creds = readCreds(in / "../passkeys.txt");
  Cred c = newCred("new-on-030.example", 7, true);
  CHECK(makeCred(f, c, 1 | (2 << 1)));  // hmac-secret, credProtect 2
  CHECK(assertCred(f, c, false));
  creds.push_back(c);
  std::string lines;
  for (const auto& x : creds) lines += credLine(x);

  CHECK(vault::tokensWrite(kTokensRecord) == vault::Status::Ok);
  CHECK(vault::tagsWrite(kTagsRecord) == vault::Status::Ok);
  CHECK(vault::fidoPinWrite(fido::pin::encode(pinState())) == vault::Status::Ok);
  vault::lock();
  dump(out, f.counter.value);
  writeText(out / "passkeys.txt", lines);
  return KEYRA_TEST_RESULT();
}

}  // namespace

int main(int argc, char** argv) {
  if (argc == 4 && std::string(argv[1]) == "write030") return write030(argv[2], argv[3]);
  upgradeReadsEverything();
  oldBackupRestores();
  rollbackKeepsWhat030Wrote();
  return KEYRA_TEST_RESULT();
}
