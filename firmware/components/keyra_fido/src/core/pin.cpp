#include "pin.hpp"

#include <cstring>

namespace keyra::fido::pin {
namespace {

constexpr uint8_t kVersion = 1;

void wipe(void* p, size_t n) { secureWipe(p, n); }

}  // namespace

std::vector<uint8_t> encode(const State& s) {
  std::vector<uint8_t> out{kVersion, s.retries};
  out.insert(out.end(), s.hash, s.hash + 16);
  return out;
}

bool decode(const std::vector<uint8_t>& data, State& out) {
  if (data.size() != 18 || data[0] != kVersion || data[1] > kMaxRetries) return false;
  out.retries = data[1];
  std::memcpy(out.hash, data.data() + 2, 16);
  return true;
}

void Shared::clear() {
  wipe(hmacKey, sizeof hmacKey);
  wipe(aesKey, sizeof aesKey);
  protocol = 0;
}

bool hkdf32(Crypto& c, const uint8_t* salt, size_t saltLen, const uint8_t* ikm, size_t ikmLen, const char* info,
            uint8_t out[32]) {
  uint8_t prk[32];
  // T(1) = HMAC(PRK, info || 0x01)
  const size_t infoLen = std::strlen(info);
  std::vector<uint8_t> msg(info, info + infoLen);
  msg.push_back(0x01);
  const bool ok = c.hmacSha256(salt, saltLen, ikm, ikmLen, prk) && c.hmacSha256(prk, 32, msg.data(), msg.size(), out);
  wipe(prk, sizeof prk);
  return ok;
}

bool sharedSecret(Crypto& c, int protocol, const uint8_t priv[32], const uint8_t peer[65], Shared& out) {
  out.clear();
  if (!supported(protocol)) return false;
  uint8_t z[32];
  if (!c.p256Ecdh(priv, peer, z)) return false;
  bool ok;
  if (protocol == 1) {
    ok = c.sha256(z, 32, out.hmacKey);
    std::memcpy(out.aesKey, out.hmacKey, 32);
  } else {
    const uint8_t salt[32] = {};
    ok = hkdf32(c, salt, sizeof salt, z, 32, "CTAP2 HMAC key", out.hmacKey) &&
         hkdf32(c, salt, sizeof salt, z, 32, "CTAP2 AES key", out.aesKey);
  }
  wipe(z, sizeof z);
  if (!ok) {
    out.clear();
    return false;
  }
  out.protocol = protocol;
  return true;
}

bool encrypt(Crypto& c, const Shared& s, const uint8_t* in, size_t n, std::vector<uint8_t>& out) {
  out.clear();
  if (n % 16 != 0 || !supported(s.protocol)) return false;
  uint8_t iv[16] = {};
  const size_t ivLen = s.protocol == 2 ? 16 : 0;
  if (ivLen && !c.random(iv, 16)) return false;
  out.assign(iv, iv + ivLen);
  out.resize(ivLen + n);
  if (!c.aesCbc(true, s.aesKey, iv, in, n, out.data() + ivLen)) {
    out.clear();
    return false;
  }
  return true;
}

bool decrypt(Crypto& c, const Shared& s, const uint8_t* in, size_t n, std::vector<uint8_t>& out) {
  out.clear();
  if (!supported(s.protocol)) return false;
  const size_t ivLen = s.protocol == 2 ? 16 : 0;
  if (n < ivLen + 16 || (n - ivLen) % 16 != 0) return false;
  uint8_t iv[16] = {};
  if (ivLen) std::memcpy(iv, in, 16);
  out.resize(n - ivLen);
  if (!c.aesCbc(false, s.aesKey, iv, in + ivLen, n - ivLen, out.data())) {
    wipe(out.data(), out.size());
    out.clear();
    return false;
  }
  return true;
}

std::vector<uint8_t> authenticate(Crypto& c, int protocol, const uint8_t* key, size_t keyLen, const uint8_t* msg,
                                  size_t n) {
  uint8_t mac[32];
  if (!supported(protocol) || !c.hmacSha256(key, keyLen, msg, n, mac)) return {};
  return std::vector<uint8_t>(mac, mac + (protocol == 1 ? 16 : 32));
}

bool verify(Crypto& c, int protocol, const uint8_t* key, size_t keyLen, const uint8_t* msg, size_t n,
            const std::vector<uint8_t>& sig) {
  const std::vector<uint8_t> want = authenticate(c, protocol, key, keyLen, msg, n);
  return !want.empty() && sig.size() == want.size() && equal(sig.data(), want.data(), want.size());
}

bool equal(const uint8_t* a, const uint8_t* b, size_t n) {
  uint8_t d = 0;
  for (size_t i = 0; i < n; ++i) d |= a[i] ^ b[i];
  return d == 0;
}

bool unpad(const std::vector<uint8_t>& padded, std::vector<uint8_t>& pin) {
  pin.clear();
  if (padded.size() != kPaddedPin) return false;
  size_t len = 0;
  while (len < padded.size() && padded[len] != 0) ++len;
  size_t codePoints = 0;
  for (size_t i = 0; i < len; ++i)
    if ((padded[i] & 0xC0) != 0x80) ++codePoints;  // not a UTF-8 continuation byte
  if (len > kMaxPinBytes || codePoints < kMinPinCodePoints) return false;
  pin.assign(padded.begin(), padded.begin() + len);
  return true;
}

}  // namespace keyra::fido::pin
