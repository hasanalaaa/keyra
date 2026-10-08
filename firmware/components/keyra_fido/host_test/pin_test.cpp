// ClientPIN (PIN/UV auth protocols 1 and 2), hmac-secret and credProtect.
// The test plays the platform with its own OpenSSL code (ECDH, KDF, AES-CBC,
// HMAC written here from the CTAP 2.1 text), so the authenticator's side is
// checked against an independent implementation. The primitives also get
// published known answers (RFC 4231, NIST SP 800-38A, RFC 5869).
#define OPENSSL_SUPPRESS_DEPRECATED
#include <openssl/ec.h>
#include <openssl/ecdh.h>
#include <openssl/evp.h>
#include <openssl/hmac.h>
#include <openssl/obj_mac.h>
#include <openssl/sha.h>

#include <cstring>
#include <string>

#include "core/cbor.hpp"
#include "core/cred.hpp"
#include "core/ctap.hpp"
#include "core/pin.hpp"
#include "fakes.hpp"
#include "host_crypto.hpp"
#include "keyra_test.hpp"

using namespace keyra::fido;
using namespace keyra::fido::test;
using cbor::Value;
using Bytes = std::vector<uint8_t>;

namespace {

Bytes hex(const std::string& h) {
  Bytes v;
  for (size_t i = 0; i + 1 < h.size(); i += 2) v.push_back(static_cast<uint8_t>(std::stoi(h.substr(i, 2), nullptr, 16)));
  return v;
}
Bytes str(const std::string& s) { return Bytes(s.begin(), s.end()); }
Bytes cat(Bytes a, const Bytes& b) {
  a.insert(a.end(), b.begin(), b.end());
  return a;
}
Bytes cdh(uint8_t fill) { return Bytes(32, fill); }

// ---- the platform, written against OpenSSL only -----------------------------

Bytes hmac(const Bytes& key, const Bytes& msg) {
  Bytes out(32);
  unsigned len = 0;
  HMAC(EVP_sha256(), key.data(), static_cast<int>(key.size()), msg.data(), msg.size(), out.data(), &len);
  return out;
}
Bytes sha(const Bytes& m) {
  Bytes out(32);
  SHA256(m.data(), m.size(), out.data());
  return out;
}
Bytes cbc(bool enc, const Bytes& key, const Bytes& iv, const Bytes& in) {
  Bytes out(in.size() + 16);
  int a = 0, b = 0;
  EVP_CIPHER_CTX* c = EVP_CIPHER_CTX_new();
  EVP_CipherInit_ex(c, EVP_aes_256_cbc(), nullptr, key.data(), iv.data(), enc ? 1 : 0);
  EVP_CIPHER_CTX_set_padding(c, 0);
  EVP_CipherUpdate(c, out.data(), &a, in.data(), static_cast<int>(in.size()));
  EVP_CipherFinal_ex(c, out.data() + a, &b);
  EVP_CIPHER_CTX_free(c);
  out.resize(a + b);
  return out;
}

struct Platform {
  int proto = 2;
  Bytes x, y;              // the platform's public key
  Bytes hmacKey, aesKey;   // shared secret
  Bytes ivCounter{0x42};   // protocol 2 IVs only need to be unique here

  // getKeyAgreement → a fresh platform key and the shared secret (CTAP 2.1 §6.5.6/§6.5.7).
  bool agree(const Value& resp) {
    const Value* k = resp.find(1);
    if (!k || !k->find(-2) || !k->find(-3)) return false;
    int64_t alg = 0;
    if (!k->find(3) || !k->find(3)->asInt(alg) || alg != -25) return false;
    EC_KEY* mine = EC_KEY_new_by_curve_name(NID_X9_62_prime256v1);
    EC_KEY_generate_key(mine);
    const EC_GROUP* g = EC_KEY_get0_group(mine);
    uint8_t pub[65];
    EC_POINT_point2oct(g, EC_KEY_get0_public_key(mine), POINT_CONVERSION_UNCOMPRESSED, pub, 65, nullptr);
    x.assign(pub + 1, pub + 33);
    y.assign(pub + 33, pub + 65);
    Bytes peer{0x04};
    peer = cat(cat(peer, k->find(-2)->str), k->find(-3)->str);
    EC_POINT* q = EC_POINT_new(g);
    const bool onCurve = EC_POINT_oct2point(g, q, peer.data(), 65, nullptr) == 1;
    Bytes z(32);
    const bool ok = onCurve && ECDH_compute_key(z.data(), 32, q, mine, nullptr) == 32;
    EC_POINT_free(q);
    EC_KEY_free(mine);
    if (!ok) return false;
    if (proto == 1) {
      hmacKey = aesKey = sha(z);
    } else {
      const Bytes prk = hmac(Bytes(32, 0), z);  // HKDF-Extract, salt = 32 zero bytes
      hmacKey = hmac(prk, cat(str("CTAP2 HMAC key"), Bytes{1}));
      aesKey = hmac(prk, cat(str("CTAP2 AES key"), Bytes{1}));
    }
    return true;
  }
  Bytes enc(const Bytes& m) {
    if (proto == 1) return cbc(true, aesKey, Bytes(16, 0), m);
    Bytes iv(16, ivCounter[0]++);
    return cat(iv, cbc(true, aesKey, iv, m));
  }
  Bytes dec(const Bytes& c) {
    if (proto == 1) return cbc(false, aesKey, Bytes(16, 0), c);
    return cbc(false, aesKey, Bytes(c.begin(), c.begin() + 16), Bytes(c.begin() + 16, c.end()));
  }
  Bytes auth(const Bytes& key, const Bytes& m) {
    Bytes h = hmac(key, m);
    if (proto == 1) h.resize(16);
    return h;
  }
  void writeKey(cbor::Writer& w) {
    w.map(5);
    w.uint(1), w.uint(2);
    w.uint(3), w.integer(-25);
    w.integer(-1), w.uint(1);
    w.integer(-2), w.bytes(x);
    w.integer(-3), w.bytes(y);
  }
};

Bytes padPin(const std::string& pin) {
  Bytes p(64, 0);
  std::memcpy(p.data(), pin.data(), pin.size());
  return p;
}

struct Rig {
  OpenSslCrypto crypto;
  MemStore store;
  MemCounter counter;
  ScriptUser user;
  MemAttestation attestation;
  Authenticator auth{crypto, store, counter, attestation};
  Rig() { user.open = &store.open; }

