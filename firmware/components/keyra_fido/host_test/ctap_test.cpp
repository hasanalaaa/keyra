// CTAP2 and U2F: known-answer GetInfo, MakeCredential/GetAssertion round trips
// whose ES256 signatures are checked with OpenSSL's verifier (independent of
// the signing path), resident credentials, U2F formats, error paths.
#include <cstring>
#include <string>

#include "core/cbor.hpp"
#include "core/ctap.hpp"
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
    const Bytes r = auth.cbor(req.data(), req.size(), user, 0);
    out = Value{};
    if (r.size() > 1) CHECK(cbor::decode(r.data() + 1, r.size() - 1, out));
    return r[0];
  }
  Bytes hash(const std::string& s) {
    Bytes h(32);
    crypto.sha256(reinterpret_cast<const uint8_t*>(s.data()), s.size(), h.data());
    return h;
  }
};

Bytes cdh(uint8_t fill) { return Bytes(32, fill); }

struct MakeOpts {
  std::string rp = "example.com";
  Bytes uid = {1, 2, 3, 4};
  std::string name = "hasan@example.com", display = "Hasan";
  bool rk = false;
  bool withUp = false, up = true;
  std::vector<Bytes> exclude;
  int64_t alg = -7;
};

Bytes makeReq(const Bytes& clientDataHash, const MakeOpts& o) {
  cbor::Writer w;
  const bool options = o.rk || o.withUp;
  w.map(4 + (o.exclude.empty() ? 0 : 1) + (options ? 1 : 0));
  w.uint(1), w.bytes(clientDataHash);
  w.uint(2), w.map(2), w.text("id"), w.text(o.rp), w.text("name"), w.text("Example");
  w.uint(3), w.map(3), w.text("id"), w.bytes(o.uid), w.text("name"), w.text(o.name), w.text("displayName"),
      w.text(o.display);
  w.uint(4), w.array(2);
  w.map(2), w.text("alg"), w.integer(-8), w.text("type"), w.text("public-key");  // EdDSA first: skipped
  w.map(2), w.text("alg"), w.integer(o.alg), w.text("type"), w.text("public-key");
  if (!o.exclude.empty()) {
    w.uint(5), w.array(o.exclude.size());
    for (const auto& id : o.exclude) w.map(2), w.text("id"), w.bytes(id), w.text("type"), w.text("public-key");
  }
  if (options) {
    w.uint(7), w.map((o.rk ? 1 : 0) + (o.withUp ? 1 : 0));
    if (o.rk) w.text("rk"), w.boolean(true);
    if (o.withUp) w.text("up"), w.boolean(o.up);
  }
  return w.out;
}

Bytes getReq(const std::string& rp, const Bytes& clientDataHash, const std::vector<Bytes>& allow, int up = -1,
             bool rkOption = false) {
  cbor::Writer w;
  const bool options = up >= 0 || rkOption;
  w.map(2 + (allow.empty() ? 0 : 1) + (options ? 1 : 0));
  w.uint(1), w.text(rp);
  w.uint(2), w.bytes(clientDataHash);
  if (!allow.empty()) {
    w.uint(3), w.array(allow.size());
    for (const auto& id : allow) w.map(2), w.text("id"), w.bytes(id), w.text("type"), w.text("public-key");
  }
  if (options) {
    w.uint(5), w.map((up >= 0 ? 1 : 0) + (rkOption ? 1 : 0));
    if (up >= 0) w.text("up"), w.boolean(up == 1);
    if (rkOption) w.text("rk"), w.boolean(true);
  }
  return w.out;
}

struct AuthData {
  Bytes rpIdHash;
  uint8_t flags = 0;
  uint32_t counter = 0;
  Bytes aaguid, credId;
  uint8_t pub[65] = {};
};

