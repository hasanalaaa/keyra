#include "cred.hpp"

#include <cstring>

#include "cbor.hpp"

namespace keyra::fido::cred {

bool wrap(Crypto& c, const uint8_t key[32], const uint8_t rpIdHash[32], const uint8_t priv[32], uint8_t flags,
          uint8_t out[kIdLen]) {
  uint8_t plain[33];
  std::memcpy(plain, priv, 32);
  plain[32] = flags;
  out[0] = kIdVersion;
  const bool ok = c.random(out + 1, 12) && c.gcmSeal(key, out + 1, rpIdHash, 32, plain, sizeof plain, out + 13);
  std::memset(plain, 0, sizeof plain);
  return ok;
}

bool unwrap(Crypto& c, const uint8_t key[32], const uint8_t rpIdHash[32], const uint8_t* id, size_t n,
            uint8_t priv[32], uint8_t& flags) {
  if (n != kIdLen || id[0] != kIdVersion) return false;
  uint8_t plain[33];
  const bool ok = c.gcmOpen(key, id + 1, rpIdHash, 32, id + 13, n - 13, plain);
  if (ok) {
    std::memcpy(priv, plain, 32);
    flags = plain[32];
  }
  std::memset(plain, 0, sizeof plain);
  return ok;
}

std::vector<uint8_t> encode(const Resident& r) {
  cbor::Writer w;
  w.map(7);
  w.uint(1), w.text(r.rpId);
  w.uint(2), w.bytes(r.rpIdHash, 32);
  w.uint(3), w.bytes(r.userId);
  w.uint(4), w.text(r.userName);
  w.uint(5), w.text(r.displayName);
  w.uint(6), w.integer(r.created);
  w.uint(7), w.bytes(r.credId);
  return w.out;
}

bool decode(const std::vector<uint8_t>& data, Resident& out) {
  cbor::Value v;
  if (!cbor::decode(data.data(), data.size(), v) || v.type != cbor::Value::Type::Map) return false;
  using T = cbor::Value::Type;
  const cbor::Value *rp = v.find(1), *hash = v.find(2), *uid = v.find(3), *name = v.find(4), *dn = v.find(5),
                    *created = v.find(6), *cid = v.find(7);
  if (!rp || rp->type != T::Text || !hash || hash->type != T::Bytes || hash->str.size() != 32 || !uid ||
      uid->type != T::Bytes || !name || name->type != T::Text || !dn || dn->type != T::Text || !created ||
      !created->asInt(out.created) || !cid || cid->type != T::Bytes || cid->str.size() != kIdLen)
    return false;
  out.rpId = rp->text();
  std::memcpy(out.rpIdHash, hash->str.data(), 32);
  out.userId = uid->str;
  out.userName = name->text();
  out.displayName = dn->text();
  out.credId = cid->str;
  return true;
}

std::string truncateUtf8(const std::string& s, size_t max) {
  if (s.size() <= max) return s;
  size_t n = max;
  while (n > 0 && (static_cast<uint8_t>(s[n]) & 0xC0) == 0x80) --n;  // s[n] starts the cut-off char
  return s.substr(0, n);
}

}  // namespace keyra::fido::cred