  uint8_t call(uint8_t cmd, const Bytes& params, Value& out) {
    Bytes req{cmd};
    req.insert(req.end(), params.begin(), params.end());
    const Bytes r = auth.cbor(req.data(), req.size(), user, 0, 1);
    out = Value{};
    if (r.size() > 1) CHECK(cbor::decode(r.data() + 1, r.size() - 1, out));
    return r[0];
  }
  Bytes info() {
    const Bytes req{ctap::kGetInfo};
    return auth.cbor(req.data(), req.size(), user, 0, 1);
  }
  // ClientPIN with a fresh key agreement each time, as platforms do.
  bool agree(Platform& p) {
    cbor::Writer w;
    w.map(2), w.uint(1), w.uint(p.proto), w.uint(2), w.uint(ctap::kGetKeyAgreement);
    Value v;
    return call(ctap::kClientPin, w.out, v) == ctap::kOk && p.agree(v);
  }
  uint8_t retries(int& out) {
    cbor::Writer w;
    w.map(2), w.uint(1), w.uint(1), w.uint(2), w.uint(ctap::kGetPinRetries);
    Value v;
    const uint8_t s = call(ctap::kClientPin, w.out, v);
    int64_t n = -1;
    if (s == ctap::kOk) CHECK(v.find(3) && v.find(3)->asInt(n));
    out = static_cast<int>(n);
    return s;
  }
  uint8_t setPin(Platform& p, const std::string& pin) {
    if (!agree(p)) return 0xFF;
    const Bytes newPinEnc = p.enc(padPin(pin));
    cbor::Writer w;
    w.map(5);
    w.uint(1), w.uint(p.proto);
    w.uint(2), w.uint(ctap::kSetPin);
    w.uint(3), p.writeKey(w);
    w.uint(4), w.bytes(p.auth(p.hmacKey, newPinEnc));
    w.uint(5), w.bytes(newPinEnc);
    Value v;
    return call(ctap::kClientPin, w.out, v);
  }
  uint8_t changePin(Platform& p, const std::string& cur, const std::string& next) {
    if (!agree(p)) return 0xFF;
    const Bytes newPinEnc = p.enc(padPin(next));
    Bytes h = sha(str(cur));
    h.resize(16);
    const Bytes hashEnc = p.enc(h);
    cbor::Writer w;
    w.map(6);
    w.uint(1), w.uint(p.proto);
    w.uint(2), w.uint(ctap::kChangePin);
    w.uint(3), p.writeKey(w);
    w.uint(4), w.bytes(p.auth(p.hmacKey, cat(newPinEnc, hashEnc)));
    w.uint(5), w.bytes(newPinEnc);
    w.uint(6), w.bytes(hashEnc);
    Value v;
    return call(ctap::kClientPin, w.out, v);
  }
  uint8_t token(Platform& p, const std::string& pin, Bytes& out) {
    out.clear();
    if (!agree(p)) return 0xFF;
    Bytes h = sha(str(pin));
    h.resize(16);
    cbor::Writer w;
    w.map(4);
    w.uint(1), w.uint(p.proto);
    w.uint(2), w.uint(ctap::kGetPinToken);
    w.uint(3), p.writeKey(w);
    w.uint(6), w.bytes(p.enc(h));
    Value v;
    const uint8_t s = call(ctap::kClientPin, w.out, v);
    if (s == ctap::kOk) {
      CHECK(v.find(2) && v.find(2)->type == Value::Type::Bytes);
      out = p.dec(v.find(2)->str);
      CHECK(out.size() == 32);
    }
    return s;
  }
};

struct Make {
  std::string rp = "example.com";
  Bytes uid = {1, 2, 3};
  bool rk = false, uv = false;
  int protect = 0;
  bool hmacSecret = false;
  Bytes pinAuth;  // empty = none
  int proto = 2;
};

Bytes makeReq(const Bytes& h, const Make& o) {
  const bool ext = o.protect || o.hmacSecret, options = o.rk || o.uv, pin = !o.pinAuth.empty();
  cbor::Writer w;
  w.map(4 + (ext ? 1 : 0) + (options ? 1 : 0) + (pin ? 2 : 0));
  w.uint(1), w.bytes(h);
  w.uint(2), w.map(1), w.text("id"), w.text(o.rp);
  w.uint(3), w.map(3), w.text("id"), w.bytes(o.uid), w.text("name"), w.text("hasan"), w.text("displayName"),
      w.text("Hasan");
  w.uint(4), w.array(1), w.map(2), w.text("alg"), w.integer(-7), w.text("type"), w.text("public-key");
  if (ext) {
    w.uint(6), w.map((o.protect ? 1 : 0) + (o.hmacSecret ? 1 : 0));
    if (o.protect) w.text("credProtect"), w.uint(o.protect);
    if (o.hmacSecret) w.text("hmac-secret"), w.boolean(true);
  }
  if (options) {
    w.uint(7), w.map((o.rk ? 1 : 0) + (o.uv ? 1 : 0));
    if (o.rk) w.text("rk"), w.boolean(true);
    if (o.uv) w.text("uv"), w.boolean(true);
  }
  if (pin) w.uint(8), w.bytes(o.pinAuth), w.uint(9), w.uint(o.proto);
  return w.out;
}

struct Get {
  std::string rp = "example.com";
  std::vector<Bytes> allow;
  bool uv = false;
  Bytes pinAuth;
  int proto = 2;
  Bytes hmacExt;  // encoded hmac-secret input map, empty = none
};

Bytes getReq(const Bytes& h, const Get& o) {
  const bool pin = !o.pinAuth.empty();
  cbor::Writer w;
  w.map(2 + (o.allow.empty() ? 0 : 1) + (o.hmacExt.empty() ? 0 : 1) + (o.uv ? 1 : 0) + (pin ? 2 : 0));
  w.uint(1), w.text(o.rp);
  w.uint(2), w.bytes(h);
  if (!o.allow.empty()) {
    w.uint(3), w.array(o.allow.size());
    for (const auto& id : o.allow) w.map(2), w.text("id"), w.bytes(id), w.text("type"), w.text("public-key");
  }
  if (!o.hmacExt.empty()) {
    w.uint(4), w.map(1), w.text("hmac-secret");
    w.out.insert(w.out.end(), o.hmacExt.begin(), o.hmacExt.end());
  }
  if (o.uv) w.uint(5), w.map(1), w.text("uv"), w.boolean(true);
  if (pin) w.uint(6), w.bytes(o.pinAuth), w.uint(7), w.uint(o.proto);
  return w.out;
}

// The ES256 COSE key Keyra writes is always 77 bytes; extensions follow it.
constexpr size_t kCoseLen = 77;

struct Made {
  Bytes credId, authData;
  uint8_t flags = 0;
  Value ext;
};

uint8_t make(Rig& r, const Bytes& h, const Make& o, Made& m) {
  Value v;
  const uint8_t s = r.call(ctap::kMakeCredential, makeReq(h, o), v);
  if (s != ctap::kOk) return s;
  m.authData = v.find(2)->str;
  m.flags = m.authData[32];
  m.credId.assign(m.authData.begin() + 55, m.authData.begin() + 55 + 62);
  const size_t extAt = 55 + 62 + kCoseLen;
  m.ext = Value{};
  if (m.flags & 0x80) CHECK(cbor::decode(m.authData.data() + extAt, m.authData.size() - extAt, m.ext));
  else CHECK(m.authData.size() == extAt);
  return s;
}

struct Asserted {
  Bytes credId, authData;
  uint8_t flags = 0;
  Value ext, user;
};

uint8_t get(Rig& r, const Bytes& h, const Get& o, Asserted& a) {
  Value v;
  const uint8_t s = r.call(ctap::kGetAssertion, getReq(h, o), v);
  if (s != ctap::kOk) return s;
  a.credId = v.find(1)->find("id")->str;
  a.authData = v.find(2)->str;
  a.flags = a.authData[32];
  a.ext = Value{};
  if (a.flags & 0x80) CHECK(cbor::decode(a.authData.data() + 37, a.authData.size() - 37, a.ext));
  else CHECK(a.authData.size() == 37);
  a.user = v.find(4) ? *v.find(4) : Value{};
  return s;
}

// ---- known answers ------------------------------------------------------------

void knownAnswers() {
  OpenSslCrypto c;
  // RFC 4231 test case 2.
  uint8_t mac[32];
  const Bytes jefe = str("Jefe"), what = str("what do ya want for nothing?");
  CHECK(c.hmacSha256(jefe.data(), jefe.size(), what.data(), what.size(), mac));
  CHECK(Bytes(mac, mac + 32) == hex("5bdcc146bf60754e6a042426089575c75a003f089d2739839dec58b964ec3843"));
  // NIST SP 800-38A F.2.5 / F.2.6, CBC-AES256, first block.
  const Bytes key = hex("603deb1015ca71be2b73aef0857d77811f352c073b6108d72d9810a30914dff4"),
              iv = hex("000102030405060708090a0b0c0d0e0f"), pt = hex("6bc1bee22e409f96e93d7e117393172a"),
              ct = hex("f58c4c04d6e5f1ba779eabfb5f7bfbd6");
  uint8_t out[16];
  CHECK(c.aesCbc(true, key.data(), iv.data(), pt.data(), 16, out) && Bytes(out, out + 16) == ct);
  CHECK(c.aesCbc(false, key.data(), iv.data(), ct.data(), 16, out) && Bytes(out, out + 16) == pt);
  CHECK(!c.aesCbc(true, key.data(), iv.data(), pt.data(), 15, out));
  // RFC 5869 test case 1, first 32 bytes of OKM.
  const Bytes ikm(22, 0x0b), salt = hex("000102030405060708090a0b0c"), info = hex("f0f1f2f3f4f5f6f7f8f9");
  uint8_t okm[32];
  CHECK(pin::hkdf32(c, salt.data(), salt.size(), ikm.data(), ikm.size(),
                    std::string(info.begin(), info.end()).c_str(), okm));
  CHECK(Bytes(okm, okm + 32) == hex("3cb25f25faacd57a90434f64d0362f2a2d2d0a90cf1a5a4c5db02d56ecc4c5bf"));

  // ECDH against OpenSSL's own, and a point off the curve is refused.
  uint8_t a[32], aPub[65], b[32], bPub[65], z1[32], z2[32];
  CHECK(c.p256Generate(a, aPub) && c.p256Generate(b, bPub));
  CHECK(c.p256Ecdh(a, bPub, z1) && c.p256Ecdh(b, aPub, z2) && std::memcmp(z1, z2, 32) == 0);
  bPub[64] ^= 1;
  CHECK(!c.p256Ecdh(a, bPub, z1));

  // Both protocols, authenticator side vs the platform code above.
  for (int proto : {1, 2}) {
    pin::Shared s;
    uint8_t priv[32], pub[65];
    CHECK(c.p256Generate(priv, pub));
    Platform p;
    p.proto = proto;
    Value resp;
    resp.type = Value::Type::Map;
    cbor::Writer w;
    w.map(1), w.uint(1), w.map(3), w.uint(3), w.integer(-25), w.integer(-2), w.bytes(pub + 1, 32), w.integer(-3),
        w.bytes(pub + 33, 32);
    CHECK(cbor::decode(w.out.data(), w.out.size(), resp) && p.agree(resp));
    uint8_t peer[65] = {0x04};
    std::memcpy(peer + 1, p.x.data(), 32);
    std::memcpy(peer + 33, p.y.data(), 32);
    CHECK(pin::sharedSecret(c, proto, priv, peer, s));
    CHECK(Bytes(s.hmacKey, s.hmacKey + 32) == p.hmacKey && Bytes(s.aesKey, s.aesKey + 32) == p.aesKey);
    const Bytes msg(48, 0x5A);
    Bytes enc;
    CHECK(pin::encrypt(c, s, msg.data(), msg.size(), enc));
    CHECK(enc.size() == msg.size() + (proto == 2 ? 16 : 0) && p.dec(enc) == msg);
    Bytes back;
    const Bytes penc = p.enc(msg);
    CHECK(pin::decrypt(c, s, penc.data(), penc.size(), back) && back == msg);
    CHECK(!pin::decrypt(c, s, penc.data(), penc.size() - 1, back));
    const Bytes sig = p.auth(p.hmacKey, msg);
    CHECK(pin::verify(c, proto, s.hmacKey, 32, msg.data(), msg.size(), sig));
    Bytes bad = sig;
    bad[0] ^= 1;
    CHECK(!pin::verify(c, proto, s.hmacKey, 32, msg.data(), msg.size(), bad));
    CHECK(!pin::verify(c, proto, s.hmacKey, 32, msg.data(), msg.size(), Bytes(sig.begin(), sig.begin() + 8)));
  }
}

void recordAndPolicy() {
  pin::State st;
  st.retries = 5;
  for (int i = 0; i < 16; ++i) st.hash[i] = static_cast<uint8_t>(i);
  const Bytes e = pin::encode(st);
  CHECK(e.size() == 18 && e[0] == 1 && e[1] == 5);
  pin::State back;
  CHECK(pin::decode(e, back) && back.retries == 5 && std::memcmp(back.hash, st.hash, 16) == 0);
  Bytes bad = e;
  bad[0] = 2;
  CHECK(!pin::decode(bad, back));
  bad = e;
  bad[1] = 9;
  CHECK(!pin::decode(bad, back));
  CHECK(!pin::decode(Bytes(e.begin(), e.end() - 1), back));

  Bytes out;
  CHECK(pin::unpad(padPin("1234"), out) && out == str("1234"));
  CHECK(!pin::unpad(padPin("123"), out));
  CHECK(pin::unpad(padPin("\xd8\xb3\xd9\x84\xd8\xa7\xd9\x85"), out));  // 4 Arabic letters, 8 bytes
  CHECK(!pin::unpad(padPin("\xd8\xb3\xd9\x84\xd8\xa7"), out));        // 3 code points, 6 bytes
  CHECK(pin::unpad(padPin(std::string(63, '7')), out) && out.size() == 63);
  CHECK(!pin::unpad(Bytes(64, '7'), out));  // 64 bytes leaves no terminator
  CHECK(!pin::unpad(Bytes(32, 0), out));
}

// ---- getInfo ------------------------------------------------------------------

void getInfoWithAndWithoutPin() {
  Rig r;
  const Bytes common =
      hex("0182" "665532465f5632" "684649444f5f325f30"
          "0282" "6b6372656450726f74656374" "6b686d61632d736563726574"
          "0350b722a2aa5acc48359c915fa93812679d");
  const Bytes tail = hex("051904b0" "06820201" "0708" "081840");
  CHECK(r.info() == cat(cat(cat(hex("00a8"), common),
                                hex("04a5" "62726bf5" "627570f5" "627576f5" "64706c6174f4" "69636c69656e7450696ef4")),
                        tail));
  Platform p;
  CHECK(r.setPin(p, "1234") == ctap::kOk);
  r.store.open = false;  // known while locked, too
  CHECK(r.info() == cat(cat(cat(hex("00a8"), common),
                                hex("04a4" "62726bf5" "627570f5" "64706c6174f4" "69636c69656e7450696ef5")),
                        tail));
  CHECK(r.user.unlockCalls == 1);  // only setPIN waited for the vault
}

// ---- the whole ClientPIN flow, as a platform runs it ------------------------------

void clientPinFlow(int proto) {
  Rig r;
  Platform p;
  p.proto = proto;
  int left = 0;
  CHECK(r.retries(left) == ctap::kOk && left == 8);
  Bytes tok;
  CHECK(r.token(p, "1234", tok) == ctap::kPinNotSet);

  // Before a PIN: MakeCredential and GetAssertion work as before (UV = unlocked).
  Made old;
  Make mo;
  mo.rk = true;
  CHECK(make(r, cdh(1), mo, old) == ctap::kOk && (old.flags & 0x04));

  CHECK(r.setPin(p, "123") == ctap::kPinPolicyViolation);
  CHECK(!r.store.pinSet());
  // A pinUvAuthParam that does not match newPinEnc.
  {
    CHECK(r.agree(p));
    const Bytes newPinEnc = p.enc(padPin("1234"));
    cbor::Writer w;
    w.map(5), w.uint(1), w.uint(proto), w.uint(2), w.uint(ctap::kSetPin), w.uint(3), p.writeKey(w), w.uint(4),
        w.bytes(p.auth(p.hmacKey, cdh(9))), w.uint(5), w.bytes(newPinEnc);
    Value v;
    CHECK(r.call(ctap::kClientPin, w.out, v) == ctap::kPinAuthInvalid);
  }
  CHECK(r.setPin(p, "1234") == ctap::kOk);
  CHECK(r.store.pinSet());
  CHECK(r.setPin(p, "5678") == ctap::kNotAllowed);
  CHECK(r.retries(left) == ctap::kOk && left == 8);

  // A PIN is set: MakeCredential needs it, GetAssertion without it has UV = 0.
  Made m;
  CHECK(make(r, cdh(2), mo, m) == ctap::kPinRequired);
  Make uvOpt = mo;
  uvOpt.uv = true;
  CHECK(make(r, cdh(2), uvOpt, m) == ctap::kUnsupportedOption);
  CHECK(r.token(p, "1234", tok) == ctap::kOk);
  Make withPin = mo;
  withPin.proto = proto;
  withPin.pinAuth = p.auth(tok, cdh(3));
  CHECK(make(r, cdh(3), withPin, m) == ctap::kOk);
  CHECK(m.flags == (0x01 | 0x04 | 0x40));
  withPin.pinAuth[0] ^= 1;
  CHECK(make(r, cdh(3), withPin, m) == ctap::kPinAuthInvalid);
  withPin.pinAuth = p.auth(tok, cdh(4));  // signed for another clientDataHash
  CHECK(make(r, cdh(3), withPin, m) == ctap::kPinAuthInvalid);

  Asserted a;
  Get g;
  CHECK(get(r, cdh(5), g, a) == ctap::kOk);
  CHECK(a.flags == 0x01);  // UP, no UV
  CHECK(a.user.find("id") && !a.user.find("name") && !a.user.find("displayName"));  // no names without UV
  g.uv = true;
  CHECK(get(r, cdh(5), g, a) == ctap::kUnsupportedOption);
  g.uv = false;
  g.proto = proto;
  g.pinAuth = p.auth(tok, cdh(6));
  CHECK(get(r, cdh(6), g, a) == ctap::kOk);
  CHECK(a.flags == 0x05 && a.user.find("name") && a.user.find("name")->text() == "hasan");

  // Zero-length pinAuth with a PIN set: PIN_INVALID after the touch.
  Make probe = mo;
  probe.pinAuth = Bytes{};
  {
    cbor::Writer w;
    const Bytes base = makeReq(cdh(7), mo);
    Value v;
    CHECK(cbor::decode(base.data(), base.size(), v));
    w.map(v.entries.size() + 2);
    w.out.insert(w.out.end(), base.begin() + 1, base.end());
    w.uint(8), w.bytes(Bytes{}), w.uint(9), w.uint(proto);
    const int before = r.user.presenceCalls;
    CHECK(r.call(ctap::kMakeCredential, w.out, v) == ctap::kPinInvalid);
    CHECK(r.user.presenceCalls == before + 1);
  }

  // A lock forgets the token.
  r.auth.vaultLocked();
  CHECK(make(r, cdh(3), Make{withPin.rp, withPin.uid, true, false, 0, false, p.auth(tok, cdh(3)), proto}, m) ==
        ctap::kPinAuthInvalid);

  // changePIN: wrong current PIN costs a retry; the right one sets the new PIN.
  CHECK(r.changePin(p, "9999", "abcd") == ctap::kPinInvalid);
  CHECK(r.retries(left) == ctap::kOk && left == 7);
  CHECK(r.changePin(p, "1234", "ab") == ctap::kPinPolicyViolation);  // checked after the old PIN
  CHECK(r.retries(left) == ctap::kOk && left == 8);
  CHECK(r.changePin(p, "1234", "abcd") == ctap::kOk);
  CHECK(r.token(p, "1234", tok) == ctap::kPinInvalid);
  CHECK(r.token(p, "abcd", tok) == ctap::kOk);
  CHECK(r.retries(left) == ctap::kOk && left == 8);

  // The stored record holds the hash, never the PIN.
  pin::State st;
  CHECK(pin::decode(r.store.pin, st));
  Bytes h = sha(str("abcd"));
  CHECK(std::memcmp(st.hash, h.data(), 16) == 0);
}

void retriesAndBlocking() {
  Rig r;
  Platform p;
  CHECK(r.setPin(p, "2468") == ctap::kOk);
  Bytes tok;
  int left = 0;
  // 3 wrong in a row: PIN_AUTH_BLOCKED until power-up, retries still counted.
  CHECK(r.token(p, "0000", tok) == ctap::kPinInvalid);
  CHECK(r.token(p, "0000", tok) == ctap::kPinInvalid);
  CHECK(r.token(p, "0000", tok) == ctap::kPinAuthBlocked);
  const int writes = r.store.pinWrites;
  CHECK(r.token(p, "2468", tok) == ctap::kPinAuthBlocked);  // even the right PIN, and nothing counted
  CHECK(r.store.pinWrites == writes);
  CHECK(r.retries(left) == ctap::kOk && left == 5);

  // The key agreement key changed after a wrong PIN: an old shared secret no longer works.
  {
    Rig fresh;
    Platform q;
    CHECK(fresh.setPin(q, "2468") == ctap::kOk);
    CHECK(fresh.token(q, "1111", tok) == ctap::kPinInvalid);
    Bytes h = sha(str("2468"));
    h.resize(16);
    cbor::Writer w;  // reuses q's secret without a new getKeyAgreement
    w.map(4), w.uint(1), w.uint(2), w.uint(2), w.uint(ctap::kGetPinToken), w.uint(3), q.writeKey(w), w.uint(6),
        w.bytes(q.enc(h));
    Value v;
    CHECK(fresh.call(ctap::kClientPin, w.out, v) == ctap::kPinInvalid);
  }

  // Power cycle: a new Authenticator over the same store.
  Authenticator again{r.crypto, r.store, r.counter, r.attestation};
  auto token2 = [&](const std::string& pin) {
    cbor::Writer k;
    k.map(2), k.uint(1), k.uint(2), k.uint(2), k.uint(ctap::kGetKeyAgreement);
    Bytes req = cat(Bytes{ctap::kClientPin}, k.out);
    Bytes resp = again.cbor(req.data(), req.size(), r.user, 0, 1);
    Value v;
    CHECK(resp[0] == 0 && cbor::decode(resp.data() + 1, resp.size() - 1, v) && p.agree(v));
    Bytes h = sha(str(pin));
    h.resize(16);
    cbor::Writer w;
    w.map(4), w.uint(1), w.uint(2), w.uint(2), w.uint(ctap::kGetPinToken), w.uint(3), p.writeKey(w), w.uint(6),
        w.bytes(p.enc(h));
    req = cat(Bytes{ctap::kClientPin}, w.out);
    resp = again.cbor(req.data(), req.size(), r.user, 0, 1);
    return resp[0];
  };
  CHECK(token2("0000") == ctap::kPinInvalid);   // 4
  CHECK(token2("0000") == ctap::kPinInvalid);   // 3
  CHECK(token2("2468") == ctap::kOk);           // right: back to 8
  pin::State st;
  CHECK(pin::decode(r.store.pin, st) && st.retries == 8);
  // Wear the retries down to 0, one wrong PIN per power-up (under the limit of 3).
  int status = 0;
  for (int i = 0; i < 8; ++i) {
    Authenticator a{r.crypto, r.store, r.counter, r.attestation};
    cbor::Writer k;
    k.map(2), k.uint(1), k.uint(1), k.uint(2), k.uint(ctap::kGetKeyAgreement);
    Bytes req = cat(Bytes{ctap::kClientPin}, k.out);
    Bytes resp = a.cbor(req.data(), req.size(), r.user, 0, 1);
    Value v;
    Platform one;
    one.proto = 1;
    CHECK(resp[0] == 0 && cbor::decode(resp.data() + 1, resp.size() - 1, v) && one.agree(v));
    Bytes h = sha(str("9999"));
    h.resize(16);
    cbor::Writer w;
    w.map(4), w.uint(1), w.uint(1), w.uint(2), w.uint(ctap::kGetPinToken), w.uint(3), one.writeKey(w), w.uint(6),
        w.bytes(one.enc(h));
    req = cat(Bytes{ctap::kClientPin}, w.out);
    status = a.cbor(req.data(), req.size(), r.user, 0, 1)[0];
    CHECK(status == (i < 7 ? ctap::kPinInvalid : ctap::kPinBlocked));
  }
  CHECK(pin::decode(r.store.pin, st) && st.retries == 0);
  // Blocked: even the right PIN is refused, and only authenticatorReset clears it.
  Authenticator b{r.crypto, r.store, r.counter, r.attestation};
  {
    cbor::Writer k;
    k.map(2), k.uint(1), k.uint(2), k.uint(2), k.uint(ctap::kGetKeyAgreement);
    Bytes req = cat(Bytes{ctap::kClientPin}, k.out);
    Bytes resp = b.cbor(req.data(), req.size(), r.user, 0, 1);
    Value v;
    Platform q;
    CHECK(cbor::decode(resp.data() + 1, resp.size() - 1, v) && q.agree(v));
    Bytes h = sha(str("2468"));
    h.resize(16);
    cbor::Writer w;
    w.map(4), w.uint(1), w.uint(2), w.uint(2), w.uint(ctap::kGetPinToken), w.uint(3), q.writeKey(w), w.uint(6),
        w.bytes(q.enc(h));
    req = cat(Bytes{ctap::kClientPin}, w.out);
    CHECK(b.cbor(req.data(), req.size(), r.user, 0, 1)[0] == ctap::kPinBlocked);
    req = Bytes{ctap::kReset};
    CHECK(b.cbor(req.data(), req.size(), r.user, 0, 1)[0] == ctap::kOk);
  }
  CHECK(!r.store.pinSet());
}

void malformedClientPin() {
  Rig r;
  Value v;
  CHECK(r.call(ctap::kClientPin, {}, v) == ctap::kMissingParameter);
  CHECK(r.call(ctap::kClientPin, hex("a10101"), v) == ctap::kMissingParameter);          // no subCommand
  CHECK(r.call(ctap::kClientPin, hex("a201030202"), v) == ctap::kInvalidParameter);     // protocol 3
  CHECK(r.call(ctap::kClientPin, hex("a201010209"), v) == ctap::kInvalidSubcommand);    // 2.1 permissions
  CHECK(r.call(ctap::kClientPin, hex("a10202"), v) == ctap::kMissingParameter);         // keyAgreement w/o protocol
  CHECK(r.call(ctap::kClientPin, hex("a2010202616b"), v) == ctap::kCborUnexpectedType);  // text subCommand
  // setPIN with a key that is not on the curve.
  Platform p;
  CHECK(r.agree(p));
  p.y[31] ^= 1;
  const Bytes enc = p.enc(padPin("1234"));
  cbor::Writer w;
  w.map(5), w.uint(1), w.uint(2), w.uint(2), w.uint(ctap::kSetPin), w.uint(3), p.writeKey(w), w.uint(4),
      w.bytes(p.auth(p.hmacKey, enc)), w.uint(5), w.bytes(enc);
  CHECK(r.call(ctap::kClientPin, w.out, v) == ctap::kInvalidParameter);
  // A newPinEnc of the wrong size.
  Platform q;
  CHECK(r.agree(q));
  const Bytes shortEnc = q.enc(Bytes(32, '1'));
  cbor::Writer s;
  s.map(5), s.uint(1), s.uint(2), s.uint(2), s.uint(ctap::kSetPin), s.uint(3), q.writeKey(s), s.uint(4),
      s.bytes(q.auth(q.hmacKey, shortEnc)), s.uint(5), s.bytes(shortEnc);
  CHECK(r.call(ctap::kClientPin, s.out, v) == ctap::kInvalidParameter);
  CHECK(!r.store.pinSet());
  // pinAuth without a PIN set (non-empty): PIN_NOT_SET, as before.
  Make m;
  m.pinAuth = Bytes(16, 1);
  Made made;
  CHECK(make(r, cdh(1), m, made) == ctap::kPinNotSet);
  // A locked vault while a PIN request waits: refused, nothing written.
  CHECK(r.setPin(p, "1234") == ctap::kOk);
  r.store.open = false;
  r.user.unlock = User::Answer::Timeout;
  int left = 0;
  CHECK(r.retries(left) == ctap::kOperationDenied);
}

// ---- hmac-secret ------------------------------------------------------------------

Bytes hmacInput(Platform& p, const Bytes& salts, bool sendProtocol) {
  const Bytes enc = p.enc(salts);
  cbor::Writer w;
  w.map(sendProtocol ? 4 : 3);
  w.uint(1), p.writeKey(w);
  w.uint(2), w.bytes(enc);
  w.uint(3), w.bytes(p.auth(p.hmacKey, enc));
  if (sendProtocol) w.uint(4), w.uint(p.proto);
  return w.out;
}

// What the outputs must be, from the documented derivation (docs/FIDO.md).
Bytes expectedOutput(const Bytes& wrapKey, const Bytes& credId, bool uv, const Bytes& salt) {
  const Bytes credRandom = hmac(wrapKey, cat(cat(str("keyra/fido/v1/hmac-secret"), Bytes{uint8_t(uv ? 1 : 0)}), credId));
  return hmac(credRandom, salt);
}

void hmacSecret() {
  for (int proto : {1, 2}) {
    Rig r;
    const Bytes wrapKey(r.store.key, r.store.key + 32);
    Make mo;
    mo.rk = true;
    mo.hmacSecret = true;
    Made m;
    CHECK(make(r, cdh(1), mo, m) == ctap::kOk);
    CHECK((m.flags & 0x80) && m.ext.find("hmac-secret") && m.ext.find("hmac-secret")->b);
    Make plain;
    plain.uid = {7};
    Made other;
    CHECK(make(r, cdh(1), plain, other) == ctap::kOk && !(other.flags & 0x80));

    Platform p;
    p.proto = proto;
    CHECK(r.agree(p));
    const Bytes salt1(32, 0x11), salt2(32, 0x22);
    Get g;
    g.allow = {m.credId};
    g.hmacExt = hmacInput(p, cat(salt1, salt2), proto == 2);
    Asserted a;
    CHECK(get(r, cdh(2), g, a) == ctap::kOk);
    CHECK(a.flags == (0x01 | 0x04 | 0x80));
    const Value* out = a.ext.find("hmac-secret");
    CHECK(out && out->str.size() == 64u + (proto == 2 ? 16u : 0u));
    const Bytes outputs = out ? p.dec(out->str) : Bytes{};
    CHECK(Bytes(outputs.begin(), outputs.begin() + 32) == expectedOutput(wrapKey, m.credId, true, salt1));
    CHECK(Bytes(outputs.begin() + 32, outputs.end()) == expectedOutput(wrapKey, m.credId, true, salt2));

    // One salt; the same answer again (deterministic, nothing stored).
    g.hmacExt = hmacInput(p, salt1, proto == 2);
    CHECK(get(r, cdh(3), g, a) == ctap::kOk);
    CHECK(p.dec(a.ext.find("hmac-secret")->str) == expectedOutput(wrapKey, m.credId, true, salt1));

    // The credential without hmac-secret gives no output.
    g.allow = {other.credId};
    CHECK(get(r, cdh(3), g, a) == ctap::kOk && !(a.flags & 0x80));

    // Under a restored wrapping key (backup on a new Keyra) the secret is the same.
    std::array<uint8_t, 32> restored;
    std::copy(r.store.key, r.store.key + 32, restored.begin());
    r.store.restored.push_back(restored);
    r.store.key[0] ^= 0x77;
    g.allow = {m.credId};
    CHECK(get(r, cdh(3), g, a) == ctap::kOk);
    CHECK(p.dec(a.ext.find("hmac-secret")->str) == expectedOutput(wrapKey, m.credId, true, salt1));

    // A bad saltAuth fails before the touch.
    g.hmacExt = hmacInput(p, salt1, proto == 2);
    const int before = r.user.presenceCalls;
    Bytes broken = g.hmacExt;
    broken[broken.size() - (proto == 2 ? 3 : 1)] ^= 1;  // last byte of saltAuth
    g.hmacExt = broken;
    CHECK(get(r, cdh(3), g, a) == ctap::kPinAuthInvalid);
    CHECK(r.user.presenceCalls == before);
  }

  // With a PIN: UV and non-UV assertions get different secrets.
  Rig r;
  const Bytes wrapKey(r.store.key, r.store.key + 32);
  Platform p;
  CHECK(r.setPin(p, "1357") == ctap::kOk);
  Bytes tok;
  CHECK(r.token(p, "1357", tok) == ctap::kOk);
  Make mo;
  mo.hmacSecret = true;
  mo.pinAuth = p.auth(tok, cdh(1));
  Made m;
  CHECK(make(r, cdh(1), mo, m) == ctap::kOk);
  const Bytes salt(32, 0x33);
  CHECK(r.agree(p));
  Get g;
  g.allow = {m.credId};
  g.hmacExt = hmacInput(p, salt, true);
  Asserted noUv, withUv;
  CHECK(get(r, cdh(2), g, noUv) == ctap::kOk && !(noUv.flags & 0x04));
  CHECK(p.dec(noUv.ext.find("hmac-secret")->str) == expectedOutput(wrapKey, m.credId, false, salt));
  g.pinAuth = p.auth(tok, cdh(2));
  CHECK(get(r, cdh(2), g, withUv) == ctap::kOk && (withUv.flags & 0x04));
  CHECK(p.dec(withUv.ext.find("hmac-secret")->str) == expectedOutput(wrapKey, m.credId, true, salt));
}

// ---- credProtect ----------------------------------------------------------------

void credProtect() {
  Rig r;
  Made l1, l2, l3;
  Make mo;
  mo.rk = true;
  mo.protect = 1;
  mo.uid = {1};
  CHECK(make(r, cdh(1), mo, l1) == ctap::kOk && l1.ext.find("credProtect"));
  mo.protect = 2;
  mo.uid = {2};
  CHECK(make(r, cdh(1), mo, l2) == ctap::kOk);
  mo.protect = 3;
  mo.uid = {3};
  CHECK(make(r, cdh(1), mo, l3) == ctap::kOk);
  int64_t level = 0;
  CHECK(l3.ext.find("credProtect") && l3.ext.find("credProtect")->asInt(level) && level == 3);
  mo.protect = 4;
  Made bad;
  CHECK(make(r, cdh(1), mo, bad) == ctap::kInvalidOption);

  // The level sits in the ID's flags byte.
  uint8_t priv[32], flags = 0;
  const Bytes rpHash = sha(str("example.com"));
  CHECK(cred::unwrap(r.crypto, r.store.key, rpHash.data(), l3.credId.data(), l3.credId.size(), priv, flags));
  CHECK(cred::protectLevel(flags) == 3 && (flags & cred::kFlagResident));
  CHECK(cred::protectLevel(0x00) == 1 && cred::protectLevel(0x01) == 1);  // IDs from before credProtect

  // No PIN: every request is user-verified (unlocked vault), all three answer.
  Value v;
  Get g;
  CHECK(r.call(ctap::kGetAssertion, getReq(cdh(2), g), v) == ctap::kOk);
  int64_t count = 0;
  CHECK(v.find(5) && v.find(5)->asInt(count) && count == 3);

  // With a PIN and no pinAuth: discoverable sees level 1 only; an allow list reaches
  // level 2 but never level 3.
  Platform p;
  CHECK(r.setPin(p, "8642") == ctap::kOk);
  Asserted a;
  CHECK(r.call(ctap::kGetAssertion, getReq(cdh(2), g), v) == ctap::kOk);
  CHECK(!v.find(5) && v.find(1)->find("id")->str == l1.credId);
  g.allow = {l2.credId};
  CHECK(get(r, cdh(3), g, a) == ctap::kOk && a.credId == l2.credId);
  g.allow = {l3.credId};
  CHECK(get(r, cdh(3), g, a) == ctap::kNoCredentials);
  g.allow = {l3.credId, l1.credId};  // the level 3 one is skipped, the next one used
  CHECK(get(r, cdh(3), g, a) == ctap::kOk && a.credId == l1.credId);

  // With the PIN everything is back.
  Bytes tok;
  CHECK(r.token(p, "8642", tok) == ctap::kOk);
  g.allow = {l3.credId};
  g.pinAuth = p.auth(tok, cdh(4));
  CHECK(get(r, cdh(4), g, a) == ctap::kOk && a.credId == l3.credId);
  Get disc;
  disc.pinAuth = p.auth(tok, cdh(5));
  CHECK(r.call(ctap::kGetAssertion, getReq(cdh(5), disc), v) == ctap::kOk);
  CHECK(v.find(5) && v.find(5)->asInt(count) && count == 3);
}

void credProtectOverU2f() {
  Rig r;
  Made l3, l2;
  Make mo;
  mo.protect = 3;
  CHECK(make(r, cdh(1), mo, l3) == ctap::kOk);
  mo.protect = 2;
  CHECK(make(r, cdh(1), mo, l2) == ctap::kOk);
  const Bytes app = sha(str("example.com")), chal(32, 0xC1);
  auto authenticate = [&](const Bytes& kh) {
    Bytes a{0x00, 0x02, 0x03, 0x00, 0x00};
    Bytes body = cat(cat(cat(chal, app), Bytes{uint8_t(kh.size())}), kh);
    a.push_back(static_cast<uint8_t>(body.size() >> 8));
    a.push_back(static_cast<uint8_t>(body.size()));
    a = cat(cat(a, body), Bytes{0, 0});
    const Bytes resp = r.auth.msg(a.data(), a.size(), r.user);
    return static_cast<uint16_t>(resp[resp.size() - 2] << 8 | resp.back());
  };
  CHECK(authenticate(l3.credId) == u2f::kSwWrongData);
  CHECK(authenticate(l2.credId) == u2f::kSwOk);
}

}  // namespace

int main() {
  knownAnswers();
  recordAndPolicy();
  getInfoWithAndWithoutPin();
  clientPinFlow(1);
  clientPinFlow(2);
  retriesAndBlocking();
  malformedClientPin();
  hmacSecret();
  credProtect();
  credProtectOverU2f();
  return KEYRA_TEST_RESULT();
}