bool parseAuthData(const Bytes& a, AuthData& out) {
  if (a.size() < 37) return false;
  out.rpIdHash.assign(a.begin(), a.begin() + 32);
  out.flags = a[32];
  out.counter = uint32_t(a[33]) << 24 | a[34] << 16 | a[35] << 8 | a[36];
  if (!(out.flags & 0x40)) return a.size() == 37;
  if (a.size() < 37 + 18) return false;
  out.aaguid.assign(a.begin() + 37, a.begin() + 53);
  const size_t len = size_t(a[53]) << 8 | a[54];
  if (a.size() < 55 + len) return false;
  out.credId.assign(a.begin() + 55, a.begin() + 55 + len);
  Value cose;
  if (!cbor::decode(a.data() + 55 + len, a.size() - 55 - len, cose)) return false;
  int64_t kty, alg, crv;
  const Value *x = cose.find(-2), *y = cose.find(-3);
  if (!cose.find(1) || !cose.find(1)->asInt(kty) || kty != 2 || !cose.find(3) || !cose.find(3)->asInt(alg) ||
      alg != -7 || !cose.find(-1) || !cose.find(-1)->asInt(crv) || crv != 1 || !x || !y || x->str.size() != 32 ||
      y->str.size() != 32)
    return false;
  out.pub[0] = 0x04;
  std::memcpy(out.pub + 1, x->str.data(), 32);
  std::memcpy(out.pub + 33, y->str.data(), 32);
  return true;
}

Bytes cat(Bytes a, const Bytes& b) {
  a.insert(a.end(), b.begin(), b.end());
  return a;
}

// MakeCredential → authData (checked) and the credential's public key.
bool make(Rig& r, const Bytes& h, const MakeOpts& o, AuthData& ad) {
  Value v;
  if (r.call(ctap::kMakeCredential, makeReq(h, o), v) != ctap::kOk) return false;
  const Value *fmt = v.find(1), *authData = v.find(2), *att = v.find(3);
  CHECK(fmt && fmt->text() == "packed");
  CHECK(authData && parseAuthData(authData->str, ad));
  CHECK(ad.flags == (0x01 | 0x04 | 0x40));
  CHECK(ad.rpIdHash == r.hash(o.rp));
  CHECK(ad.aaguid == Bytes(kAaguid.begin(), kAaguid.end()));
  CHECK(ad.credId.size() == 62);
  // Self attestation: the credential key signs authData || clientDataHash.
  int64_t alg = 0;
  CHECK(att && att->find("alg") && att->find("alg")->asInt(alg) && alg == -7 && !att->find("x5c"));
  CHECK(att && att->find("sig") && verifyEs256(ad.pub, cat(authData->str, h), att->find("sig")->str));
  return true;
}

void getInfoKnownAnswer() {
  Rig r;
  const Bytes req{ctap::kGetInfo};
  const Bytes got = r.auth.cbor(req.data(), req.size(), r.user, 0);
  const Bytes expected = hex(
      "00a4"
      "0182" "665532465f5632" "684649444f5f325f30"
      "0350b722a2aa5acc48359c915fa93812679d"
      "04a4" "62726bf5" "627570f5" "627576f5" "64706c6174f4"
      "051904b0");
  CHECK(got == expected);
  CHECK(r.user.presenceCalls == 0 && r.user.unlockCalls == 0);
}

void roundTripAllowList() {
  Rig r;
  AuthData ad;
  MakeOpts o;
  CHECK(make(r, cdh(0x11), o, ad));
  CHECK(r.store.recs.empty());  // not resident
  CHECK(ad.counter == 1);
  CHECK(r.user.presenceCalls == 1);

  Value v;
  CHECK(r.call(ctap::kGetAssertion, getReq("example.com", cdh(0x22), {Bytes(62, 0), ad.credId}), v) == ctap::kOk);
  const Value *cred = v.find(1), *authData = v.find(2), *sig = v.find(3);
  CHECK(cred && cred->find("id") && cred->find("id")->str == ad.credId);
  CHECK(cred && cred->find("type") && cred->find("type")->text() == "public-key");
  AuthData a2;
  CHECK(authData && parseAuthData(authData->str, a2) && a2.flags == 0x05 && a2.counter == 2);
  CHECK(sig && verifyEs256(ad.pub, cat(authData->str, cdh(0x22)), sig->str));
  CHECK(!verifyEs256(ad.pub, cat(authData->str, cdh(0x23)), sig->str));  // the verifier really checks
  CHECK(!v.find(4) && !v.find(5));  // non-resident: no user, no count

  // Another RP cannot use it; neither can a tampered ID.
  CHECK(r.call(ctap::kGetAssertion, getReq("evil.example", cdh(1), {ad.credId}), v) == ctap::kNoCredentials);
  Bytes bad = ad.credId;
  bad[20] ^= 1;
  CHECK(r.call(ctap::kGetAssertion, getReq("example.com", cdh(1), {bad}), v) == ctap::kNoCredentials);

  // Silent assertion (up=false): no press asked, UP flag clear.
  const int before = r.user.presenceCalls;
  CHECK(r.call(ctap::kGetAssertion, getReq("example.com", cdh(3), {ad.credId}, 0), v) == ctap::kOk);
  CHECK(r.user.presenceCalls == before && v.find(2)->str[32] == 0x04);
}

