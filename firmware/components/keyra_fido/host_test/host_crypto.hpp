#pragma once
// OpenSSL 3 implementation of fido::Crypto for host tests and the harness,
// plus an independent ES256 verifier the tests use to check signatures.
#include <vector>

#include "core/platform.hpp"

namespace keyra::fido::test {

class OpenSslCrypto final : public Crypto {
 public:
  bool random(uint8_t* out, size_t n) override;
  bool sha256(const uint8_t* msg, size_t n, uint8_t out[32]) override;
  bool gcmSeal(const uint8_t key[32], const uint8_t iv[12], const uint8_t* aad, size_t aadLen, const uint8_t* in,
               size_t n, uint8_t* out) override;
  bool gcmOpen(const uint8_t key[32], const uint8_t iv[12], const uint8_t* aad, size_t aadLen, const uint8_t* in,
               size_t n, uint8_t* out) override;
  bool p256Generate(uint8_t priv[32], uint8_t pub[65]) override;
  bool p256Sign(const uint8_t priv[32], const uint8_t* msg, size_t n, uint8_t sig[64]) override;
};

// Verifies a DER ECDSA-SHA256 signature with an uncompressed P-256 public key,
// using OpenSSL's EVP_DigestVerify (no code shared with the signer above).
bool verifyEs256(const uint8_t pub[65], const std::vector<uint8_t>& msg, const std::vector<uint8_t>& der);
// Same, with the public key taken from a DER X.509 certificate.
bool verifyWithCert(const std::vector<uint8_t>& certDer, const std::vector<uint8_t>& msg,
                    const std::vector<uint8_t>& der);

}  // namespace keyra::fido::test
