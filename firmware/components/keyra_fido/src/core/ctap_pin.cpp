// authenticatorClientPIN (CTAP 2.0 §5.5, protocols 1 and 2 from CTAP 2.1
// §6.5), PIN checks on MakeCredential/GetAssertion and the hmac-secret
// extension's key exchange. docs/FIDO.md "ClientPIN" describes the choices.
#include <cstring>

#include "ctap.hpp"
#include "ctap_common.hpp"

namespace keyra::fido {
namespace {

using cbor::Value;
using T = cbor::Value::Type;
using namespace ctap;

constexpr int64_t kEcdhEsHkdf256 = -25;  // COSE alg the key agreement key is labelled with

bool bytesOrAbsent(const Value* v) { return !v || v->type == T::Bytes; }

// Encrypted PIN block sizes: 64 bytes, plus the IV under protocol 2.
size_t paddedPinEnc(int protocol) { return protocol == 2 ? 16 + pin::kPaddedPin : pin::kPaddedPin; }
size_t pinHashEnc(int protocol) { return protocol == 2 ? 32 : 16; }

}  // namespace

bool Authenticator::keyAgreement() {
  if (!ka_.ready) ka_.ready = crypto_.p256Generate(ka_.priv.data(), ka_.pub.data());
  return ka_.ready;
}

uint8_t Authenticator::decapsulate(const Value* cose, int protocol, pin::Shared& out) {
  if (!cose) return kMissingParameter;
  if (cose->type != T::Map) return kCborUnexpectedType;
  int64_t kty = 0, crv = 0;
  const Value *x = cose->find(-2), *y = cose->find(-3);
  if (!cose->find(1) || !cose->find(1)->asInt(kty) || kty != 2 || !cose->find(-1) || !cose->find(-1)->asInt(crv) ||
      crv != 1 || !x || x->type != T::Bytes || x->str.size() != 32 || !y || y->type != T::Bytes ||
      y->str.size() != 32)
    return kInvalidParameter;
  if (!keyAgreement()) return kOther;
  uint8_t peer[65];
  peer[0] = 0x04;
  std::memcpy(peer + 1, x->str.data(), 32);
  std::memcpy(peer + 33, y->str.data(), 32);
  // A point off the curve fails here too (the platform's key is not trusted).
  return pin::sharedSecret(crypto_, protocol, ka_.priv.data(), peer, out) ? uint8_t{kOk} : uint8_t{kInvalidParameter};
}

uint8_t Authenticator::readPin(pin::State& out) {
  std::vector<uint8_t> data;
  switch (store_.pinRead(data)) {
    case Store::Result::Ok: break;
    case Store::Result::NotFound: return kPinNotSet;
    case Store::Result::Locked: return kOperationDenied;
    default: return kOther;
  }
  return pin::decode(data, out) ? uint8_t{kOk} : uint8_t{kOther};
}

uint8_t Authenticator::checkPinHash(const pin::Shared& s, const Value* enc, pin::State& st) {
  // Counted before the comparison and stored first: pulling the plug mid-check
  // cannot give a free guess.
  --st.retries;
  if (store_.pinWrite(pin::encode(st)) != Store::Result::Ok) return kOther;
  std::vector<uint8_t> hash;
  if (!pin::decrypt(crypto_, s, enc->str.data(), enc->str.size(), hash)) return kOther;
  const bool match = hash.size() >= 16 && pin::equal(hash.data(), st.hash, 16);
  secureWipe(hash.data(), hash.size());
  if (!match) {
    ka_ = KeyAgreement{};  // a new key: the platform must start the exchange again
    ++consecutiveWrong_;
    if (st.retries == 0) return kPinBlocked;
    return consecutiveWrong_ >= pin::kMaxConsecutive ? uint8_t{kPinAuthBlocked} : uint8_t{kPinInvalid};
  }
  consecutiveWrong_ = 0;
  st.retries = pin::kMaxRetries;
  return store_.pinWrite(pin::encode(st)) == Store::Result::Ok ? uint8_t{kOk} : uint8_t{kOther};
}

uint8_t Authenticator::clientPin(const uint8_t* p, size_t n, User& user, std::vector<uint8_t>& out) {
  Value req;
  if (uint8_t s = parseParams(p, n, req); s != kOk) return s;
  const Value *proto = req.find(1), *sub = req.find(2), *platformKey = req.find(3), *param = req.find(4),
              *newPinEnc = req.find(5), *hashEnc = req.find(6);
  if (!sub) return kMissingParameter;
  if (sub->type != T::Uint || (proto && proto->type != T::Uint)) return kCborUnexpectedType;
  if (!bytesOrAbsent(param) || !bytesOrAbsent(newPinEnc) || !bytesOrAbsent(hashEnc)) return kCborUnexpectedType;
  int protocol = 0;
  if (proto) {
    if (proto->u != 1 && proto->u != 2) return kInvalidParameter;
    protocol = static_cast<int>(proto->u);
  }

  cbor::Writer w;
  switch (sub->u) {
    case kGetPinRetries: {
      pin::State st;  // no PIN: the full count
      if (store_.pinSet()) {
        if (uint8_t s = fromUnlock(user.waitUnlocked()); s != kOk) return s;
        if (uint8_t s = readPin(st); s != kOk && s != kPinNotSet) return s;
      }
      w.map(1);
      w.uint(3), w.uint(st.retries);
      break;
    }

    case kGetKeyAgreement: {
      if (!protocol) return kMissingParameter;
      if (!keyAgreement()) return kOther;
      w.map(1);
      w.uint(1);
      w.map(5);
      w.uint(1), w.uint(2);                    // kty: EC2
      w.uint(3), w.integer(kEcdhEsHkdf256);     // alg
      w.integer(-1), w.uint(1);                // crv: P-256
      w.integer(-2), w.bytes(ka_.pub.data() + 1, 32);
      w.integer(-3), w.bytes(ka_.pub.data() + 33, 32);
      break;
    }

    case kSetPin: {
      if (!protocol || !platformKey || !param || !newPinEnc) return kMissingParameter;
      if (store_.pinSet()) return kNotAllowed;  // changePIN is the way to replace one
      pin::Shared s;
      if (uint8_t r = decapsulate(platformKey, protocol, s); r != kOk) return r;
      if (!pin::verify(crypto_, protocol, s.hmacKey, 32, newPinEnc->str.data(), newPinEnc->str.size(), param->str))
        return kPinAuthInvalid;
      if (newPinEnc->str.size() != paddedPinEnc(protocol)) return kInvalidParameter;
      std::vector<uint8_t> padded, pinBytes;
      if (!pin::decrypt(crypto_, s, newPinEnc->str.data(), newPinEnc->str.size(), padded)) return kOther;
      const bool ok = pin::unpad(padded, pinBytes);
      secureWipe(padded.data(), padded.size());
      if (!ok) return kPinPolicyViolation;
      pin::State st;
      uint8_t digest[32];
      const bool hashed = crypto_.sha256(pinBytes.data(), pinBytes.size(), digest);
      secureWipe(pinBytes.data(), pinBytes.size());
      std::memcpy(st.hash, digest, 16);
      secureWipe(digest, sizeof digest);
      if (!hashed) return kOther;
      if (uint8_t r = fromUnlock(user.waitUnlocked()); r != kOk) return r;
      if (store_.pinSet()) return kNotAllowed;
      if (store_.pinWrite(pin::encode(st)) != Store::Result::Ok) return kOther;
      consecutiveWrong_ = 0;
      return kOk;
    }

    case kChangePin: {
      if (!protocol || !platformKey || !param || !newPinEnc || !hashEnc) return kMissingParameter;
      if (!store_.pinSet()) return kPinNotSet;
      if (consecutiveWrong_ >= pin::kMaxConsecutive) return kPinAuthBlocked;
      if (uint8_t r = fromUnlock(user.waitUnlocked()); r != kOk) return r;
      pin::State st;
      if (uint8_t r = readPin(st); r != kOk) return r;
      if (st.retries == 0) return kPinBlocked;
      pin::Shared s;
      if (uint8_t r = decapsulate(platformKey, protocol, s); r != kOk) return r;
      std::vector<uint8_t> msg = newPinEnc->str;
      msg.insert(msg.end(), hashEnc->str.begin(), hashEnc->str.end());
      if (!pin::verify(crypto_, protocol, s.hmacKey, 32, msg.data(), msg.size(), param->str)) return kPinAuthInvalid;
      if (newPinEnc->str.size() != paddedPinEnc(protocol) || hashEnc->str.size() != pinHashEnc(protocol))
        return kInvalidParameter;
      if (uint8_t r = checkPinHash(s, hashEnc, st); r != kOk) return r;
      std::vector<uint8_t> padded, pinBytes;
      if (!pin::decrypt(crypto_, s, newPinEnc->str.data(), newPinEnc->str.size(), padded)) return kOther;
      const bool ok = pin::unpad(padded, pinBytes);
      secureWipe(padded.data(), padded.size());
      if (!ok) return kPinPolicyViolation;
      uint8_t digest[32];
      const bool hashed = crypto_.sha256(pinBytes.data(), pinBytes.size(), digest);
      secureWipe(pinBytes.data(), pinBytes.size());
      std::memcpy(st.hash, digest, 16);
      secureWipe(digest, sizeof digest);
      if (!hashed || store_.pinWrite(pin::encode(st)) != Store::Result::Ok) return kOther;
      token_ = Token{};  // a token from the old PIN stops working
      return kOk;
    }

    case kGetPinToken: {
      if (!protocol || !platformKey || !hashEnc) return kMissingParameter;
      if (!store_.pinSet()) return kPinNotSet;
      if (consecutiveWrong_ >= pin::kMaxConsecutive) return kPinAuthBlocked;
      if (uint8_t r = fromUnlock(user.waitUnlocked()); r != kOk) return r;
      pin::State st;
      if (uint8_t r = readPin(st); r != kOk) return r;
      if (st.retries == 0) return kPinBlocked;
      pin::Shared s;
      if (uint8_t r = decapsulate(platformKey, protocol, s); r != kOk) return r;
      if (hashEnc->str.size() != pinHashEnc(protocol)) return kInvalidParameter;
      if (uint8_t r = checkPinHash(s, hashEnc, st); r != kOk) return r;
      // A new token each time: one handed out earlier stops working.
      token_ = Token{};
      if (!crypto_.random(token_.key.data(), token_.key.size())) return kOther;
      std::vector<uint8_t> enc;
      if (!pin::encrypt(crypto_, s, token_.key.data(), token_.key.size(), enc)) {
        token_ = Token{};
        return kOther;
      }
      token_.valid = true;
      w.map(1);
      w.uint(2), w.bytes(enc);
      break;
    }

    default: return kInvalidSubcommand;  // 2.1 subcommands (permissions, UV) are not offered
  }
  out = std::move(w.out);
  return kOk;
}

uint8_t Authenticator::userVerification(const Value* pinAuth, const Value* protocol, const uint8_t clientDataHash[32],
                                        bool uvOption, bool makeCredential, User& user, bool& uv) {
  uv = false;
  const bool pinSet = store_.pinSet();
  if (pinAuth) {
    // Zero length: "touch to pick this key" (CTAP 2.0 §5.1 step 5).
    if (pinAuth->str.empty()) {
      const uint8_t s = fromAnswer(user.waitPresence());
      if (s != kOk) return s;
      return pinSet ? kPinInvalid : kPinNotSet;
    }
    if (!pinSet) return kPinNotSet;
    if (!protocol) return kMissingParameter;
    int64_t proto = 0;
    if (!protocol->isInt()) return kCborUnexpectedType;
    if (!protocol->asInt(proto) || !pin::supported(proto)) return kPinAuthInvalid;
    if (!token_.valid || !pin::verify(crypto_, static_cast<int>(proto), token_.key.data(), token_.key.size(),
                                      clientDataHash, 32, pinAuth->str))
      return kPinAuthInvalid;
    uv = true;
    return kOk;
  }
  if (!pinSet) {
    uv = true;  // no signature is ever made while locked (docs/FIDO.md)
    return kOk;
  }
  // With a PIN, getInfo stops offering built-in "uv": only the PIN verifies.
  if (uvOption) return kUnsupportedOption;
  return makeCredential ? uint8_t{kPinRequired} : uint8_t{kOk};
}

uint8_t Authenticator::readHmacSecret(const Value* in, HmacSecret& out) {
  if (!in) return kOk;
  if (in->type != T::Map) return kCborUnexpectedType;
  const Value *platformKey = in->find(1), *saltEnc = in->find(2), *saltAuth = in->find(3), *proto = in->find(4);
  if (!platformKey || !saltEnc || !saltAuth) return kMissingParameter;
  if (saltEnc->type != T::Bytes || saltAuth->type != T::Bytes || (proto && proto->type != T::Uint))
    return kCborUnexpectedType;
  int protocol = 1;  // CTAP 2.0 platforms do not send it
  if (proto) {
    if (proto->u != 1 && proto->u != 2) return kInvalidParameter;
    protocol = static_cast<int>(proto->u);
  }
  pin::Shared s;
  if (uint8_t r = decapsulate(platformKey, protocol, s); r != kOk) return r;
  if (!pin::verify(crypto_, protocol, s.hmacKey, 32, saltEnc->str.data(), saltEnc->str.size(), saltAuth->str))
    return kPinAuthInvalid;
  std::vector<uint8_t> salts;
  if (!pin::decrypt(crypto_, s, saltEnc->str.data(), saltEnc->str.size(), salts)) return kInvalidLength;
  if (salts.size() != 32 && salts.size() != 64) {
    secureWipe(salts.data(), salts.size());
    return kInvalidLength;
  }
  out.present = true;
  out.protocol = protocol;
  std::memcpy(out.hmacKey.data(), s.hmacKey, 32);
  std::memcpy(out.aesKey.data(), s.aesKey, 32);
  std::memcpy(out.salts.data(), salts.data(), salts.size());
  out.saltLen = salts.size();
  secureWipe(salts.data(), salts.size());
  return kOk;
}

bool Authenticator::hmacSecretOutput(const HmacSecret& in, const Found& f, std::vector<uint8_t>& out) {
  uint8_t outputs[64];
  bool ok = crypto_.hmacSha256(f.credRandom.data(), 32, in.salts.data(), 32, outputs) &&
            (in.saltLen == 32 || crypto_.hmacSha256(f.credRandom.data(), 32, in.salts.data() + 32, 32, outputs + 32));
  if (ok) {
    pin::Shared s;
    s.protocol = in.protocol;
    std::memcpy(s.hmacKey, in.hmacKey.data(), 32);
    std::memcpy(s.aesKey, in.aesKey.data(), 32);
    ok = pin::encrypt(crypto_, s, outputs, in.saltLen, out);
  }
  secureWipe(outputs, sizeof outputs);
  return ok;
}

}  // namespace keyra::fido