void excludeList() {
  Rig r;
  AuthData ad;
  CHECK(make(r, cdh(1), MakeOpts{}, ad));
  MakeOpts o;
  o.exclude = {Bytes(10, 7), ad.credId};
  Value v;
  r.user.presenceCalls = 0;
  CHECK(r.call(ctap::kMakeCredential, makeReq(cdh(2), o), v) == ctap::kCredentialExcluded);
  CHECK(r.user.presenceCalls == 1);
  CHECK(r.counter.value == 1);
  // Excluded credential of another RP does not match.
  o.rp = "other.example";
  CHECK(make(r, cdh(2), o, ad));
}

void residentCredentials() {
  Rig r;
  AuthData a1, a2, a3;
  MakeOpts o;
  o.rk = true;
  r.user.unix = 100;
  CHECK(make(r, cdh(1), o, a1));
  o.uid = {9, 9};
  o.name = "second@example.com";
  o.display = "Second";
  r.user.unix = 200;
  CHECK(make(r, cdh(1), o, a2));
  MakeOpts other = o;
  other.rp = "other.example";
  CHECK(make(r, cdh(1), other, a3));
  CHECK(r.store.recs.size() == 3);

  // Discoverable: newest first, count, user with names.
  Value v;
  CHECK(r.call(ctap::kGetAssertion, getReq("example.com", cdh(5), {}), v) == ctap::kOk);
  CHECK(v.find(1)->find("id")->str == a2.credId);
  CHECK(v.find(5) && v.find(5)->u == 2);
  const Value* u = v.find(4);
  CHECK(u && u->find("id")->str == Bytes({9, 9}) && u->find("name")->text() == "second@example.com" &&
        u->find("displayName")->text() == "Second");
  CHECK(verifyEs256(a2.pub, cat(v.find(2)->str, cdh(5)), v.find(3)->str));
  CHECK(r.user.presenceCalls == 4);  // one press for the whole sequence

  CHECK(r.call(ctap::kGetNextAssertion, {}, v) == ctap::kOk);
  CHECK(v.find(1)->find("id")->str == a1.credId && !v.find(5));
  CHECK(v.find(4)->find("name")->text() == "hasan@example.com");
  CHECK(verifyEs256(a1.pub, cat(v.find(2)->str, cdh(5)), v.find(3)->str));
  CHECK(r.call(ctap::kGetNextAssertion, {}, v) == ctap::kNotAllowed);
  CHECK(r.user.presenceCalls == 4);

  // Same RP + user handle replaces the passkey.
  r.user.unix = 300;
  AuthData a4;
  CHECK(make(r, cdh(1), o, a4));
  CHECK(r.store.recs.size() == 3);
  CHECK(r.call(ctap::kGetAssertion, getReq("example.com", cdh(6), {a2.credId}), v) == ctap::kNoCredentials);

  // Deleting the record revokes the credential, also through an allow list.
  CHECK(r.call(ctap::kGetAssertion, getReq("example.com", cdh(6), {a1.credId}), v) == ctap::kOk);
  CHECK(v.find(4) && v.find(4)->find("id")->str == Bytes({1, 2, 3, 4}));  // resident: user handle included
  for (auto it = r.store.recs.begin(); it != r.store.recs.end();) {
    Value rec;
    cbor::decode(it->second.data(), it->second.size(), rec);
    it = rec.find(7)->str == a1.credId ? r.store.recs.erase(it) : std::next(it);
  }
  CHECK(r.call(ctap::kGetAssertion, getReq("example.com", cdh(6), {a1.credId}), v) == ctap::kNoCredentials);
  CHECK(r.call(ctap::kGetAssertion, getReq("nobody.example", cdh(6), {}), v) == ctap::kNoCredentials);

  // GetNextAssertion without a preceding GetAssertion.
  CHECK(r.call(ctap::kGetNextAssertion, {}, v) == ctap::kNotAllowed);
}

