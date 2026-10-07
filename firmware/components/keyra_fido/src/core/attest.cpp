#include "attest.hpp"

#include <cstring>
#include <string>

namespace keyra::fido {

std::vector<uint8_t> derSignature(const uint8_t sig[64]) {
  auto integer = [](const uint8_t* x) {
    size_t i = 0;
    while (i < 31 && x[i] == 0) ++i;  // minimal encoding
    std::vector<uint8_t> v;
    v.push_back(0x02);
    const bool pad = (x[i] & 0x80) != 0;  // keep it positive
    v.push_back(static_cast<uint8_t>(32 - i + (pad ? 1 : 0)));
    if (pad) v.push_back(0);
    v.insert(v.end(), x + i, x + 32);
    return v;
  };
  const auto r = integer(sig), s = integer(sig + 32);
  std::vector<uint8_t> out{0x30, static_cast<uint8_t>(r.size() + s.size())};
  out.insert(out.end(), r.begin(), r.end());
  out.insert(out.end(), s.begin(), s.end());
  return out;
}

namespace attest {
namespace {

using Bytes = std::vector<uint8_t>;

Bytes tlv(uint8_t tag, const Bytes& content) {
  Bytes out{tag};
  const size_t n = content.size();
  if (n < 0x80) {
    out.push_back(static_cast<uint8_t>(n));
  } else if (n <= 0xFF) {
    out.insert(out.end(), {0x81, static_cast<uint8_t>(n)});
  } else {
    out.insert(out.end(), {0x82, static_cast<uint8_t>(n >> 8), static_cast<uint8_t>(n)});
  }
  out.insert(out.end(), content.begin(), content.end());
  return out;
}

Bytes cat(std::initializer_list<Bytes> parts) {
  Bytes out;
  for (const auto& p : parts) out.insert(out.end(), p.begin(), p.end());
  return out;
}

Bytes text(const char* s) { return Bytes(s, s + std::strlen(s)); }

const Bytes kOidEcdsaSha256 = {0x06, 0x08, 0x2A, 0x86, 0x48, 0xCE, 0x3D, 0x04, 0x03, 0x02};
const Bytes kOidEcPublicKey = {0x06, 0x07, 0x2A, 0x86, 0x48, 0xCE, 0x3D, 0x02, 0x01};
const Bytes kOidP256 = {0x06, 0x08, 0x2A, 0x86, 0x48, 0xCE, 0x3D, 0x03, 0x01, 0x07};
const Bytes kOidCommonName = {0x06, 0x03, 0x55, 0x04, 0x03};

}  // namespace

bool makeCert(Crypto& c, const uint8_t priv[32], const uint8_t pub[65], std::vector<uint8_t>& out) {
  Bytes serial(16);
  if (!c.random(serial.data(), serial.size())) return false;
  serial[0] = static_cast<uint8_t>((serial[0] & 0x7F) | 0x40);  // positive, minimal (no leading zero)

  const Bytes alg = tlv(0x30, kOidEcdsaSha256);
  const Bytes name = tlv(0x30, tlv(0x31, tlv(0x30, cat({kOidCommonName, tlv(0x0C, text("Keyra U2F self attestation"))}))));
  const Bytes validity = tlv(0x30, cat({tlv(0x17, text("260101000000Z")), tlv(0x18, text("99991231235959Z"))}));
  Bytes key{0x00};  // BIT STRING: no unused bits
  key.insert(key.end(), pub, pub + 65);
  const Bytes spki = tlv(0x30, cat({tlv(0x30, cat({kOidEcPublicKey, kOidP256})), tlv(0x03, key)}));
  const Bytes tbs = tlv(0x30, cat({tlv(0x02, serial), alg, name, validity, name, spki}));

  uint8_t sig[64];
  if (!c.p256Sign(priv, tbs.data(), tbs.size(), sig)) return false;
  Bytes bits{0x00};
  const Bytes der = derSignature(sig);
  bits.insert(bits.end(), der.begin(), der.end());
  out = tlv(0x30, cat({tbs, alg, tlv(0x03, bits)}));
  return true;
}

bool get(Crypto& c, AttestationStore& s, uint8_t priv[32], std::vector<uint8_t>& cert) {
  if (s.load(priv, cert) && !cert.empty()) return true;
  uint8_t pub[65];
  const bool ok = c.p256Generate(priv, pub) && makeCert(c, priv, pub, cert) && s.save(priv, cert);
  if (!ok) std::memset(priv, 0, 32);
  return ok;
}

}  // namespace attest
}  // namespace keyra::fido
