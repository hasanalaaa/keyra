// Passkey records and the FIDO wrapping key (vault_passkeys.cpp).
#include <cstring>

#include "check.hpp"
#include "rig.hpp"

using namespace keyra::vault;
using namespace keyra::vault::test;

namespace {
std::vector<uint8_t> blob(const char* s) { return std::vector<uint8_t>(s, s + std::strlen(s)); }
}  // namespace

TEST(passkeys_need_unlock) {
  auto r = Rig::ready();
  (*r)->lock();
  std::vector<PasskeyRecord> out;
  uint32_t id = 0;
  uint8_t k[32];
  CHECK((*r)->passkeyList(out) == Status::Locked);
  CHECK((*r)->passkeyPut(id, blob("x")) == Status::Locked);
  CHECK((*r)->passkeyRemove(1) == Status::Locked);
  CHECK((*r)->passkeyWrapKey(k) == Status::Locked);
  CHECK((*r)->passkeyReset() == Status::Locked);
}

TEST(passkeys_round_trip_encrypted_and_survive_reboot) {
  auto r = Rig::ready();
  uint32_t id = 0;
  CHECK((*r)->passkeyPut(id, blob("github.com/hasan")) == Status::Ok);
  CHECK(id != 0);
  char name[16];
  std::snprintf(name, sizeof name, "f/%08x.bin", id);
  CHECK(r->storage.files.count(name) == 1);
  const auto& file = r->storage.files[name];
  CHECK(std::search(file.begin(), file.end(), std::begin("github"), std::end("github") - 1) == file.end());

  r->reboot();
  CHECK((*r)->init() == Status::Ok);
  CHECK((*r)->unlock(kPass, nullptr) == Status::Ok);
  std::vector<PasskeyRecord> out;
  CHECK((*r)->passkeyList(out) == Status::Ok);
  CHECK(out.size() == 1 && out[0].id == id && out[0].data == blob("github.com/hasan"));

  CHECK((*r)->passkeyPut(id, blob("replaced")) == Status::Ok);
  CHECK((*r)->passkeyList(out) == Status::Ok && out.size() == 1 && out[0].data == blob("replaced"));
  uint32_t missing = id + 1;
  CHECK((*r)->passkeyPut(missing, blob("y")) == Status::NotFound);
  CHECK((*r)->passkeyRemove(id) == Status::Ok);
  CHECK((*r)->passkeyRemove(id) == Status::NotFound);
  CHECK((*r)->passkeyList(out) == Status::Ok && out.empty());
}

TEST(passkey_record_bound_to_its_name) {
  auto r = Rig::ready();
  uint32_t a = 0, b = 0;
  CHECK((*r)->passkeyPut(a, blob("A")) == Status::Ok);
  CHECK((*r)->passkeyPut(b, blob("B")) == Status::Ok);
  char na[16], nb[16];
  std::snprintf(na, sizeof na, "f/%08x.bin", a);
  std::snprintf(nb, sizeof nb, "f/%08x.bin", b);
  r->storage.files[na] = r->storage.files[nb];  // swap a ciphertext under another id
  r->reboot();
  CHECK((*r)->init() == Status::Ok);
  CHECK((*r)->unlock(kPass, nullptr) == Status::Ok);  // entries unaffected
  std::vector<PasskeyRecord> out;
  CHECK((*r)->passkeyList(out) == Status::Corrupt);
}

TEST(passkey_limits) {
  auto r = Rig::ready();
  uint32_t id = 0;
  CHECK((*r)->passkeyPut(id, {}) == Status::Invalid);
  CHECK((*r)->passkeyPut(id, std::vector<uint8_t>(kMaxPasskeyRecord + 1, 1)) == Status::Invalid);
  for (size_t i = 0; i < kMaxPasskeys; ++i) {
    id = 0;
    CHECK((*r)->passkeyPut(id, blob("r")) == Status::Ok);
  }
  id = 0;
  CHECK((*r)->passkeyPut(id, blob("r")) == Status::Full);
}

TEST(wrap_key_stable_until_reset) {
  auto r = Rig::ready();
  uint8_t k1[32], k2[32], k3[32];
  CHECK((*r)->passkeyWrapKey(k1) == Status::Ok);
  CHECK(r->storage.files.count("fido.bin") == 1);
  CHECK((*r)->changePassphrase(kPass, "another long passphrase") == Status::Ok);
  r->reboot();
  CHECK((*r)->init() == Status::Ok);
  CHECK((*r)->unlock("another long passphrase", nullptr) == Status::Ok);
  CHECK((*r)->passkeyWrapKey(k2) == Status::Ok);
  CHECK(std::memcmp(k1, k2, 32) == 0);

  uint32_t id = 0;
  CHECK((*r)->passkeyPut(id, blob("x")) == Status::Ok);
  CHECK((*r)->passkeyReset() == Status::Ok);
  std::vector<PasskeyRecord> out;
  CHECK((*r)->passkeyList(out) == Status::Ok && out.empty());
  CHECK((*r)->passkeyWrapKey(k3) == Status::Ok);
  CHECK(std::memcmp(k1, k3, 32) != 0);
}

TEST(factory_reset_and_setup_drop_passkeys) {
  auto r = Rig::ready();
  uint32_t id = 0;
  uint8_t k[32];
  CHECK((*r)->passkeyPut(id, blob("x")) == Status::Ok);
  CHECK((*r)->passkeyWrapKey(k) == Status::Ok);
  CHECK((*r)->factoryReset() == Status::Ok);
  CHECK((*r)->setup(kPass) == Status::Ok);
  std::vector<PasskeyRecord> out;
  CHECK((*r)->passkeyList(out) == Status::Ok && out.empty());
  CHECK(r->storage.files.count("fido.bin") == 0);
}

TEST_MAIN()
