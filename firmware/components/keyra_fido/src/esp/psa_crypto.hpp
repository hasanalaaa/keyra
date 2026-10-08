#pragma once
#include "core/platform.hpp"

namespace keyra::fido::esp {

class PsaCrypto final : public Crypto {
 public:
  bool random(uint8_t* out, size_t n) override;
  bool sha256(const uint8_t* msg, size_t n, uint8_t out[32]) override;
  bool gcmSeal(const uint8_t key[32], const uint8_t iv[12], const uint8_t* aad, size_t aadLen, const uint8_t* in,
               size_t n, uint8_t* out) override;
  bool gcmOpen(const uint8_t key[32], const uint8_t iv[12], const uint8_t* aad, size_t aadLen, const uint8_t* in,
               size_t n, uint8_t* out) override;
  bool p256Generate(uint8_t priv[32], uint8_t pub[65]) override;
  bool p256Sign(const uint8_t priv[32], const uint8_t* msg, size_t n, uint8_t sig[64]) override;
  bool p256Ecdh(const uint8_t priv[32], const uint8_t peer[65], uint8_t z[32]) override;
  bool hmacSha256(const uint8_t* key, size_t keyLen, const uint8_t* msg, size_t n, uint8_t out[32]) override;
  bool aesCbc(bool encrypt, const uint8_t key[32], const uint8_t iv[16], const uint8_t* in, size_t n,
              uint8_t* out) override;
};

}  // namespace keyra::fido::esp
