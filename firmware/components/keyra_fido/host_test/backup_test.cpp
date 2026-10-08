// Passkeys in backups, end to end (docs/research/PASSKEY-BACKUP.md): credentials
// made on one vault are exported, the vault is reset (a new DEK, as on a new
// Keyra) and the backup restored; GetAssertion must then sign with the original
// keys, checked with OpenSSL's verifier. Real vault core and VaultStore.
#include <cstring>
#include <string>

#include "core/cbor.hpp"
#include "core/ctap.hpp"
#include "fakes.hpp"
#include "host_crypto.hpp"
#include "keyra/vault.hpp"
#include "keyra_test.hpp"
#include "vault_store.hpp"

using namespace keyra::fido;
using namespace keyra::fido::test;
namespace vault = keyra::vault;
using cbor::Value;
using Bytes = std::vector<uint8_t>;

namespace {

const std::string kPass = "first device passphrase", kBackupPass = "backup pass phrase";

struct Rig {
  OpenSslCrypto crypto;
  VaultStore store;
  MemCounter counter;
  ScriptUser user;  // presence always approved; the vault is already unlocked
  MemAttestation attestation;
  Authenticator auth{crypto, store, counter, attestation};

  uint8_t call(uint8_t cmd, const Bytes& params, Value& out) {
    Bytes req{cmd};
    req.insert(req.end(), params.begin(), params.end());
    const Bytes r = auth.cbor(req.data(), req.size(), user, 0, 1);
    out = Value{};
    if (r.size() > 1) CHECK(cbor::decode(r.data() + 1, r.size() - 1, out));
    return r[0];
  }
};

struct Cred {
  std::string rp;
  Bytes id;
  uint8_t pub[65] = {};
};

Bytes cat(Bytes a, const Bytes& b) {
  a.insert(a.end(), b.begin(), b.end());
  return a;
}

uint32_t counterOf(const Bytes& authData) {
  return uint32_t(authData[33]) << 24 | uint32_t(authData[34]) << 16 | uint32_t(authData[35]) << 8 | authData[36];
}

// MakeCredential (ES256, rk as asked) → the credential ID and public key.
bool make(Rig& r, const std::string& rp, uint8_t user, bool rk, Cred& out) {
  cbor::Writer w;
  w.map(rk ? 5 : 4);
  w.uint(1), w.bytes(Bytes(32, 0x11));
  w.uint(2), w.map(1), w.text("id"), w.text(rp);
  w.uint(3), w.map(2), w.text("id"), w.bytes(Bytes{user}), w.text("name"), w.text("hasan");
  w.uint(4), w.array(1), w.map(2), w.text("alg"), w.integer(-7), w.text("type"), w.text("public-key");
  if (rk) w.uint(7), w.map(1), w.text("rk"), w.boolean(true);
  Value v;
  if (r.call(ctap::kMakeCredential, w.out, v) != ctap::kOk) return false;
  const Value* ad = v.find(2);
  if (!ad || ad->str.size() < 55 + 62) return false;
  const Bytes& a = ad->str;
  out.rp = rp;
  out.id.assign(a.begin() + 55, a.begin() + 55 + 62);
  Value cose;
  if (!cbor::decode(a.data() + 55 + 62, a.size() - 55 - 62, cose) || !cose.find(-2) || !cose.find(-3)) return false;
  out.pub[0] = 0x04;
  std::memcpy(out.pub + 1, cose.find(-2)->str.data(), 32);
  std::memcpy(out.pub + 33, cose.find(-3)->str.data(), 32);
  return true;
}

// GetAssertion with the credential in the allow list (or none: discoverable);
// true when it signs with c's key. *count gets the signature counter.
bool assertWith(Rig& r, const Cred& c, bool discoverable, uint32_t* count = nullptr) {
  cbor::Writer w;
  w.map(discoverable ? 2 : 3);
  w.uint(1), w.text(c.rp);
  w.uint(2), w.bytes(Bytes(32, 0x22));
  if (!discoverable) w.uint(3), w.array(1), w.map(2), w.text("id"), w.bytes(c.id), w.text("type"), w.text("public-key");
  Value v;
  if (r.call(ctap::kGetAssertion, w.out, v) != ctap::kOk) return false;
  const Value *cred = v.find(1), *ad = v.find(2), *sig = v.find(3);
  if (!cred || !cred->find("id") || cred->find("id")->str != c.id || !ad || !sig) return false;
  if (count) *count = counterOf(ad->str);
  return verifyEs256(c.pub, cat(ad->str, Bytes(32, 0x22)), sig->str);
}

// A Keyra with one discoverable and one non-discoverable credential, exported.
struct Source {
  Cred resident, plain;
  std::string backup;
  uint32_t counter = 0;
};

Source makeSource() {
  Source s;
  CHECK(vault::factoryReset() == vault::Status::Ok && vault::setup(kPass) == vault::Status::Ok);
  Rig r;
  CHECK(make(r, "resident.example", 1, true, s.resident));
  CHECK(make(r, "plain.example", 2, false, s.plain));
  r.counter.value = 500;  // as if it had signed a lot
  CHECK(assertWith(r, s.resident, true));
  s.counter = r.counter.value;
  CHECK(vault::exportBackup(kBackupPass, s.backup, true, s.counter) == vault::Status::Ok);
  return s;
}

void restoreOnNewKeyra() {
  const Source src = makeSource();
  // A new Keyra: another DEK, so the old wrap key cannot be derived again.
  CHECK(vault::factoryReset() == vault::Status::Ok && vault::setup("second device passphrase") == vault::Status::Ok);
  Rig r;
  CHECK(!assertWith(r, src.plain, false));
  size_t added = 0, updated = 0;
  vault::PasskeyRestore pk;
  CHECK(vault::importBackup(kBackupPass, src.backup, true, &added, &updated, &pk) == vault::Status::Ok);
  CHECK(pk.present && pk.added == 1 && pk.counter == src.counter);
  r.counter.value = restoredCounter(r.counter.value, pk.counter);

  uint32_t count = 0;
  CHECK(assertWith(r, src.plain, false, &count));
  CHECK(count > src.counter);
  CHECK(assertWith(r, src.resident, true, &count));
  CHECK(assertWith(r, src.resident, false));
  CHECK(count > src.counter);
  // New credentials still work, and so do the old ones beside them.
  Cred fresh;
  CHECK(make(r, "new.example", 3, false, fresh));
  CHECK(assertWith(r, fresh, false) && assertWith(r, src.plain, false));
}

void mergeKeepsBothSets() {
  const Source src = makeSource();
  CHECK(vault::factoryReset() == vault::Status::Ok && vault::setup("second device passphrase") == vault::Status::Ok);
  Rig r;
  Cred localRes, localPlain;
  CHECK(make(r, "resident.example", 9, true, localRes));  // same site, another account
  CHECK(make(r, "local.example", 8, false, localPlain));
  uint8_t before[vault::kMaxPasskeyWrapKeys][32];
  size_t n = 0;
  CHECK(vault::passkeyWrapKeys(before, n) == vault::Status::Ok && n == 1);

  size_t added = 0, updated = 0;
  vault::PasskeyRestore pk;
  CHECK(vault::importBackup(kBackupPass, src.backup, false, &added, &updated, &pk) == vault::Status::Ok);
  CHECK(pk.present && pk.added == 1);
  uint8_t after[vault::kMaxPasskeyWrapKeys][32];
  CHECK(vault::passkeyWrapKeys(after, n) == vault::Status::Ok && n == 2);
  CHECK(std::memcmp(before[0], after[0], 32) == 0);  // new credentials keep this Keyra's key

  CHECK(assertWith(r, src.plain, false) && assertWith(r, src.resident, false));
  CHECK(assertWith(r, localPlain, false) && assertWith(r, localRes, false));
  // Discoverable on the shared site: both accounts are offered.
  cbor::Writer w;
  w.map(2), w.uint(1), w.text("resident.example"), w.uint(2), w.bytes(Bytes(32, 0x22));
  Value v;
  CHECK(r.call(ctap::kGetAssertion, w.out, v) == ctap::kOk);
  int64_t total = 0;
  CHECK(v.find(5) && v.find(5)->asInt(total) && total == 2);

  // The same backup again adds nothing.
  CHECK(vault::importBackup(kBackupPass, src.backup, false, &added, &updated, &pk) == vault::Status::Ok);
  CHECK(pk.added == 0);
  CHECK(vault::passkeyWrapKeys(after, n) == vault::Status::Ok && n == 2);
}

void replaceTakesTheBackups() {
  const Source src = makeSource();
  CHECK(vault::factoryReset() == vault::Status::Ok && vault::setup("second device passphrase") == vault::Status::Ok);
  Rig r;
  Cred local;
  CHECK(make(r, "local.example", 8, true, local));
  size_t added = 0;
  vault::PasskeyRestore pk;
  CHECK(vault::importBackup(kBackupPass, src.backup, true, &added, nullptr, &pk) == vault::Status::Ok);
  CHECK(assertWith(r, src.plain, false) && assertWith(r, src.resident, true));
  CHECK(!assertWith(r, local, false));  // replaced: the local key and record are gone

  // A backup without passkeys leaves them alone.
  std::string noPasskeys;
  CHECK(vault::exportBackup(kBackupPass, noPasskeys, false) == vault::Status::Ok);
  CHECK(vault::importBackup(kBackupPass, noPasskeys, true, &added, nullptr, &pk) == vault::Status::Ok);
  CHECK(!pk.present && pk.added == 0);
  CHECK(assertWith(r, src.plain, false) && assertWith(r, src.resident, true));
}

void counterNeverGoesBack() {
  CHECK(restoredCounter(10, 500) == 1500);
  CHECK(restoredCounter(5000, 500) == 5000);
  CHECK(restoredCounter(0, UINT32_MAX - 10) == UINT32_MAX);
}

}  // namespace

int main() {
  CHECK(vault::init() == vault::Status::Ok);
  restoreOnNewKeyra();
  mergeKeepsBothSets();
  replaceTakesTheBackups();
  counterNeverGoesBack();
  return KEYRA_TEST_RESULT();
}
