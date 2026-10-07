// The low-level EC_KEY API is the simplest way to sign with a raw scalar;
// it is deprecated in OpenSSL 3 but still present.
#define OPENSSL_SUPPRESS_DEPRECATED
#include "host_crypto.hpp"

#include <openssl/bn.h>
#include <openssl/core_names.h>
#include <openssl/ec.h>
#include <openssl/ecdsa.h>
#include <openssl/evp.h>
#include <openssl/obj_mac.h>
#include <openssl/param_build.h>
#include <openssl/rand.h>
#include <openssl/sha.h>
#include <openssl/x509.h>
#include <openssl/x509v3.h>

#include <cstring>

namespace keyra::fido::test {

bool OpenSslCrypto::random(uint8_t* out, size_t n) { return RAND_bytes(out, static_cast<int>(n)) == 1; }

bool OpenSslCrypto::sha256(const uint8_t* msg, size_t n, uint8_t out[32]) {
  return SHA256(msg, n, out) != nullptr;
}

bool OpenSslCrypto::gcmSeal(const uint8_t key[32], const uint8_t iv[12], const uint8_t* aad, size_t aadLen,
                            const uint8_t* in, size_t n, uint8_t* out) {
  EVP_CIPHER_CTX* c = EVP_CIPHER_CTX_new();
  int len = 0;
  bool ok = c && EVP_EncryptInit_ex(c, EVP_aes_256_gcm(), nullptr, key, iv) == 1 &&
            (aadLen == 0 || EVP_EncryptUpdate(c, nullptr, &len, aad, static_cast<int>(aadLen)) == 1) &&
            EVP_EncryptUpdate(c, out, &len, in, static_cast<int>(n)) == 1 &&
            EVP_EncryptFinal_ex(c, out + len, &len) == 1 &&
            EVP_CIPHER_CTX_ctrl(c, EVP_CTRL_GCM_GET_TAG, 16, out + n) == 1;
  EVP_CIPHER_CTX_free(c);
  return ok;
}

bool OpenSslCrypto::gcmOpen(const uint8_t key[32], const uint8_t iv[12], const uint8_t* aad, size_t aadLen,
                            const uint8_t* in, size_t n, uint8_t* out) {
  if (n < 16) return false;
  EVP_CIPHER_CTX* c = EVP_CIPHER_CTX_new();
  int len = 0;
  uint8_t tag[16];
  std::memcpy(tag, in + n - 16, 16);
  bool ok = c && EVP_DecryptInit_ex(c, EVP_aes_256_gcm(), nullptr, key, iv) == 1 &&
            (aadLen == 0 || EVP_DecryptUpdate(c, nullptr, &len, aad, static_cast<int>(aadLen)) == 1) &&
            EVP_DecryptUpdate(c, out, &len, in, static_cast<int>(n - 16)) == 1 &&
            EVP_CIPHER_CTX_ctrl(c, EVP_CTRL_GCM_SET_TAG, 16, tag) == 1 && EVP_DecryptFinal_ex(c, out + len, &len) == 1;
  EVP_CIPHER_CTX_free(c);
  if (!ok) std::memset(out, 0, n - 16);
  return ok;
}

bool OpenSslCrypto::p256Generate(uint8_t priv[32], uint8_t pub[65]) {
  EC_KEY* k = EC_KEY_new_by_curve_name(NID_X9_62_prime256v1);
  bool ok = k && EC_KEY_generate_key(k) == 1 && BN_bn2binpad(EC_KEY_get0_private_key(k), priv, 32) == 32 &&
            EC_POINT_point2oct(EC_KEY_get0_group(k), EC_KEY_get0_public_key(k), POINT_CONVERSION_UNCOMPRESSED, pub,
                               65, nullptr) == 65;
  EC_KEY_free(k);
  return ok;
}

bool OpenSslCrypto::p256Sign(const uint8_t priv[32], const uint8_t* msg, size_t n, uint8_t sig[64]) {
  uint8_t digest[32];
  SHA256(msg, n, digest);
  EC_KEY* k = EC_KEY_new_by_curve_name(NID_X9_62_prime256v1);
  BIGNUM* d = BN_bin2bn(priv, 32, nullptr);
  bool ok = k && d && EC_KEY_set_private_key(k, d) == 1;
  ECDSA_SIG* s = ok ? ECDSA_do_sign(digest, 32, k) : nullptr;
  ok = s && BN_bn2binpad(ECDSA_SIG_get0_r(s), sig, 32) == 32 && BN_bn2binpad(ECDSA_SIG_get0_s(s), sig + 32, 32) == 32;
  ECDSA_SIG_free(s);
  BN_clear_free(d);
  EC_KEY_free(k);
  return ok;
}

namespace {

bool verifyWith(EVP_PKEY* key, const std::vector<uint8_t>& msg, const std::vector<uint8_t>& der) {
  EVP_MD_CTX* c = EVP_MD_CTX_new();
  const bool ok = c && key && EVP_DigestVerifyInit(c, nullptr, EVP_sha256(), nullptr, key) == 1 &&
                  EVP_DigestVerify(c, der.data(), der.size(), msg.data(), msg.size()) == 1;
  EVP_MD_CTX_free(c);
  return ok;
}

}  // namespace

bool verifyEs256(const uint8_t pub[65], const std::vector<uint8_t>& msg, const std::vector<uint8_t>& der) {
  OSSL_PARAM_BLD* b = OSSL_PARAM_BLD_new();
  OSSL_PARAM_BLD_push_utf8_string(b, OSSL_PKEY_PARAM_GROUP_NAME, "prime256v1", 0);
  OSSL_PARAM_BLD_push_octet_string(b, OSSL_PKEY_PARAM_PUB_KEY, pub, 65);
  OSSL_PARAM* params = OSSL_PARAM_BLD_to_param(b);
  EVP_PKEY_CTX* ctx = EVP_PKEY_CTX_new_from_name(nullptr, "EC", nullptr);
  EVP_PKEY* key = nullptr;
  const bool built = ctx && EVP_PKEY_fromdata_init(ctx) == 1 &&
                     EVP_PKEY_fromdata(ctx, &key, EVP_PKEY_PUBLIC_KEY, params) == 1;
  const bool ok = built && verifyWith(key, msg, der);
  EVP_PKEY_free(key);
  EVP_PKEY_CTX_free(ctx);
  OSSL_PARAM_free(params);
  OSSL_PARAM_BLD_free(b);
  return ok;
}

bool selfSignedCertOk(const std::vector<uint8_t>& certDer) {
  const uint8_t* p = certDer.data();
  X509* x = d2i_X509(nullptr, &p, static_cast<long>(certDer.size()));
  EVP_PKEY* key = x ? X509_get_pubkey(x) : nullptr;
  const bool ok = key && p == certDer.data() + certDer.size() && X509_verify(x, key) == 1 &&
                  X509_check_issued(x, x) == X509_V_OK;
  EVP_PKEY_free(key);
  X509_free(x);
  return ok;
}

bool verifyWithCert(const std::vector<uint8_t>& certDer, const std::vector<uint8_t>& msg,
                    const std::vector<uint8_t>& der) {
  const uint8_t* p = certDer.data();
  X509* x = d2i_X509(nullptr, &p, static_cast<long>(certDer.size()));
  EVP_PKEY* key = x ? X509_get_pubkey(x) : nullptr;
  const bool ok = key && verifyWith(key, msg, der);
  EVP_PKEY_free(key);
  X509_free(x);
  return ok;
}

}  // namespace keyra::fido::test
