#include "host_fakes.hpp"

#include <openssl/evp.h>
#include <openssl/hmac.h>
#include <openssl/rand.h>

#include <memory>

namespace keyra::vault::test {

bool MemStorage::step() {
  if (dead) return false;
  if (crashAfter == 0) {
    dead = true;
    return false;
  }
  if (crashAfter > 0) --crashAfter;
  if (failAfter == 0) {
    failAfter = -1;
    return false;
  }
  if (failAfter > 0) --failAfter;
  return true;
}

Storage::Read MemStorage::read(const std::string& path, std::vector<uint8_t>& out) {
  if (dead) return Read::Error;
  auto it = files.find(path);
  if (it == files.end()) return Read::NotFound;
  out = it->second;
  return Read::Ok;
}

bool MemStorage::write(const std::string& path, const uint8_t* data, size_t n) {
  if (!step()) {
    // Power cut mid-write: a torn prefix is what may reach flash.
    if (dead) files[path].assign(data, data + n / 2);
    return false;
  }
  files[path].assign(data, data + n);
  return true;
}

bool MemStorage::rename(const std::string& from, const std::string& to) {
  if (!step()) return false;
  auto it = files.find(from);
  if (it == files.end()) return false;
  files[to] = std::move(it->second);
  files.erase(from);
  return true;
}

bool MemStorage::remove(const std::string& path) {
  if (!step()) return false;
  files.erase(path);
  return true;
}

bool MemStorage::list(const std::string& dir, std::vector<std::string>& names) {
  if (dead) return false;
  names.clear();
  const std::string prefix = dir.empty() ? "" : dir + "/";
  for (const auto& f : files) {
    if (f.first.compare(0, prefix.size(), prefix) != 0) continue;
    std::string rest = f.first.substr(prefix.size());
    if (rest.find('/') == std::string::npos) names.push_back(rest);
  }
  return true;
}

bool MemStorage::format() {
  if (!step()) return false;
  files.clear();
  return true;
}

bool OpenSslCrypto::random(uint8_t* out, size_t n) { return RAND_bytes(out, int(n)) == 1; }

bool OpenSslCrypto::pbkdf2Sha256(const std::string& pass, const uint8_t* salt, size_t saltLen,
                                 uint32_t iterations, uint8_t* out, size_t outLen) {
  ++pbkdf2Calls;
  if (onPbkdf2) onPbkdf2();
  return PKCS5_PBKDF2_HMAC(pass.data(), int(pass.size()), salt, int(saltLen), int(iterations),
                           EVP_sha256(), int(outLen), out) == 1;
}

namespace {
using Ctx = std::unique_ptr<EVP_CIPHER_CTX, decltype(&EVP_CIPHER_CTX_free)>;
}

bool OpenSslCrypto::gcmSeal(const uint8_t key[32], const uint8_t iv[12], const uint8_t* aad,
                            size_t aadLen, const uint8_t* in, size_t n, uint8_t* out) {
  Ctx ctx(EVP_CIPHER_CTX_new(), EVP_CIPHER_CTX_free);
  int len = 0;
  if (!ctx || EVP_EncryptInit_ex(ctx.get(), EVP_aes_256_gcm(), nullptr, key, iv) != 1) return false;
  if (aadLen && EVP_EncryptUpdate(ctx.get(), nullptr, &len, aad, int(aadLen)) != 1) return false;
  if (n && EVP_EncryptUpdate(ctx.get(), out, &len, in, int(n)) != 1) return false;
  if (EVP_EncryptFinal_ex(ctx.get(), out + n, &len) != 1) return false;
  return EVP_CIPHER_CTX_ctrl(ctx.get(), EVP_CTRL_GCM_GET_TAG, 16, out + n) == 1;
}

Crypto::Open OpenSslCrypto::gcmOpen(const uint8_t key[32], const uint8_t iv[12], const uint8_t* aad,
                                    size_t aadLen, const uint8_t* in, size_t n, uint8_t* out) {
  if (n < 16) return Open::AuthFailed;
  Ctx ctx(EVP_CIPHER_CTX_new(), EVP_CIPHER_CTX_free);
  int len = 0;
  size_t ctLen = n - 16;
  if (!ctx || EVP_DecryptInit_ex(ctx.get(), EVP_aes_256_gcm(), nullptr, key, iv) != 1)
    return Open::Error;
  if (aadLen && EVP_DecryptUpdate(ctx.get(), nullptr, &len, aad, int(aadLen)) != 1)
    return Open::Error;
  if (ctLen && EVP_DecryptUpdate(ctx.get(), out, &len, in, int(ctLen)) != 1) return Open::Error;
  if (EVP_CIPHER_CTX_ctrl(ctx.get(), EVP_CTRL_GCM_SET_TAG, 16, const_cast<uint8_t*>(in + ctLen)) != 1)
    return Open::Error;
  return EVP_DecryptFinal_ex(ctx.get(), out + ctLen, &len) == 1 ? Open::Ok : Open::AuthFailed;
}

bool OpenSslCrypto::hmac(Hash h, const uint8_t* key, size_t keyLen, const uint8_t* msg,
                         size_t msgLen, uint8_t* out, size_t* outLen) {
  const EVP_MD* md = h == Hash::Sha1 ? EVP_sha1() : h == Hash::Sha256 ? EVP_sha256() : EVP_sha512();
  unsigned int len = 0;
  if (!HMAC(md, key, int(keyLen), msg, msgLen, out, &len)) return false;
  *outLen = len;
  return true;
}

}  // namespace keyra::vault::test
