#pragma once
// U2F attestation (docs/FIDO.md): U2F registration must carry an X.509
// certificate. Each Keyra makes its own P-256 attestation key on first use
// (platform RNG) and a minimal self-signed certificate for it; both are kept
// by the platform (NVS on the device) and replaced by a factory reset. The
// certificate proves nothing beyond "this Keyra"; nothing is certified.
#include <cstdint>
#include <vector>

#include "platform.hpp"

namespace keyra::fido {

// Persisted attestation key + certificate.
class AttestationStore {
 public:
  virtual ~AttestationStore() = default;
  virtual bool load(uint8_t priv[32], std::vector<uint8_t>& cert) = 0;  // false: none stored yet
  virtual bool save(const uint8_t priv[32], const std::vector<uint8_t>& cert) = 0;
};

// DER ECDSA-Sig-Value from r || s.
std::vector<uint8_t> derSignature(const uint8_t sig[64]);

namespace attest {

// X.509 v1 certificate, subject = issuer = "CN=Keyra U2F self attestation",
// random positive 16-byte serial, valid 2026-01-01 to 9999-12-31 (RFC 5280
// "no expiry"), P-256 key `pub`, ecdsa-with-SHA256 signed by `priv`.
bool makeCert(Crypto& c, const uint8_t priv[32], const uint8_t pub[65], std::vector<uint8_t>& out);

// The stored key and certificate, created and saved on first use.
bool get(Crypto& c, AttestationStore& s, uint8_t priv[32], std::vector<uint8_t>& cert);

}  // namespace attest
}  // namespace keyra::fido
