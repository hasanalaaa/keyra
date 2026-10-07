// CTAP 2.1 §6 (authenticator API) and FIDO U2F raw message formats. Behaviour
// was checked against the specifications and, for edge cases, against how
// LionKey (MIT) and Solo 1 (Apache-2.0/MIT) answer; no code was taken from them.
#include "ctap.hpp"

#include <algorithm>
#include <cstring>

#include "cbor.hpp"

namespace keyra::fido {
namespace {

using cbor::Value;
using T = cbor::Value::Type;
using namespace ctap;

constexpr uint8_t kFlagUp = 0x01, kFlagUv = 0x04, kFlagAt = 0x40;
constexpr int64_t kEs256 = -7;

uint8_t fromAnswer(User::Answer a) {
  switch (a) {
    case User::Answer::Approved: return kOk;
    case User::Answer::Denied: return kOperationDenied;
    case User::Answer::Timeout: return kUserActionTimeout;
    case User::Answer::Cancelled: return kKeepaliveCancel;
  }
  return kOther;
}

// Unlock wait has its own meaning on timeout: Keyra stayed locked.
uint8_t fromUnlock(User::Answer a) {
  return a == User::Answer::Timeout ? uint8_t{kOperationDenied} : fromAnswer(a);
}

void put32(std::vector<uint8_t>& v, uint32_t x) {
  for (int s = 24; s >= 0; s -= 8) v.push_back(static_cast<uint8_t>(x >> s));
}

// Parses the request map. Keys must be unsigned integers (CTAP2 parameters).
uint8_t parseParams(const uint8_t* p, size_t n, Value& out) {
  if (n == 0) return kMissingParameter;  // every command below needs parameters
  if (!cbor::decode(p, n, out)) return kInvalidCbor;
  if (out.type != T::Map) return kCborUnexpectedType;
  for (const auto& e : out.entries)
    if (e.first.type != T::Uint) return kCborUnexpectedType;
  return kOk;
}

// options map: known keys must be booleans; others are ignored.
uint8_t readOption(const Value* options, const char* key, bool& out) {
  if (!options) return kOk;
  const Value* v = options->find(key);
  if (!v) return kOk;
  if (v->type != T::Bool) return kCborUnexpectedType;
  out = v->b;
  return kOk;
}

// PublicKeyCredentialDescriptor list → credential IDs of type "public-key".
uint8_t readCredList(const Value* list, std::vector<std::vector<uint8_t>>& out) {
  out.clear();
  if (!list) return kOk;
  if (list->type != T::Array) return kCborUnexpectedType;
  for (const Value& d : list->items) {
    if (d.type != T::Map) return kCborUnexpectedType;
    const Value* type = d.find("type");
    const Value* id = d.find("id");
    if (!type || type->type != T::Text || !id || id->type != T::Bytes) return kCborUnexpectedType;
    if (type->text() == "public-key") out.push_back(id->str);
  }
  return kOk;
}

void writeCose(cbor::Writer& w, const uint8_t pub[65]) {
  w.map(5);
  w.uint(1), w.uint(2);           // kty: EC2
  w.uint(3), w.integer(kEs256);  // alg: ES256
  w.integer(-1), w.uint(1);      // crv: P-256
  w.integer(-2), w.bytes(pub + 1, 32);
  w.integer(-3), w.bytes(pub + 33, 32);
}

}  // namespace

std::vector<uint8_t> Authenticator::cbor(const uint8_t* req, size_t n, User& user, int64_t now) {
  std::vector<uint8_t> body;
  uint8_t status = kInvalidLength;
  if (n >= 1) {
    const uint8_t cmd = req[0];
    if (cmd != kGetNextAssertion) next_ = Next{};  // only valid right after GetAssertion
    switch (cmd) {
      case kMakeCredential: status = makeCredential(req + 1, n - 1, user, body); break;
      case kGetAssertion: status = getAssertion(req + 1, n - 1, user, now, body); break;
      case kGetNextAssertion: status = getNextAssertion(now, body); break;
      case kGetInfo: status = getInfo(body); break;
      case kReset: status = reset(user); break;
      case kSelection: status = selection(user); break;
      default: status = kInvalidCommand; break;  // ClientPIN and 2.1 management are not offered
    }
  }
  if (status != kOk) body.clear();
  body.insert(body.begin(), status);
  return body;
}

uint8_t Authenticator::getInfo(std::vector<uint8_t>& out) {
  cbor::Writer w;
  w.map(4);
  w.uint(1);
  w.array(2), w.text("U2F_V2"), w.text("FIDO_2_0");
  w.uint(3), w.bytes(kAaguid.data(), kAaguid.size());
  w.uint(4);
  w.map(4);  // canonical order: shorter keys first, then bytewise
  w.text("rk"), w.boolean(true);
  w.text("up"), w.boolean(true);
  w.text("uv"), w.boolean(true);  // "verified" = the vault is unlocked (docs/FIDO.md)
  w.text("plat"), w.boolean(false);
  w.uint(5), w.uint(1200);  // maxMsgSize: bounds what we parse; hosts split long allow lists
  out = std::move(w.out);
  return kOk;
}

bool Authenticator::residents(std::vector<cred::Resident>& out) {
  out.clear();
  std::vector<Record> recs;
  if (store_.list(recs) != Store::Result::Ok) return false;
  for (const auto& r : recs) {
    cred::Resident x;
    if (!cred::decode(r.data, x)) continue;  // unreadable record: never matches, still listed for deletion
    x.recordId = r.id;
    out.push_back(std::move(x));
  }
  return true;
}

bool Authenticator::lookup(const uint8_t key[32], const uint8_t rpIdHash[32], const std::vector<uint8_t>& id,
                           const std::vector<cred::Resident>& res, Found& out) {
  uint8_t flags = 0;
  if (!cred::unwrap(crypto_, key, rpIdHash, id.data(), id.size(), out.priv.data(), flags)) return false;
  out.credId = id;
  out.resident = (flags & cred::kFlagResident) != 0;
  if (!out.resident) return true;
  // A deleted passkey must stop working even though its ID still decrypts.
  for (const auto& r : res) {
    if (r.credId == id) {
      out.userId = r.userId;
      out.userName = r.userName;
      out.displayName = r.displayName;
      out.created = r.created;
      return true;
    }
  }
  std::memset(out.priv.data(), 0, 32);
  return false;
}

bool Authenticator::sign(const uint8_t priv[32], const std::vector<uint8_t>& authData,
                         const uint8_t* clientDataHash, std::vector<uint8_t>& der) {
  std::vector<uint8_t> msg(authData);
  msg.insert(msg.end(), clientDataHash, clientDataHash + 32);
  uint8_t sig[64];
  if (!crypto_.p256Sign(priv, msg.data(), msg.size(), sig)) return false;
  der = derSignature(sig);
  return true;
}

uint8_t Authenticator::makeCredential(const uint8_t* p, size_t n, User& user, std::vector<uint8_t>& out) {
  Value req;
  if (uint8_t s = parseParams(p, n, req); s != kOk) return s;
  const Value *cdh = req.find(1), *rp = req.find(2), *usr = req.find(3), *algs = req.find(4),
              *exclude = req.find(5), *ext = req.find(6), *options = req.find(7), *pinAuth = req.find(8);
  if (!cdh || !rp || !usr || !algs) return kMissingParameter;
  if (cdh->type != T::Bytes || rp->type != T::Map || usr->type != T::Map || algs->type != T::Array)
    return kCborUnexpectedType;
  if (cdh->str.size() != 32) return kInvalidLength;
  if (ext && ext->type != T::Map) return kCborUnexpectedType;
  if (options && options->type != T::Map) return kCborUnexpectedType;
  if (pinAuth && pinAuth->type != T::Bytes) return kCborUnexpectedType;

  const Value* rpId = rp->find("id");
  if (!rpId) return kMissingParameter;
  if (rpId->type != T::Text) return kCborUnexpectedType;
  if (rpId->str.empty() || rpId->str.size() > cred::kMaxRpId) return kInvalidLength;
  const Value* uid = usr->find("id");
  if (!uid) return kMissingParameter;
  if (uid->type != T::Bytes) return kCborUnexpectedType;
  if (uid->str.empty() || uid->str.size() > cred::kMaxUserId) return kInvalidLength;
  std::string userName, displayName;
  for (auto [key, dst] : {std::pair{"name", &userName}, std::pair{"displayName", &displayName}}) {
    const Value* v = usr->find(key);
    if (!v) continue;
    if (v->type != T::Text) return kCborUnexpectedType;
    *dst = cred::truncateUtf8(v->text(), cred::kMaxName);
  }

  bool es256 = false;
  for (const Value& a : algs->items) {
    if (a.type != T::Map) return kCborUnexpectedType;
    const Value *alg = a.find("alg"), *type = a.find("type");
    if (!alg || !type || !alg->isInt() || type->type != T::Text) return kCborUnexpectedType;
    int64_t x;
    if (type->text() == "public-key" && alg->asInt(x) && x == kEs256) es256 = true;
  }
  if (!es256) return kUnsupportedAlgorithm;

  bool rk = false, up = true, uv = false;
  if (uint8_t s = readOption(options, "rk", rk); s != kOk) return s;
  if (uint8_t s = readOption(options, "up", up); s != kOk) return s;
  if (uint8_t s = readOption(options, "uv", uv); s != kOk) return s;
  if (!up) return kInvalidOption;  // creating a credential always needs presence

  std::vector<std::vector<uint8_t>> excludes;
  if (uint8_t s = readCredList(exclude, excludes); s != kOk) return s;

  // A zero-length pinAuth asks "touch to pick this key"; without ClientPIN the
  // answer after the touch is PIN_NOT_SET. Any other pinAuth cannot be checked.
  if (pinAuth) {
    if (!pinAuth->str.empty()) return kPinNotSet;
    const uint8_t s = fromAnswer(user.waitPresence());
    return s == kOk ? uint8_t{kPinNotSet} : s;
  }

  if (uint8_t s = fromUnlock(user.waitUnlocked()); s != kOk) return s;
  uint8_t rpIdHash[32];
  if (!crypto_.sha256(rpId->str.data(), rpId->str.size(), rpIdHash)) return kOther;
  uint8_t key[32];
  if (store_.wrapKey(key) != Store::Result::Ok) return kOther;
  std::vector<cred::Resident> res;
  if (!residents(res)) {
    std::memset(key, 0, sizeof key);
    return kOther;
  }

  for (const auto& id : excludes) {
    Found f;
    if (lookup(key, rpIdHash, id, res, f)) {
      std::memset(f.priv.data(), 0, 32);
      std::memset(key, 0, sizeof key);
      // The spec asks for presence first, so a site cannot silently probe.
      const uint8_t s = fromAnswer(user.waitPresence());
      return s == kOk ? uint8_t{kCredentialExcluded} : s;
    }
  }

  // An existing passkey of the same account is replaced; otherwise we need room.
  const cred::Resident* same = nullptr;
  for (const auto& r : res)
    if (std::memcmp(r.rpIdHash, rpIdHash, 32) == 0 && r.userId == uid->str) same = &r;
  if (rk && !same && res.size() >= cred::kMaxResident) {
    std::memset(key, 0, sizeof key);
    return kKeyStoreFull;
  }

  if (uint8_t s = fromAnswer(user.waitPresence()); s != kOk) {
    std::memset(key, 0, sizeof key);
    return s;
  }

  uint8_t priv[32], pub[65], credId[cred::kIdLen];
  bool ok = crypto_.p256Generate(priv, pub) &&
            cred::wrap(crypto_, key, rpIdHash, priv, rk ? cred::kFlagResident : 0, credId);
  std::memset(key, 0, sizeof key);
  if (!ok) {
    std::memset(priv, 0, sizeof priv);
    return kOther;
  }

  if (rk) {
    cred::Resident r;
    r.rpId = rpId->text();
    std::memcpy(r.rpIdHash, rpIdHash, 32);
    r.userId = uid->str;
    r.userName = userName;
    r.displayName = displayName;
    r.created = user.unixTime();
    r.credId.assign(credId, credId + sizeof credId);
    uint32_t id = same ? same->recordId : 0;
    const Store::Result sr = store_.put(id, cred::encode(r));
    if (sr != Store::Result::Ok) {
      std::memset(priv, 0, sizeof priv);
      return sr == Store::Result::Full ? kKeyStoreFull : kOther;
    }
  }

  uint32_t counter = 0;
  if (!counter_.next(counter)) {
    std::memset(priv, 0, sizeof priv);
    return kOther;
  }
  std::vector<uint8_t> authData(rpIdHash, rpIdHash + 32);
  authData.push_back(kFlagUp | kFlagUv | kFlagAt);
  put32(authData, counter);
  authData.insert(authData.end(), kAaguid.begin(), kAaguid.end());
  authData.push_back(0);
  authData.push_back(static_cast<uint8_t>(cred::kIdLen));
  authData.insert(authData.end(), credId, credId + cred::kIdLen);
  cbor::Writer cose;
  writeCose(cose, pub);
  authData.insert(authData.end(), cose.out.begin(), cose.out.end());

  // Self attestation: signed by the credential key itself (no certificate).
  std::vector<uint8_t> der;
  ok = sign(priv, authData, cdh->str.data(), der);
  std::memset(priv, 0, sizeof priv);
  if (!ok) return kOther;

  cbor::Writer w;
  w.map(3);
  w.uint(1), w.text("packed");
  w.uint(2), w.bytes(authData);
  w.uint(3);
  w.map(2);
  w.text("alg"), w.integer(kEs256);
  w.text("sig"), w.bytes(der);
  out = std::move(w.out);
  return kOk;
}

uint8_t Authenticator::assertion(const Found& f, const uint8_t rpIdHash[32], const uint8_t clientDataHash[32],
                                 uint8_t flags, bool includeUser, size_t count, std::vector<uint8_t>& out) {
  uint32_t counter = 0;
  if (!counter_.next(counter)) return kOther;
  std::vector<uint8_t> authData(rpIdHash, rpIdHash + 32);
  authData.push_back(flags);
  put32(authData, counter);
  std::vector<uint8_t> der;
  if (!sign(f.priv.data(), authData, clientDataHash, der)) return kOther;

  const bool user = includeUser && f.resident;
  cbor::Writer w;
  w.map(3 + (user ? 1 : 0) + (count > 1 ? 1 : 0));
  w.uint(1);
  w.map(2);
  w.text("id"), w.bytes(f.credId);
  w.text("type"), w.text("public-key");
  w.uint(2), w.bytes(authData);
  w.uint(3), w.bytes(der);
  if (user) {
    w.uint(4);
    // Names may be returned because user verification (unlocked vault) always holds.
    w.map(1 + (f.userName.empty() ? 0 : 1) + (f.displayName.empty() ? 0 : 1));
    w.text("id"), w.bytes(f.userId);
    if (!f.userName.empty()) w.text("name"), w.text(f.userName);
    if (!f.displayName.empty()) w.text("displayName"), w.text(f.displayName);
  }
  if (count > 1) w.uint(5), w.uint(count);
  out = std::move(w.out);
  return kOk;
}

uint8_t Authenticator::getAssertion(const uint8_t* p, size_t n, User& user, int64_t now, std::vector<uint8_t>& out) {
  Value req;
  if (uint8_t s = parseParams(p, n, req); s != kOk) return s;
  const Value *rpId = req.find(1), *cdh = req.find(2), *allow = req.find(3), *ext = req.find(4),
              *options = req.find(5), *pinAuth = req.find(6);
  if (!rpId || !cdh) return kMissingParameter;
  if (rpId->type != T::Text || cdh->type != T::Bytes) return kCborUnexpectedType;
  if (cdh->str.size() != 32) return kInvalidLength;
  if (ext && ext->type != T::Map) return kCborUnexpectedType;
  if (options && options->type != T::Map) return kCborUnexpectedType;
  if (pinAuth && pinAuth->type != T::Bytes) return kCborUnexpectedType;
  std::vector<std::vector<uint8_t>> allowed;
  if (uint8_t s = readCredList(allow, allowed); s != kOk) return s;
  bool up = true, uv = false;
  if (uint8_t s = readOption(options, "up", up); s != kOk) return s;
  if (uint8_t s = readOption(options, "uv", uv); s != kOk) return s;  // always met: see kFlagUv below
  if (options && options->find("rk")) return kUnsupportedOption;  // not a GetAssertion option

  if (pinAuth) {
    if (!pinAuth->str.empty()) return kPinNotSet;
    const uint8_t s = fromAnswer(user.waitPresence());
    return s == kOk ? uint8_t{kPinNotSet} : s;
  }

  if (uint8_t s = fromUnlock(user.waitUnlocked()); s != kOk) return s;
  uint8_t rpIdHash[32];
  if (!crypto_.sha256(rpId->str.data(), rpId->str.size(), rpIdHash)) return kOther;
  uint8_t key[32];
  if (store_.wrapKey(key) != Store::Result::Ok) return kOther;
  std::vector<cred::Resident> res;
  if (!residents(res)) {
    std::memset(key, 0, sizeof key);
    return kOther;
  }

  std::vector<Found> found;
  const bool discoverable = allow == nullptr || allowed.empty();
  if (!discoverable) {
    for (const auto& id : allowed) {
      Found f;
      if (lookup(key, rpIdHash, id, res, f)) {
        found.push_back(std::move(f));
        break;  // with an allow list the first match is used
      }
    }
  } else {
    std::vector<const cred::Resident*> mine;
    for (const auto& r : res)
      if (std::memcmp(r.rpIdHash, rpIdHash, 32) == 0) mine.push_back(&r);
    std::stable_sort(mine.begin(), mine.end(),
                     [](const cred::Resident* a, const cred::Resident* b) { return a->created > b->created; });
    for (const auto* r : mine) {
      Found f;
      if (lookup(key, rpIdHash, r->credId, res, f)) found.push_back(std::move(f));
    }
  }
  std::memset(key, 0, sizeof key);
  if (found.empty()) return kNoCredentials;

  uint8_t flags = kFlagUv;  // no signature is ever made while locked (docs/FIDO.md)
  if (up) {
    if (uint8_t s = fromAnswer(user.waitPresence()); s != kOk) {
      for (auto& f : found) std::memset(f.priv.data(), 0, 32);
      return s;
    }
    flags |= kFlagUp;
  }

  const size_t count = found.size();
  const uint8_t s = assertion(found[0], rpIdHash, cdh->str.data(), flags, discoverable || found[0].resident,
                              discoverable ? count : 1, out);
  std::memset(found[0].priv.data(), 0, 32);
  if (s == kOk && discoverable && count > 1) {
    next_.rest.assign(std::make_move_iterator(found.begin() + 1), std::make_move_iterator(found.end()));
    std::reverse(next_.rest.begin(), next_.rest.end());  // pop_back order = newest first
    std::memcpy(next_.rpIdHash.data(), rpIdHash, 32);
    std::memcpy(next_.clientDataHash.data(), cdh->str.data(), 32);
    next_.flags = flags;
    next_.until = now + kNextAssertionMs;
  } else {
    for (auto& f : found) std::memset(f.priv.data(), 0, 32);
  }
  return s;
}

uint8_t Authenticator::getNextAssertion(int64_t now, std::vector<uint8_t>& out) {
  if (next_.rest.empty() || now > next_.until) {
    next_ = Next{};
    return kNotAllowed;
  }
  Found f = std::move(next_.rest.back());
  next_.rest.pop_back();
  const uint8_t s = assertion(f, next_.rpIdHash.data(), next_.clientDataHash.data(), next_.flags, true, 1, out);
  std::memset(f.priv.data(), 0, 32);
  if (next_.rest.empty()) next_ = Next{};
  return s;
}

uint8_t Authenticator::reset(User& user) {
  if (user.uptimeMs() > kResetWindowMs) return kNotAllowed;
  if (uint8_t s = fromUnlock(user.waitUnlocked()); s != kOk) return s;
  if (uint8_t s = fromAnswer(user.waitPresence()); s != kOk) return s;
  return store_.reset() == Store::Result::Ok ? kOk : kOther;
}

uint8_t Authenticator::selection(User& user) { return fromAnswer(user.waitPresence()); }

// ---- CTAP1 / U2F ----------------------------------------------------------

std::vector<uint8_t> Authenticator::msg(const uint8_t* a, size_t n, User& user) {
  next_ = Next{};
  std::vector<uint8_t> out;
  uint16_t sw = u2f::kSwOther;
  auto finish = [&](uint16_t status) {
    if (status != u2f::kSwOk) out.clear();
    out.push_back(static_cast<uint8_t>(status >> 8));
    out.push_back(static_cast<uint8_t>(status));
    return out;
  };
  if (n < 4) return finish(u2f::kSwWrongLength);
  if (a[0] != 0) return finish(u2f::kSwCla);
  // Lc: none, short (1 byte) or extended (0x00 + 2 bytes); an optional Le follows.
  size_t lc = 0, off = 4;
  if (n == 4 || n == 5) {
    lc = 0;
  } else if (a[4] == 0 && n >= 7) {
    lc = size_t(a[5]) << 8 | a[6];
    off = 7;
    if (n == 7) lc = 0;  // extended Le only
    else if (n != off + lc && n != off + lc + 2) return finish(u2f::kSwWrongLength);
  } else {
    lc = a[4];
    off = 5;
    if (n != off + lc && n != off + lc + 1) return finish(u2f::kSwWrongLength);
  }
  const uint8_t* body = a + off;
  switch (a[1]) {
    case 0x01:  // REGISTER
      sw = lc == 64 ? u2fRegister(body, user, out) : u2f::kSwWrongLength;
      break;
    case 0x02:  // AUTHENTICATE
      sw = lc >= 65 && lc == 65 + size_t(body[64]) ? u2fAuthenticate(a[2], body, lc, user, out)
                                                    : u2f::kSwWrongLength;
      break;
    case 0x03:  // VERSION
      if (lc != 0) {
        sw = u2f::kSwWrongLength;
      } else {
        out.assign({'U', '2', 'F', '_', 'V', '2'});
        sw = u2f::kSwOk;
      }
      break;
    default: sw = u2f::kSwIns; break;
  }
  return finish(sw);
}

uint16_t Authenticator::u2fRegister(const uint8_t* body, User& user, std::vector<uint8_t>& out) {
  const uint8_t* challenge = body;
  const uint8_t* app = body + 32;
  // U2F hosts poll: "conditions not satisfied" until the vault is unlocked and pressed.
  if (!store_.unlocked() || !user.takePresence()) return u2f::kSwConditions;
  uint8_t key[32];
  if (store_.wrapKey(key) != Store::Result::Ok) return u2f::kSwOther;
  uint8_t priv[32], pub[65], kh[cred::kIdLen];
  bool ok = crypto_.p256Generate(priv, pub) && cred::wrap(crypto_, key, app, priv, 0, kh);
  std::memset(key, 0, sizeof key);
  std::memset(priv, 0, sizeof priv);
  if (!ok) return u2f::kSwOther;

  std::vector<uint8_t> signed_{0x00};
  signed_.insert(signed_.end(), app, app + 32);
  signed_.insert(signed_.end(), challenge, challenge + 32);
  signed_.insert(signed_.end(), kh, kh + sizeof kh);
  signed_.insert(signed_.end(), pub, pub + 65);
  uint8_t attKey[32], sig[64];
  std::vector<uint8_t> cert;
  ok = attest::get(crypto_, attestation_, attKey, cert) &&
       crypto_.p256Sign(attKey, signed_.data(), signed_.size(), sig);
  std::memset(attKey, 0, sizeof attKey);
  if (!ok) return u2f::kSwOther;
  const auto der = derSignature(sig);

  out.push_back(0x05);
  out.insert(out.end(), pub, pub + 65);
  out.push_back(static_cast<uint8_t>(sizeof kh));
  out.insert(out.end(), kh, kh + sizeof kh);
  out.insert(out.end(), cert.begin(), cert.end());
  out.insert(out.end(), der.begin(), der.end());
  return u2f::kSwOk;
}

uint16_t Authenticator::u2fAuthenticate(uint8_t p1, const uint8_t* body, size_t lc, User& user,
                                        std::vector<uint8_t>& out) {
  const uint8_t* challenge = body;
  const uint8_t* app = body + 32;
  const std::vector<uint8_t> kh(body + 65, body + lc);
  if (p1 != 0x03 && p1 != 0x07 && p1 != 0x08) return u2f::kSwWrongData;
  if (!store_.unlocked()) {
    // Cannot tell whether the handle is ours. "Unknown" for check-only keeps a
    // registration from being refused as a duplicate; otherwise ask to retry.
    if (p1 == 0x07) return u2f::kSwWrongData;
    user.takePresence();  // show the FIDO prompt so the person unlocks Keyra
    return u2f::kSwConditions;
  }
  uint8_t key[32];
  if (store_.wrapKey(key) != Store::Result::Ok) return u2f::kSwOther;
  std::vector<cred::Resident> res;
  const bool listed = residents(res);
  Found f;
  const bool mine = listed && lookup(key, app, kh, res, f);
  std::memset(key, 0, sizeof key);
  if (!listed) return u2f::kSwOther;
  if (!mine) return u2f::kSwWrongData;
  if (p1 == 0x07) {
    std::memset(f.priv.data(), 0, 32);
    return u2f::kSwConditions;  // check-only: "this handle is ours"
  }
  uint8_t flags = 0;
  if (p1 == 0x03) {
    if (!user.takePresence()) {
      std::memset(f.priv.data(), 0, 32);
      return u2f::kSwConditions;
    }
    flags = kFlagUp;
  }
  uint32_t counter = 0;
  if (!counter_.next(counter)) {
    std::memset(f.priv.data(), 0, 32);
    return u2f::kSwOther;
  }
  std::vector<uint8_t> signed_(app, app + 32);
  signed_.push_back(flags);
  put32(signed_, counter);
  signed_.insert(signed_.end(), challenge, challenge + 32);
  uint8_t sig[64];
  const bool ok = crypto_.p256Sign(f.priv.data(), signed_.data(), signed_.size(), sig);
  std::memset(f.priv.data(), 0, 32);
  if (!ok) return u2f::kSwOther;
  const auto der = derSignature(sig);
  out.push_back(flags);
  put32(out, counter);
  out.insert(out.end(), der.begin(), der.end());
  return u2f::kSwOk;
}

}  // namespace keyra::fido
