// ESP-IDF 6 ships Mbed TLS 4 / TF-PSA-Crypto, where the legacy mbedtls_gcm_*,
// mbedtls_md_hmac and PKCS#5 APIs are private; the PSA API is the public one.
#include "esp_adapters.hpp"
#include "psa/crypto.h"

namespace keyra::vault::esp {
namespace {

bool ready() { return psa_crypto_init() == PSA_SUCCESS; }  // idempotent

// Volatile key, destroyed (and wiped by PSA) when this goes out of scope.
class TempKey {
 public:
  TempKey(psa_key_type_t type, const uint8_t* key, size_t len, psa_key_usage_t usage,
          psa_algorithm_t alg) {
    psa_key_attributes_t a = PSA_KEY_ATTRIBUTES_INIT;
    psa_set_key_type(&a, type);
    psa_set_key_bits(&a, len * 8);
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

}  // namespace

bool PsaCrypto::random(uint8_t* out, size_t n) {
  return ready() && psa_generate_random(out, n) == PSA_SUCCESS;
}

bool PsaCrypto::pbkdf2Sha256(const std::string& pass, const uint8_t* salt, size_t saltLen,
                             uint32_t iterations, uint8_t* out, size_t outLen) {
  if (!ready()) return false;
  psa_key_derivation_operation_t op = PSA_KEY_DERIVATION_OPERATION_INIT;
  bool ok =
      psa_key_derivation_setup(&op, PSA_ALG_PBKDF2_HMAC(PSA_ALG_SHA_256)) == PSA_SUCCESS &&
      psa_key_derivation_input_integer(&op, PSA_KEY_DERIVATION_INPUT_COST, iterations) == PSA_SUCCESS &&
      psa_key_derivation_input_bytes(&op, PSA_KEY_DERIVATION_INPUT_SALT, salt, saltLen) == PSA_SUCCESS &&
      psa_key_derivation_input_bytes(&op, PSA_KEY_DERIVATION_INPUT_PASSWORD,
                                     reinterpret_cast<const uint8_t*>(pass.data()),
                                     pass.size()) == PSA_SUCCESS &&
      psa_key_derivation_output_bytes(&op, out, outLen) == PSA_SUCCESS;
  psa_key_derivation_abort(&op);  // wipes the password copy held by the operation
  return ok;
}

bool PsaCrypto::gcmSeal(const uint8_t key[32], const uint8_t iv[12], const uint8_t* aad,
                        size_t aadLen, const uint8_t* in, size_t n, uint8_t* out) {
  TempKey k(PSA_KEY_TYPE_AES, key, 32, PSA_KEY_USAGE_ENCRYPT, PSA_ALG_GCM);
  size_t len = 0;
  return k.ok() &&
         psa_aead_encrypt(k.id(), PSA_ALG_GCM, iv, 12, aad, aadLen, in, n, out, n + 16, &len) ==
             PSA_SUCCESS &&
         len == n + 16;
}

Crypto::Open PsaCrypto::gcmOpen(const uint8_t key[32], const uint8_t iv[12], const uint8_t* aad,
                                size_t aadLen, const uint8_t* in, size_t n, uint8_t* out) {
  if (n < 16) return Open::AuthFailed;
  TempKey k(PSA_KEY_TYPE_AES, key, 32, PSA_KEY_USAGE_DECRYPT, PSA_ALG_GCM);
  if (!k.ok()) return Open::Error;
  size_t len = 0;
  psa_status_t s =
      psa_aead_decrypt(k.id(), PSA_ALG_GCM, iv, 12, aad, aadLen, in, n, out, n - 16, &len);
  if (s == PSA_ERROR_INVALID_SIGNATURE) return Open::AuthFailed;
  return s == PSA_SUCCESS && len == n - 16 ? Open::Ok : Open::Error;
}

bool PsaCrypto::hmac(Hash h, const uint8_t* key, size_t keyLen, const uint8_t* msg, size_t msgLen,
                     uint8_t* out, size_t* outLen) {
  psa_algorithm_t hash = h == Hash::Sha1     ? PSA_ALG_SHA_1
                         : h == Hash::Sha256 ? PSA_ALG_SHA_256
                                             : PSA_ALG_SHA_512;
  TempKey k(PSA_KEY_TYPE_HMAC, key, keyLen, PSA_KEY_USAGE_SIGN_MESSAGE, PSA_ALG_HMAC(hash));
  return k.ok() &&
         psa_mac_compute(k.id(), PSA_ALG_HMAC(hash), msg, msgLen, out, 64, outLen) == PSA_SUCCESS;
}

}  // namespace keyra::vault::esp
