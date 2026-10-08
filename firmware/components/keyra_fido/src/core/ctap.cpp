// CTAP 2.1 §6 (authenticator API) and FIDO U2F raw message formats. Behaviour
// was checked against the specifications and, for edge cases, against how
// LionKey (MIT) and Solo 1 (Apache-2.0/MIT) answer; no code was taken from them.
#include "ctap.hpp"

#include <algorithm>
#include <cstring>

#include "cbor.hpp"
#include "ctap_common.hpp"

namespace keyra::fido {
namespace {

void wipe(void* p, size_t n) { secureWipe(p, n); }

using cbor::Value;
using T = cbor::Value::Type;
using namespace ctap;

constexpr uint8_t kFlagUp = 0x01, kFlagUv = 0x04, kFlagAt = 0x40, kFlagEd = 0x80;
constexpr int64_t kEs256 = -7;

void put32(std::vector<uint8_t>& v, uint32_t x) {
  for (int s = 24; s >= 0; s -= 8) v.push_back(static_cast<uint8_t>(x >> s));
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

std::vector<uint8_t> Authenticator::cbor(const uint8_t* req, size_t n, User& user, int64_t now, uint32_t cid) {
  std::vector<uint8_t> body;
  uint8_t status = kInvalidLength;
  if (n >= 1) {
    const uint8_t cmd = req[0];
    // Only valid right after GetAssertion, and only on the channel that asked.
    if (cmd != kGetNextAssertion || cid != next_.cid) next_ = Next{};
    switch (cmd) {
      case kMakeCredential: status = makeCredential(req + 1, n - 1, user, body); break;
      case kGetAssertion:
        status = getAssertion(req + 1, n - 1, user, now, body);
        if (!next_.rest.empty()) next_.cid = cid;
        break;
      case kGetNextAssertion: status = getNextAssertion(now, body); break;
      case kGetInfo: status = getInfo(body); break;
      case kClientPin: status = clientPin(req + 1, n - 1, user, body); break;
      case kReset: status = reset(user); break;
      case kSelection: status = selection(user); break;
      default: status = kInvalidCommand; break;  // 2.1 management commands are not offered
    }
  }
  if (status != kOk) body.clear();
  body.insert(body.begin(), status);
  return body;
}

uint8_t Authenticator::getInfo(std::vector<uint8_t>& out) {
  // Read while locked too: whether a PIN exists is not secret (the file exists).
  const bool pinSet = store_.pinSet();
  cbor::Writer w;
  w.map(8);
  w.uint(1);
  // Still 2.0: FIDO_2_1 would make pinUvAuthToken permissions, credential
  // management and more mandatory (docs/FIDO.md).
  w.array(2), w.text("U2F_V2"), w.text("FIDO_2_0");
  w.uint(2);
  w.array(2), w.text("credProtect"), w.text("hmac-secret");
  w.uint(3), w.bytes(kAaguid.data(), kAaguid.size());
  w.uint(4);
  w.map(pinSet ? 4 : 5);  // canonical order: shorter keys first, then bytewise
  w.text("rk"), w.boolean(true);
  w.text("up"), w.boolean(true);
  // Without a PIN, "verified" = the vault is unlocked (docs/FIDO.md). With one,
  // only the PIN verifies; a platform that saw uv would skip asking for it.
  if (!pinSet) w.text("uv"), w.boolean(true);
  w.text("plat"), w.boolean(false);
  w.text("clientPin"), w.boolean(pinSet);
  w.uint(5), w.uint(1200);  // maxMsgSize: bounds what we parse; hosts split long allow lists
  w.uint(6);
  w.array(2), w.uint(2), w.uint(1);  // PIN/UV auth protocols, preferred first
  w.uint(7), w.uint(8);              // maxCredentialCountInList: 8 descriptors fit in maxMsgSize
  w.uint(8), w.uint(64);             // maxCredentialIdLength (ours are 62 bytes)
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

bool Authenticator::lookup(const WrapKeys& keys, const uint8_t rpIdHash[32], const std::vector<uint8_t>& id,
                           const std::vector<cred::Resident>& res, Found& out) {
  uint8_t flags = 0;
  bool opened = false;
  for (size_t i = 0; i < keys.count && !opened; ++i) {
    opened = cred::unwrap(crypto_, keys.key[i], rpIdHash, id.data(), id.size(), out.priv.data(), flags);
    if (opened) out.keyIndex = i;
  }
  if (!opened) return false;
  out.credId = id;
  out.flags = flags;
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
  wipe(out.priv.data(), 32);
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
              *exclude = req.find(5), *ext = req.find(6), *options = req.find(7), *pinAuth = req.find(8),
              *pinProtocol = req.find(9);
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

  // Extensions: what the credential will remember lives in its ID's flags byte.
  bool hmacSecret = false;
  int64_t protect = 0;  // credProtect level, 0 = not asked
  if (ext) {
    if (const Value* h = ext->find("hmac-secret")) {
      if (h->type != T::Bool) return kCborUnexpectedType;
      hmacSecret = h->b;
    }
    if (const Value* cp = ext->find("credProtect")) {
      if (!cp->isInt()) return kCborUnexpectedType;
      if (!cp->asInt(protect) || protect < 1 || protect > 3) return kInvalidOption;
    }
  }

  bool verified = false;
  if (uint8_t s = userVerification(pinAuth, pinProtocol, cdh->str.data(), uv, true, user, verified); s != kOk)
    return s;

  if (uint8_t s = fromUnlock(user.waitUnlocked()); s != kOk) return s;
  uint8_t rpIdHash[32];
  if (!crypto_.sha256(rpId->str.data(), rpId->str.size(), rpIdHash)) return kOther;
  WrapKeys keys;
  if (store_.wrapKeys(keys) != Store::Result::Ok) return kOther;
  std::vector<cred::Resident> res;
  if (!residents(res)) return kOther;

  for (const auto& id : excludes) {
    Found f;
    if (lookup(keys, rpIdHash, id, res, f)) {
      wipe(f.priv.data(), 32);
      keys.clear();
      // The spec asks for presence first, so a site cannot silently probe.
      const uint8_t s = fromAnswer(user.waitPresence());
      return s == kOk ? uint8_t{kCredentialExcluded} : s;
    }
  }

  // An existing passkey of the same account is replaced; otherwise we need room.
  const cred::Resident* same = nullptr;
  for (const auto& r : res)
    if (std::memcmp(r.rpIdHash, rpIdHash, 32) == 0 && r.userId == uid->str) same = &r;
  if (rk && !same && res.size() >= cred::kMaxResident) return kKeyStoreFull;

  // No key across the wait for the button; a vault locked meanwhile refuses.
  keys.clear();
  if (uint8_t s = fromAnswer(user.waitPresence()); s != kOk) return s;
  if (!store_.unlocked() || store_.wrapKeys(keys) != Store::Result::Ok) return kOperationDenied;

  uint8_t idFlags = (rk ? cred::kFlagResident : 0) | (hmacSecret ? cred::kFlagHmacSecret : 0);
  if (protect) idFlags = cred::withProtect(idFlags, static_cast<uint8_t>(protect));
  uint8_t priv[32], pub[65], credId[cred::kIdLen];
  bool ok = crypto_.p256Generate(priv, pub) && cred::wrap(crypto_, keys.key[0], rpIdHash, priv, idFlags, credId);
  keys.clear();
  if (!ok) {
    wipe(priv, sizeof priv);
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
      wipe(priv, sizeof priv);
      return sr == Store::Result::Full ? kKeyStoreFull : kOther;
    }
  }

  uint32_t counter = 0;
  if (!counter_.next(counter)) {
    wipe(priv, sizeof priv);
    return kOther;
  }
  const bool extOut = hmacSecret || protect;
  std::vector<uint8_t> authData(rpIdHash, rpIdHash + 32);
  authData.push_back(kFlagUp | (verified ? kFlagUv : 0) | kFlagAt | (extOut ? kFlagEd : 0));
  put32(authData, counter);
  authData.insert(authData.end(), kAaguid.begin(), kAaguid.end());
  authData.push_back(0);
  authData.push_back(static_cast<uint8_t>(cred::kIdLen));
  authData.insert(authData.end(), credId, credId + cred::kIdLen);
  cbor::Writer cose;
  writeCose(cose, pub);
  if (extOut) {
    cose.map((protect ? 1 : 0) + (hmacSecret ? 1 : 0));  // same length: bytewise, so credProtect first
    if (protect) cose.text("credProtect"), cose.uint(static_cast<uint64_t>(protect));
    if (hmacSecret) cose.text("hmac-secret"), cose.boolean(true);
  }
  authData.insert(authData.end(), cose.out.begin(), cose.out.end());

  // Self attestation: signed by the credential key itself (no certificate).
  std::vector<uint8_t> der;
  ok = sign(priv, authData, cdh->str.data(), der);
  wipe(priv, sizeof priv);
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
                                 uint8_t flags, bool includeUser, size_t count, const HmacSecret& hmac,
                                 std::vector<uint8_t>& out) {
  // hmac-secret answers only for credentials made with it (CTAP 2.1 §12.5).
  std::vector<uint8_t> secret;
  if (hmac.present && (f.flags & cred::kFlagHmacSecret) && !hmacSecretOutput(hmac, f, secret)) return kOther;
  uint32_t counter = 0;
  if (!counter_.next(counter)) return kOther;
  std::vector<uint8_t> authData(rpIdHash, rpIdHash + 32);
  authData.push_back(secret.empty() ? flags : flags | kFlagEd);
  put32(authData, counter);
  if (!secret.empty()) {
    cbor::Writer e;
    e.map(1);
    e.text("hmac-secret"), e.bytes(secret);
    authData.insert(authData.end(), e.out.begin(), e.out.end());
  }
  std::vector<uint8_t> der;
  if (!sign(f.priv.data(), authData, clientDataHash, der)) return kOther;

  const bool user = includeUser && f.resident;
  // CTAP 2.0 §5.2: names only after user verification; the id is always given.
  const bool names = (flags & kFlagUv) != 0;
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
    const bool name = names && !f.userName.empty(), display = names && !f.displayName.empty();
    w.map(1 + (name ? 1 : 0) + (display ? 1 : 0));
    w.text("id"), w.bytes(f.userId);
    if (name) w.text("name"), w.text(f.userName);
    if (display) w.text("displayName"), w.text(f.displayName);
  }
  if (count > 1) w.uint(5), w.uint(count);
  out = std::move(w.out);
  return kOk;
}

uint8_t Authenticator::getAssertion(const uint8_t* p, size_t n, User& user, int64_t now, std::vector<uint8_t>& out) {
  Value req;
  if (uint8_t s = parseParams(p, n, req); s != kOk) return s;
  const Value *rpId = req.find(1), *cdh = req.find(2), *allow = req.find(3), *ext = req.find(4),
              *options = req.find(5), *pinAuth = req.find(6), *pinProtocol = req.find(7);
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
  if (uint8_t s = readOption(options, "uv", uv); s != kOk) return s;
  if (options && options->find("rk")) return kUnsupportedOption;  // not a GetAssertion option

  bool verified = false;
  if (uint8_t s = userVerification(pinAuth, pinProtocol, cdh->str.data(), uv, false, user, verified); s != kOk)
    return s;
  // Checked and decrypted before the touch, so a bad request fails at once.
  HmacSecret hmac;
  if (ext)
    if (uint8_t s = readHmacSecret(ext->find("hmac-secret"), hmac); s != kOk) return s;

  if (uint8_t s = fromUnlock(user.waitUnlocked()); s != kOk) return s;
  uint8_t rpIdHash[32];
  if (!crypto_.sha256(rpId->str.data(), rpId->str.size(), rpIdHash)) return kOther;
  WrapKeys keys;
  if (store_.wrapKeys(keys) != Store::Result::Ok) return kOther;
  std::vector<cred::Resident> res;
  if (!residents(res)) return kOther;

  std::vector<Found> found;
  const bool discoverable = allow == nullptr || allowed.empty();
  // credProtect (CTAP 2.1 §12.1): level 3 needs user verification always,
  // level 2 unless the site named the credential. Others are as if absent.
  auto protectedAway = [&](const Found& f) {
    const uint8_t level = cred::protectLevel(f.flags);
    return !verified && (level == 3 || (level == 2 && discoverable));
  };
  if (!discoverable) {
    for (const auto& id : allowed) {
      Found f;
      if (lookup(keys, rpIdHash, id, res, f)) {
        if (protectedAway(f)) {
          wipe(f.priv.data(), 32);
          continue;
        }
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
      if (!lookup(keys, rpIdHash, r->credId, res, f)) continue;
      if (protectedAway(f)) {
        wipe(f.priv.data(), 32);
        continue;
      }
      found.push_back(std::move(f));
    }
  }
  keys.clear();
  // Only which credentials matched is kept; their keys come back in rearm().
  for (auto& f : found) wipe(f.priv.data(), 32);
  if (found.empty()) return kNoCredentials;

  uint8_t flags = verified ? kFlagUv : 0;
  if (up) {
    if (uint8_t s = fromAnswer(user.waitPresence()); s != kOk) return s;
    flags |= kFlagUp;
  }

  const size_t count = found.size();
  if (uint8_t s = rearm(rpIdHash, found[0], hmac.present, verified); s != kOk) return s;
  const uint8_t s = assertion(found[0], rpIdHash, cdh->str.data(), flags, discoverable || found[0].resident,
                              discoverable ? count : 1, hmac, out);
  wipe(found[0].priv.data(), 32);
  wipe(found[0].credRandom.data(), 32);
  if (s == kOk && discoverable && count > 1) {
    next_.rest.assign(std::make_move_iterator(found.begin() + 1), std::make_move_iterator(found.end()));
    std::reverse(next_.rest.begin(), next_.rest.end());  // pop_back order = newest first
    std::memcpy(next_.rpIdHash.data(), rpIdHash, 32);
    std::memcpy(next_.clientDataHash.data(), cdh->str.data(), 32);
    next_.flags = flags;
    next_.until = now + kNextAssertionMs;
    next_.hmac = hmac;
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
  // Locked (or the passkey deleted) since GetAssertion: stop the sequence.
  if (uint8_t s = rearm(next_.rpIdHash.data(), f, next_.hmac.present, next_.flags & kFlagUv); s != kOk) {
    next_ = Next{};
    return s;
  }
  const uint8_t s =
      assertion(f, next_.rpIdHash.data(), next_.clientDataHash.data(), next_.flags, true, 1, next_.hmac, out);
  wipe(f.priv.data(), 32);
  wipe(f.credRandom.data(), 32);
  if (next_.rest.empty()) next_ = Next{};
  return s;
}

uint8_t Authenticator::rearm(const uint8_t rpIdHash[32], Found& f, bool hmacSecret, bool uv) {
  if (!store_.unlocked()) return kOperationDenied;
  WrapKeys keys;
  if (store_.wrapKeys(keys) != Store::Result::Ok) return kOperationDenied;
  std::vector<cred::Resident> res;
  const std::vector<uint8_t> id = f.credId;
  bool ok = residents(res) && lookup(keys, rpIdHash, id, res, f);
  if (ok && hmacSecret && (f.flags & cred::kFlagHmacSecret)) {
    // CredRandom is derived, not stored: HMAC(wrap key, label || uv || credential ID).
    // The same wrapping key comes back with a restored backup, so the secrets do too.
    static constexpr char kLabel[] = "keyra/fido/v1/hmac-secret";
    std::vector<uint8_t> msg(kLabel, kLabel + sizeof kLabel - 1);
    msg.push_back(uv ? 1 : 0);
    msg.insert(msg.end(), id.begin(), id.end());
    ok = crypto_.hmacSha256(keys.key[f.keyIndex], 32, msg.data(), msg.size(), f.credRandom.data());
    if (!ok) wipe(f.priv.data(), 32);
  }
  keys.clear();
  return ok ? kOk : kNoCredentials;
}

uint8_t Authenticator::reset(User& user) {
  if (user.uptimeMs() > kResetWindowMs) return kNotAllowed;
  if (uint8_t s = fromUnlock(user.waitUnlocked()); s != kOk) return s;
  if (uint8_t s = fromAnswer(user.waitPresence()); s != kOk) return s;
  // The PIN goes with the credentials (CTAP 2.0 §5.6); so do the token and the counts.
  token_ = Token{};
  consecutiveWrong_ = 0;
  ka_ = KeyAgreement{};
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
  WrapKeys keys;
  if (store_.wrapKeys(keys) != Store::Result::Ok) return u2f::kSwOther;
  uint8_t priv[32], pub[65], kh[cred::kIdLen];
  bool ok = crypto_.p256Generate(priv, pub) && cred::wrap(crypto_, keys.key[0], app, priv, 0, kh);
  keys.clear();
  wipe(priv, sizeof priv);
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
  wipe(attKey, sizeof attKey);
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
  WrapKeys keys;
  if (store_.wrapKeys(keys) != Store::Result::Ok) return u2f::kSwOther;
  std::vector<cred::Resident> res;
  const bool listed = residents(res);
  Found f;
  const bool mine = listed && lookup(keys, app, kh, res, f);
  keys.clear();
  if (!listed) return u2f::kSwOther;
  // U2F never verifies the user: a credProtect level 3 credential is not offered there.
  if (mine && cred::protectLevel(f.flags) == 3) {
    wipe(f.priv.data(), 32);
    return u2f::kSwWrongData;
  }
  if (!mine) return u2f::kSwWrongData;
  if (p1 == 0x07) {
    wipe(f.priv.data(), 32);
    return u2f::kSwConditions;  // check-only: "this handle is ours"
  }
  uint8_t flags = 0;
  if (p1 == 0x03) {
    if (!user.takePresence()) {
      wipe(f.priv.data(), 32);
      return u2f::kSwConditions;
    }
    flags = kFlagUp;
  }
  // As in CTAP2: nothing is signed once the vault has locked (docs/FIDO.md).
  if (!store_.unlocked()) {
    wipe(f.priv.data(), 32);
    return u2f::kSwConditions;
  }
  uint32_t counter = 0;
  if (!counter_.next(counter)) {
    wipe(f.priv.data(), 32);
    return u2f::kSwOther;
  }
  std::vector<uint8_t> signed_(app, app + 32);
  signed_.push_back(flags);
  put32(signed_, counter);
  signed_.insert(signed_.end(), challenge, challenge + 32);
  uint8_t sig[64];
  const bool ok = crypto_.p256Sign(f.priv.data(), signed_.data(), signed_.size(), sig);
  wipe(f.priv.data(), 32);
  if (!ok) return u2f::kSwOther;
  const auto der = derSignature(sig);
  out.push_back(flags);
  put32(out, counter);
  out.insert(out.end(), der.begin(), der.end());
  return u2f::kSwOk;
}

}  // namespace keyra::fido
