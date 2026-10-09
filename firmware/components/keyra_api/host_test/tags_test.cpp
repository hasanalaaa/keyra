// NFC tap tags (SPEC §18): secrets and URLs, AES-CMAC (RFC 4493), SUN
// verification against the NXP SDM example (AN12196) and against tags
// simulated with OpenSSL's own CMAC, replay, UID binding, the stored record.
#include <openssl/core_names.h>
#include <openssl/evp.h>
#include <openssl/rand.h>
#include <openssl/sha.h>

#include <cstring>
#include <string>
#include <vector>

#include "keyra_test.hpp"
#include "tags.hpp"

using namespace keyra::api::tags;

namespace {

bool aesBlock(bool enc, const uint8_t key[16], const uint8_t in[16], uint8_t out[16]) {
  EVP_CIPHER_CTX* c = EVP_CIPHER_CTX_new();
  int a = 0, b = 0;
  const bool ok = c && EVP_CipherInit_ex(c, EVP_aes_128_ecb(), nullptr, key, nullptr, enc ? 1 : 0) == 1 &&
                  EVP_CIPHER_CTX_set_padding(c, 0) == 1 && EVP_CipherUpdate(c, out, &a, in, 16) == 1 &&
                  EVP_CipherFinal_ex(c, out + a, &b) == 1 && a + b == 16;
  EVP_CIPHER_CTX_free(c);
  return ok;
}
bool enc(const uint8_t key[16], const uint8_t in[16], uint8_t out[16]) { return aesBlock(true, key, in, out); }
bool dec(const uint8_t key[16], const uint8_t in[16], uint8_t out[16]) { return aesBlock(false, key, in, out); }
bool broken(const uint8_t*, const uint8_t*, uint8_t*) { return false; }
const Cipher kAes{enc, dec};

std::vector<uint8_t> hex(const std::string& s) {
  std::vector<uint8_t> v(s.size() / 2);
  CHECK(parseHex(s, v.data(), v.size()));
  return v;
}

// OpenSSL's CMAC, independent of tags::cmac, for the simulated tag.
std::vector<uint8_t> opensslCmac(const uint8_t key[16], const uint8_t* msg, size_t n) {
  EVP_MAC* mac = EVP_MAC_fetch(nullptr, "CMAC", nullptr);
  EVP_MAC_CTX* ctx = EVP_MAC_CTX_new(mac);
  char cipher[] = "AES-128-CBC";
  OSSL_PARAM params[] = {OSSL_PARAM_construct_utf8_string(OSSL_MAC_PARAM_CIPHER, cipher, 0),
                         OSSL_PARAM_construct_end()};
  std::vector<uint8_t> out(16);
  size_t len = 0;
  const bool ok = EVP_MAC_init(ctx, key, 16, params) == 1 && EVP_MAC_update(ctx, msg, n) == 1 &&
                  EVP_MAC_final(ctx, out.data(), &len, out.size()) == 1 && len == 16;
  CHECK(ok);
  EVP_MAC_CTX_free(ctx);
  EVP_MAC_free(mac);
  return out;
}

// What an NTAG 424 DNA with SDM would mirror into its URL for this read.
Sun simulate(const Key& meta, const Key& file, const Uid& uid, uint32_t ctr) {
  uint8_t plain[16] = {0xC7};
  std::memcpy(plain + 1, uid.data(), uid.size());
  plain[8] = ctr & 0xff, plain[9] = (ctr >> 8) & 0xff, plain[10] = (ctr >> 16) & 0xff;
  RAND_bytes(plain + 11, 5);
  Sun s{};
  CHECK(enc(meta.data(), plain, s.picc));
  uint8_t sv2[16] = {0x3C, 0xC3, 0x00, 0x01, 0x00, 0x80};
  std::memcpy(sv2 + 6, plain + 1, 10);
  const auto session = opensslCmac(file.data(), sv2, 16);
  const auto full = opensslCmac(session.data(), nullptr, 0);
  for (int i = 0; i < 8; ++i) s.mac[i] = full[2 * i + 1];
  return s;
}

Tag secureTag(uint32_t id = 7) {
  Tag t;
  t.id = id;
  t.kind = Kind::Secure;
  t.name = "Desk";
  t.entry = 42;
  RAND_bytes(t.metaKey.data(), 16);
  RAND_bytes(t.fileKey.data(), 16);
  return t;
}

Tag simpleTag(uint32_t id, const std::string& secret) {
  Tag t;
  t.id = id;
  t.name = "Monitor";
  t.entry = 9;
  t.what = What::Password;
  SHA256(reinterpret_cast<const unsigned char*>(secret.data()), secret.size(), t.hash.data());
  return t;
}

void rfc4493() {
  const auto key = hex("2b7e151628aed2a6abf7158809cf4f3c");
  const auto msg = hex(
      "6bc1bee22e409f96e93d7e117393172aae2d8a571e03ac9c9eb76fac45af8e5130c81c46a35ce411e5fbc1191a0a52eff69f2445df4f9b17"
      "ad2b417be66c3710");
  struct {
    size_t len;
    const char* mac;
  } cases[] = {{0, "bb1d6929e95937287fa37d129b756746"},
               {16, "070a16b46b4d4144f79bdd9dd04a287c"},
               {40, "dfa66747de9ae63030ca32611497c827"},
               {64, "51f0bebf7e3b9d92fc49741779363cfe"}};
  for (const auto& c : cases) {
    uint8_t out[16];
    CHECK(cmac(enc, key.data(), msg.data(), c.len, out));
    CHECK(std::vector<uint8_t>(out, out + 16) == hex(c.mac));
  }
  // Every length up to four blocks agrees with OpenSSL's CMAC.
  for (size_t n = 0; n <= 64; ++n) {
    uint8_t out[16];
    CHECK(cmac(enc, key.data(), msg.data(), n, out));
    CHECK(std::vector<uint8_t>(out, out + 16) == opensslCmac(key.data(), msg.data(), n));
  }
  uint8_t out[16];
  CHECK(!cmac(broken, key.data(), msg.data(), 16, out));
}

// The SDM example NXP publishes (AN12196; also the nfcdeveloper.com demo):
// all-zero keys, PICCData EF963FF7828658A599F3041510671E88 → UID 04DE5F1EACC040,
// SDMReadCtr 61; SDMMAC over empty input 94EED9EE65337086.
void nxpExample() {
  Tag t = secureTag();
  t.metaKey.fill(0);
  t.fileKey.fill(0);
  Sun s{};
  CHECK(parseHex("EF963FF7828658A599F3041510671E88", s.picc, 16));
  CHECK(parseHex("94eed9ee65337086", s.mac, 8));
  Uid uid{};
  uint32_t ctr = 0;
  CHECK(verifySun(t, kAes, s, uid, ctr) == Verdict::Ok);
  CHECK(ctr == 61);
  CHECK(std::vector<uint8_t>(uid.begin(), uid.end()) == hex("04DE5F1EACC040"));
  // The session MAC key AN12196 derives for it.
  uint8_t sv2[16] = {0x3C, 0xC3, 0x00, 0x01, 0x00, 0x80, 0x04, 0xDE, 0x5F, 0x1E, 0xAC, 0xC0, 0x40, 0x3D, 0x00, 0x00};
  uint8_t session[16];
  CHECK(cmac(enc, t.fileKey.data(), sv2, 16, session));
  CHECK(std::vector<uint8_t>(session, session + 16) == hex("3FB5F6E3A807A03D5E3570ACE393776F"));
  // One flipped bit anywhere is refused.
  for (int i = 0; i < 8; ++i) {
    Sun bad = s;
    bad.mac[i] ^= 1;
    CHECK(verifySun(t, kAes, bad, uid, ctr) == Verdict::BadMac);
  }
  Sun bad = s;
  bad.picc[5] ^= 0x10;
  CHECK(verifySun(t, kAes, bad, uid, ctr) == Verdict::BadMac);
  // Another tag's keys see noise.
  t.fileKey[0] = 1;
  CHECK(verifySun(t, kAes, s, uid, ctr) == Verdict::BadMac);
  t.fileKey[0] = 0;
  t.metaKey[0] = 1;
  CHECK(verifySun(t, kAes, s, uid, ctr) == Verdict::BadMac);
  const Cipher noDecrypt{enc, broken};
  t.metaKey[0] = 0;
  CHECK(verifySun(t, noDecrypt, s, uid, ctr) == Verdict::Crypto);
}

void replayAndUid() {
  Tag t = secureTag();
  const Uid uid = {0x04, 0x11, 0x22, 0x33, 0x44, 0x55, 0x66};
  Uid got{};
  uint32_t ctr = 0;
  // The first tap binds the UID; counter 0 is fine then.
  const Sun first = simulate(t.metaKey, t.fileKey, uid, 0);
  CHECK(verifySun(t, kAes, first, got, ctr) == Verdict::Ok && ctr == 0 && got == uid);
  accept(t, got, ctr);
  CHECK(t.bound && t.counter == 0);
  CHECK(verifySun(t, kAes, first, got, ctr) == Verdict::Replayed);
  for (uint32_t c : {1u, 2u, 300u, 0xFFFFFEu}) {
    const Sun s = simulate(t.metaKey, t.fileKey, uid, c);
    CHECK(verifySun(t, kAes, s, got, ctr) == Verdict::Ok && ctr == c);
    accept(t, got, ctr);
    CHECK(verifySun(t, kAes, s, got, ctr) == Verdict::Replayed);  // the same URL again
  }
  // An older tap captured earlier is refused once a newer one was seen.
  CHECK(verifySun(t, kAes, simulate(t.metaKey, t.fileKey, uid, 299), got, ctr) == Verdict::Replayed);
  // The same keys on another chip (another UID) are refused.
  const Uid other = {0x04, 0x99, 0x22, 0x33, 0x44, 0x55, 0x66};
  CHECK(verifySun(t, kAes, simulate(t.metaKey, t.fileKey, other, 0xFFFFFF), got, ctr) == Verdict::WrongUid);
  // A simple tag never verifies a SUN.
  Tag simple = simpleTag(3, "x");
  CHECK(verifySun(simple, kAes, first, got, ctr) == Verdict::BadMac);
}

void secretsAndUrls() {
  uint8_t zeros[kSecretBytes] = {};
  CHECK(formatSecret(zeros) == std::string(24, 'a'));
  uint8_t ones[kSecretBytes];
  std::memset(ones, 0xff, sizeof ones);
  CHECK(formatSecret(ones) == std::string(24, '7'));
  uint8_t foobar[kSecretBytes] = {'f', 'o', 'o', 'b', 'a', 'r'};
  CHECK(formatSecret(foobar).substr(0, 10) == "mzxw6ytboi");  // RFC 4648 "foobar"
  const std::string s = formatSecret(foobar);
  CHECK(wellFormedSecret(s) && s.size() == kSecretChars);
  CHECK(!wellFormedSecret(s.substr(1)) && !wellFormedSecret(s + "a") && !wellFormedSecret(""));
  std::string upper = s;
  upper[0] = 'M';
  CHECK(!wellFormedSecret(upper));
  CHECK(simpleUrl(12345, s) == "http://keyra.local/t/12345/" + s);
  CHECK(secureUrl(4294967295u) ==
        "http://keyra.local/t/4294967295?p=00000000000000000000000000000000&m=0000000000000000");
  // NTAG213 holds 144 bytes of NDEF; the longest URLs fit with room to spare.
  CHECK(simpleUrl(4294967295u, s).size() < 80 && secureUrl(4294967295u).size() < 100);

  const Tag t = simpleTag(5, s);
  keyra::api::tags::Digest d{};
  SHA256(reinterpret_cast<const unsigned char*>(s.data()), s.size(), d.data());
  CHECK(secretMatches(t, d));
  d[31] ^= 1;
  CHECK(!secretMatches(t, d));
  Tag secure = secureTag();
  secure.hash = t.hash;
  d[31] ^= 1;
  CHECK(!secretMatches(secure, d));  // a secure tag never takes a static secret

  uint8_t b[2];
  CHECK(parseHex("aF09", b, 2) && b[0] == 0xAF && b[1] == 0x09);
  CHECK(!parseHex("aF0", b, 2) && !parseHex("aF0g", b, 2) && !parseHex("aF09aa", b, 2));
}

void namesAndOwners() {
  CHECK(parseKind("simple") == Kind::Simple && parseKind("secure") == Kind::Secure && !parseKind("ntag"));
  CHECK(std::string(kindName(Kind::Secure)) == "secure");
  for (const char* w : {"username", "password", "both", "totp"}) CHECK(std::string(whatName(*parseWhat(w))) == w);
  CHECK(!parseWhat("sequence") && !parseWhat(""));
  CHECK(validTarget("") && validTarget("usb") && validTarget("AA:bb:01:23:45:67"));
  CHECK(!validTarget("USB") && !validTarget("AA:BB:CC:DD:EE") && !validTarget("AA-BB-CC-DD-EE-FF") &&
        !validTarget("AA:BB:CC:DD:EE:GG"));
  CHECK(owner(9) == "tag:9" && ownerId(owner(9)) == 9 && ownerId(owner(4294967295u)) == 4294967295u);
  CHECK(ownerId("token:9") == 0 && ownerId("tag:") == 0 && ownerId("tag:4294967296") == 0 && ownerId("tag:1x") == 0);
  CHECK(ownerId(std::string(64, 'a')) == 0);
}

void storeRules() {
  Store s;
  CHECK(s.add(simpleTag(1, "a")));
  CHECK(!s.add(simpleTag(1, "b")));  // id taken
  CHECK(!s.add(simpleTag(0, "b")));
  Tag noEntry = simpleTag(2, "b");
  noEntry.entry = 0;
  CHECK(!s.add(noEntry));
  Tag badName = simpleTag(2, "b");
  badName.name = "";
  CHECK(!s.add(badName));
  Tag badTarget = simpleTag(2, "b");
  badTarget.target = "bluetooth";
  CHECK(!s.add(badTarget));
  for (uint32_t id = 2; id <= kMaxTags; ++id) CHECK(s.add(simpleTag(id, "x")));
  CHECK(s.full() && !s.add(simpleTag(99, "x")));
  CHECK(s.remove(5) && !s.remove(5) && !s.contains(5) && !s.full());
  CHECK(s.find(6) && s.find(6)->id == 6 && !s.find(5));
}

void recordRoundTrip() {
  Store s;
  Tag a = simpleTag(10, "first secret");
  a.target = "usb";
  a.created = 1790000000;
  a.lastUsed = 1790000500;
  Tag b = secureTag(11);
  b.name = "\xD9\x85\xD9\x83\xD8\xAA\xD8\xA8";  // مكتب
  b.target = "AA:BB:CC:DD:EE:FF";
  b.what = What::Totp;
  accept(b, {1, 2, 3, 4, 5, 6, 7}, 0x123456);
  CHECK(s.add(a) && s.add(b));
  const auto blob = s.serialize();
  const auto back = Store::parse(blob.data(), blob.size());
  CHECK(back.has_value() && back->all().size() == 2);
  if (back && back->all().size() == 2) {
    const Tag& x = back->all()[0];
    const Tag& y = back->all()[1];
    CHECK(x.id == 10 && x.kind == Kind::Simple && x.hash == a.hash && x.target == "usb" && x.what == What::Password);
    CHECK(x.created == a.created && x.lastUsed == a.lastUsed && x.entry == 9 && x.name == "Monitor");
    CHECK(y.kind == Kind::Secure && y.metaKey == b.metaKey && y.fileKey == b.fileKey && y.bound && y.uid == b.uid);
    CHECK(y.counter == 0x123456 && y.name == b.name && y.target == b.target && y.what == What::Totp);
  }
  CHECK(Store::parse(nullptr, 0).has_value());  // no record yet
  // Truncation anywhere, a trailing byte, a new version or a bad field are refused.
  for (size_t n = 1; n < blob.size(); ++n) CHECK(!Store::parse(blob.data(), n).has_value());
  auto longer = blob;
  longer.push_back(0);
  CHECK(!Store::parse(longer.data(), longer.size()).has_value());
  auto v2 = blob;
  v2[0] = 2;
  CHECK(!Store::parse(v2.data(), v2.size()).has_value());
  auto kind = blob;
  kind[2 + 4] = 9;
  CHECK(!Store::parse(kind.data(), kind.size()).has_value());
  auto what = blob;
  what[2 + 5] = 4;
  CHECK(!Store::parse(what.data(), what.size()).has_value());
}

}  // namespace

int main() {
  rfc4493();
  nxpExample();
  replayAndUid();
  secretsAndUrls();
  namesAndOwners();
  storeRules();
  recordRoundTrip();
  return KEYRA_TEST_RESULT();
}