void storeFull() {
  Rig r;
  MakeOpts o;
  o.rk = true;
  for (int i = 0; i < 50; ++i) {
    o.uid = {static_cast<uint8_t>(i), 1};
    AuthData ad;
    CHECK(make(r, cdh(1), o, ad));
  }
  o.uid = {200, 1};
  Value v;
  const int before = r.user.presenceCalls;
  CHECK(r.call(ctap::kMakeCredential, makeReq(cdh(1), o), v) == ctap::kKeyStoreFull);
  CHECK(r.user.presenceCalls == before);  // refused before asking for a press
  o.uid = {0, 1};  // replacing an existing one still works
  AuthData ad;
  CHECK(make(r, cdh(1), o, ad));
}

void presenceOutcomes() {
  Rig r;
  Value v;
  const std::pair<User::Answer, uint8_t> cases[] = {{User::Answer::Denied, ctap::kOperationDenied},
                                                    {User::Answer::Timeout, ctap::kUserActionTimeout},
                                                    {User::Answer::Cancelled, ctap::kKeepaliveCancel}};
  MakeOpts o;
  o.rk = true;
  for (const auto& [answer, status] : cases) {
    r.user.presence = {answer};
    CHECK(r.call(ctap::kMakeCredential, makeReq(cdh(1), o), v) == status);
  }
  CHECK(r.store.recs.empty() && r.counter.value == 0);

  // Locked: waits for an unlock; staying locked is "operation denied".
  r.store.open = false;
  r.user.unlock = User::Answer::Timeout;
  CHECK(r.call(ctap::kMakeCredential, makeReq(cdh(1), o), v) == ctap::kOperationDenied);
  CHECK(r.call(ctap::kGetAssertion, getReq("example.com", cdh(1), {}), v) == ctap::kOperationDenied);
  CHECK(r.counter.value == 0);
  r.user.unlock = User::Answer::Approved;  // unlocked on the phone while waiting
  AuthData ad;
  CHECK(make(r, cdh(1), o, ad));
  CHECK(r.store.open);
}

