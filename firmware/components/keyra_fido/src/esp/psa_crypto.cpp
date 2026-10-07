// fido::Crypto over PSA (Mbed TLS 4 / TF-PSA-Crypto in ESP-IDF 6). Keys are
// imported as volatile PSA keys only for the duration of one operation.
#include "psa_crypto.hpp"

#include <cstring>

#include "psa/crypto.h"

namespace keyra::fido::esp {
namespace {

bool ready() { return psa_crypto_init() == PSA_SUCCESS; }  // idempotent

class TempKey {
 public:
  TempKey(psa_key_type_t type, size_t bits, const uint8_t* key, size_t len, psa_key_usage_t usage,
          psa_algorithm_t alg) {
    psa_key_attributes_t a = PSA_KEY_ATTRIBUTES_INIT;
    psa_set_key_type(&a, type);
    psa_set_key_bits(&a, bits);
    psa_set_key_usage_flags(&a, usage);
    psa_set_key_algorithm(&a, alg);
    ok_ = ready() && psa_import_key(&a, key, len, &id_) == PSA_SUCCESS;
    psa_reset_key_attributes(&a);
  }
  ~TempKey() {
    if (ok_) psa_destroy_key(id_);
  }
  TempKey(const TempKey&) = delete;
  TempKey& operator=(const TempKey&) = delete;
  bool ok() const { return ok_; }
  mbedtls_svc_key_id_t id() const { return id_; }

 private:
  mbedtls_svc_key_id_t id_ = MBEDTLS_SVC_KEY_ID_INIT;
  bool ok_ = false;
};

constexpr psa_key_type_t kP256Pair = PSA_KEY_TYPE_ECC_KEY_PAIR(PSA_ECC_FAMILY_SECP_R1);
constexpr psa_algorithm_t kEs256 = PSA_ALG_ECDSA(PSA_ALG_SHA_256);

}  // namespace

bool PsaCrypto::random(uint8_t* out, size_t n) { return ready() && psa_generate_random(out, n) == PSA_SUCCESS; }

bool PsaCrypto::sha256(const uint8_t* msg, size_t n, uint8_t out[32]) {
  size_t len = 0;
  return ready() && psa_hash_compute(PSA_ALG_SHA_256, msg, n, out, 32, &len) == PSA_SUCCESS && len == 32;
}

bool PsaCrypto::gcmSeal(const uint8_t key[32], const uint8_t iv[12], const uint8_t* aad, size_t aadLen,
                        const uint8_t* in, size_t n, uint8_t* out) {
  TempKey k(PSA_KEY_TYPE_AES, 256, key, 32, PSA_KEY_USAGE_ENCRYPT, PSA_ALG_GCM);
  size_t len = 0;
  return k.ok() &&
         psa_aead_encrypt(k.id(), PSA_ALG_GCM, iv, 12, aad, aadLen, in, n, out, n + 16, &len) == PSA_SUCCESS &&
         len == n + 16;
}

bool PsaCrypto::gcmOpen(const uint8_t key[32], const uint8_t iv[12], const uint8_t* aad, size_t aadLen,
                        const uint8_t* in, size_t n, uint8_t* out) {
  if (n < 16) return false;
  TempKey k(PSA_KEY_TYPE_AES, 256, key, 32, PSA_KEY_USAGE_DECRYPT, PSA_ALG_GCM);
  size_t len = 0;
  return k.ok() &&
         psa_aead_decrypt(k.id(), PSA_ALG_GCM, iv, 12, aad, aadLen, in, n, out, n - 16, &len) == PSA_SUCCESS &&
         len == n - 16;
}

bool PsaCrypto::p256Generate(uint8_t priv[32], uint8_t pub[65]) {
  if (!ready()) return false;
  psa_key_attributes_t a = PSA_KEY_ATTRIBUTES_INIT;
  psa_set_key_type(&a, kP256Pair);
  psa_set_key_bits(&a, 256);
  psa_set_key_usage_flags(&a, PSA_KEY_USAGE_EXPORT);
  psa_set_key_algorithm(&a, kEs256);
  mbedtls_svc_key_id_t id = MBEDTLS_SVC_KEY_ID_INIT;
  bool ok = psa_generate_key(&a, &id) == PSA_SUCCESS;
  psa_reset_key_attributes(&a);
  size_t privLen = 0, pubLen = 0;
  ok = ok && psa_export_key(id, priv, 32, &privLen) == PSA_SUCCESS && privLen == 32 &&
       psa_export_public_key(id, pub, 65, &pubLen) == PSA_SUCCESS && pubLen == 65;
  psa_destroy_key(id);
  if (!ok) std::memset(priv, 0, 32);
  return ok;
}

bool PsaCrypto::p256Sign(const uint8_t priv[32], const uint8_t* msg, size_t n, uint8_t sig[64]) {
  TempKey k(kP256Pair, 256, priv, 32, PSA_KEY_USAGE_SIGN_MESSAGE, kEs256);
  size_t len = 0;
  return k.ok() && psa_sign_message(k.id(), kEs256, msg, n, sig, 64, &len) == PSA_SUCCESS && len == 64;
}

}  // namespace keyra::fido::esp