void malformedRequests() {
  Rig r;
  Value v;
  const Bytes empty;
  CHECK(r.auth.cbor(empty.data(), 0, r.user, 0) == Bytes{ctap::kInvalidLength});
  CHECK(r.call(0x40, {}, v) == ctap::kInvalidCommand);
  CHECK(r.call(ctap::kClientPin, hex("a1010102"), v) == ctap::kInvalidCommand);
  CHECK(r.call(ctap::kMakeCredential, {}, v) == ctap::kMissingParameter);
  CHECK(r.call(ctap::kMakeCredential, hex("a201"), v) == ctap::kInvalidCbor);         // truncated
  CHECK(r.call(ctap::kMakeCredential, hex("a2010203"), v) == ctap::kInvalidCbor);     // 2 entries, 1 given
  CHECK(r.call(ctap::kMakeCredential, hex("8101"), v) == ctap::kCborUnexpectedType);  // array, not map
  CHECK(r.call(ctap::kMakeCredential, hex("a1616101"), v) == ctap::kCborUnexpectedType);  // text key
  CHECK(r.call(ctap::kGetAssertion, hex("a10161"), v) == ctap::kInvalidCbor);
  CHECK(r.call(ctap::kGetAssertion, hex("a1016161"), v) == ctap::kMissingParameter);  // no clientDataHash

  MakeOpts o;
  CHECK(r.call(ctap::kMakeCredential, makeReq(Bytes(31, 1), o), v) == ctap::kInvalidLength);
  o.alg = -257;
  CHECK(r.call(ctap::kMakeCredential, makeReq(cdh(1), o), v) == ctap::kUnsupportedAlgorithm);
  o = MakeOpts{};
  o.uid = Bytes(65, 1);
  CHECK(r.call(ctap::kMakeCredential, makeReq(cdh(1), o), v) == ctap::kInvalidLength);
  o = MakeOpts{};
  o.withUp = true, o.up = false;
  CHECK(r.call(ctap::kMakeCredential, makeReq(cdh(1), o), v) == ctap::kInvalidOption);
  CHECK(r.call(ctap::kGetAssertion, getReq("example.com", cdh(1), {}, -1, true), v) == ctap::kUnsupportedOption);
  // excludeList entry that is not a map
  cbor::Writer w;
  w.map(5);
  w.uint(1), w.bytes(cdh(1));
  w.uint(2), w.map(1), w.text("id"), w.text("a.example");
  w.uint(3), w.map(1), w.text("id"), w.bytes(Bytes{1});
  w.uint(4), w.array(1), w.map(2), w.text("alg"), w.integer(-7), w.text("type"), w.text("public-key");
  w.uint(5), w.array(1), w.uint(3);
  CHECK(r.call(ctap::kMakeCredential, w.out, v) == ctap::kCborUnexpectedType);
  CHECK(r.counter.value == 0 && r.user.presenceCalls == 0);

  // Zero-length pinAuth: "touch to select" → PIN_NOT_SET after the touch.
  cbor::Writer p;
  p.map(5);
  p.uint(1), p.bytes(cdh(1));
  p.uint(2), p.map(1), p.text("id"), p.text("a.example");
  p.uint(3), p.map(1), p.text("id"), p.bytes(Bytes{1});
  p.uint(4), p.array(1), p.map(2), p.text("alg"), p.integer(-7), p.text("type"), p.text("public-key");
  p.uint(8), p.bytes(Bytes{});
  CHECK(r.call(ctap::kMakeCredential, p.out, v) == ctap::kPinNotSet);
  CHECK(r.user.presenceCalls == 1 && r.counter.value == 0);
}

void resetAndSelection() {
  Rig r;
  AuthData ad;
  MakeOpts o;
  o.rk = true;
  CHECK(make(r, cdh(1), o, ad));
  Value v;
  r.user.uptime = kResetWindowMs + 1;
  CHECK(r.call(ctap::kReset, {}, v) == ctap::kNotAllowed);
  CHECK(r.store.recs.size() == 1);
  r.user.uptime = 5000;
  r.user.presence = {User::Answer::Denied};
  CHECK(r.call(ctap::kReset, {}, v) == ctap::kOperationDenied);
  CHECK(r.store.recs.size() == 1);
  CHECK(r.call(ctap::kReset, {}, v) == ctap::kOk);
  CHECK(r.store.recs.empty());
  CHECK(r.call(ctap::kGetAssertion, getReq("example.com", cdh(1), {ad.credId}), v) == ctap::kNoCredentials);

  CHECK(r.call(ctap::kSelection, {}, v) == ctap::kOk);
  r.user.presence = {User::Answer::Timeout};
  CHECK(r.call(ctap::kSelection, {}, v) == ctap::kUserActionTimeout);
}

void derEncoding() {
  uint8_t sig[64] = {};
  sig[0] = 0x80;  // r has the top bit set → 0x00 pad
  sig[63] = 0x05;  // s = 5 → one byte
  const Bytes d = derSignature(sig);
  CHECK(d.size() == 2 + 2 + 33 + 2 + 1);
  CHECK(d[0] == 0x30 && d[1] == d.size() - 2 && d[2] == 0x02 && d[3] == 33 && d[4] == 0x00 && d[5] == 0x80);
  CHECK(d[d.size() - 3] == 0x02 && d[d.size() - 2] == 1 && d.back() == 5);
}

// ---- U2F ------------------------------------------------------------------

Bytes apdu(uint8_t ins, uint8_t p1, const Bytes& data, bool extended = true) {
  Bytes a{0x00, ins, p1, 0x00};
  if (extended) {
    a.push_back(0);
    a.push_back(static_cast<uint8_t>(data.size() >> 8));
    a.push_back(static_cast<uint8_t>(data.size()));
  } else {
    a.push_back(static_cast<uint8_t>(data.size()));
  }
  a.insert(a.end(), data.begin(), data.end());
  if (extended) a.insert(a.end(), {0, 0});
  return a;
}

uint16_t sw(const Bytes& r) { return static_cast<uint16_t>(r[r.size() - 2] << 8 | r.back()); }

void u2fFlows() {
  Rig r;
  const Bytes chal(32, 0xC1);
  const Bytes app = r.hash("https://u2f.example");
  const Bytes ver = r.auth.msg(hex("0003000000").data(), 5, r.user);
  CHECK(ver == Bytes({'U', '2', 'F', '_', 'V', '2', 0x90, 0x00}));
  Bytes a = apdu(0x03, 0, {});
  CHECK(sw(r.auth.msg(a.data(), a.size(), r.user)) == 0x9000);

  // REGISTER: not pressed yet → conditions not satisfied (the host polls).
  r.user.latched = false;
  a = apdu(0x01, 0x03, cat(chal, app));
  CHECK(r.auth.msg(a.data(), a.size(), r.user) == Bytes({0x69, 0x85}));
  r.user.latched = true;
  Bytes reg = r.auth.msg(a.data(), a.size(), r.user);
  CHECK(sw(reg) == 0x9000);
  reg.resize(reg.size() - 2);
  CHECK(reg[0] == 0x05 && reg[1] == 0x04);
  const Bytes pub(reg.begin() + 1, reg.begin() + 66);
  const size_t khLen = reg[66];
  CHECK(khLen == 62);
  const Bytes kh(reg.begin() + 67, reg.begin() + 67 + khLen);
  const size_t certAt = 67 + khLen;
  CHECK(reg[certAt] == 0x30 && reg[certAt + 1] == 0x82);
  const size_t certLen = 4 + (size_t(reg[certAt + 2]) << 8 | reg[certAt + 3]);
  const Bytes cert(reg.begin() + certAt, reg.begin() + certAt + certLen);
  // Per-device attestation: made on first use, self-signed, then reused.
  CHECK(r.attestation.has && r.attestation.saves == 1 && cert == r.attestation.cert);
  CHECK(selfSignedCertOk(cert));
  const Bytes regSig(reg.begin() + certAt + certLen, reg.end());
  const Bytes signedReg = cat(cat(cat(cat(Bytes{0}, app), chal), kh), pub);
  CHECK(verifyWithCert(cert, signedReg, regSig));

  Bytes reg2 = r.auth.msg(a.data(), a.size(), r.user);
  CHECK(sw(reg2) == 0x9000 && r.attestation.saves == 1);
  CHECK(std::search(reg2.begin(), reg2.end(), cert.begin(), cert.end()) != reg2.end());
  // A wiped store (factory reset) makes a new key and certificate.
  r.attestation.has = false;
  reg2 = r.auth.msg(a.data(), a.size(), r.user);
  CHECK(sw(reg2) == 0x9000 && r.attestation.saves == 2 && r.attestation.cert != cert);
  CHECK(selfSignedCertOk(r.attestation.cert));

  // AUTHENTICATE: check-only, enforce, don't-enforce.
  const Bytes body = cat(cat(cat(chal, app), Bytes{static_cast<uint8_t>(kh.size())}), kh);
  a = apdu(0x02, 0x07, body);
  CHECK(r.auth.msg(a.data(), a.size(), r.user) == Bytes({0x69, 0x85}));
  a = apdu(0x02, 0x03, body);
  Bytes auth = r.auth.msg(a.data(), a.size(), r.user);
  CHECK(sw(auth) == 0x9000 && auth[0] == 0x01);
  auth.resize(auth.size() - 2);
  const uint32_t ctr = uint32_t(auth[1]) << 24 | auth[2] << 16 | auth[3] << 8 | auth[4];
  CHECK(ctr == r.counter.value);
  const Bytes signedAuth = cat(cat(app, Bytes(auth.begin(), auth.begin() + 5)), chal);
  CHECK(verifyEs256(pub.data(), signedAuth, Bytes(auth.begin() + 5, auth.end())));
  a = apdu(0x02, 0x08, body);
  auth = r.auth.msg(a.data(), a.size(), r.user);
  CHECK(sw(auth) == 0x9000 && auth[0] == 0x00);
  r.user.latched = false;
  a = apdu(0x02, 0x03, body);
  CHECK(r.auth.msg(a.data(), a.size(), r.user) == Bytes({0x69, 0x85}));
  r.user.latched = true;

  // Wrong application / handle → wrong data. Short-form Lc works too.
  const Bytes otherApp = r.hash("https://evil.example");
  a = apdu(0x02, 0x03, cat(cat(cat(chal, otherApp), Bytes{62}), kh), false);
  CHECK(r.auth.msg(a.data(), a.size(), r.user) == Bytes({0x6A, 0x80}));
  a = apdu(0x02, 0x07, cat(cat(cat(chal, app), Bytes{3}), Bytes{1, 2, 3}));
  CHECK(r.auth.msg(a.data(), a.size(), r.user) == Bytes({0x6A, 0x80}));

  // The U2F credential works through CTAP2 with the appid as rpId.
  Value v;
  CHECK(r.call(ctap::kGetAssertion, getReq("https://u2f.example", cdh(9), {kh}), v) == ctap::kOk);
  CHECK(verifyEs256(pub.data(), cat(v.find(2)->str, cdh(9)), v.find(3)->str));

  // Locked: register/enforce ask to retry, check-only says "unknown".
  r.store.open = false;
  a = apdu(0x02, 0x03, body);
  CHECK(r.auth.msg(a.data(), a.size(), r.user) == Bytes({0x69, 0x85}));
  a = apdu(0x02, 0x07, body);
  CHECK(r.auth.msg(a.data(), a.size(), r.user) == Bytes({0x6A, 0x80}));
  a = apdu(0x01, 0x03, cat(chal, app));
  CHECK(r.auth.msg(a.data(), a.size(), r.user) == Bytes({0x69, 0x85}));
  r.store.open = true;

  // Malformed APDUs.
  a = {0x80, 0x03, 0, 0};
  CHECK(r.auth.msg(a.data(), a.size(), r.user) == Bytes({0x6E, 0x00}));
  a = {0x00, 0x09, 0, 0};
  CHECK(r.auth.msg(a.data(), a.size(), r.user) == Bytes({0x6D, 0x00}));
  a = {0x00, 0x01};
  CHECK(r.auth.msg(a.data(), a.size(), r.user) == Bytes({0x67, 0x00}));
  a = apdu(0x01, 0x03, Bytes(63, 1));
  CHECK(r.auth.msg(a.data(), a.size(), r.user) == Bytes({0x67, 0x00}));
  a = apdu(0x02, 0x03, cat(cat(chal, app), Bytes{70, 1}));  // handle length beyond the data
  CHECK(r.auth.msg(a.data(), a.size(), r.user) == Bytes({0x67, 0x00}));
  a = apdu(0x01, 0x03, cat(chal, app));
  a.push_back(0);  // stray byte after Le
  CHECK(r.auth.msg(a.data(), a.size(), r.user) == Bytes({0x67, 0x00}));
}

}  // namespace

int main() {
  getInfoKnownAnswer();
  roundTripAllowList();
  excludeList();
  residentCredentials();
  storeFull();
  presenceOutcomes();
  malformedRequests();
  resetAndSelection();
  derEncoding();
  u2fFlows();
  return KEYRA_TEST_RESULT();
}
