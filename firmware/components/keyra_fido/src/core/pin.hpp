#pragma once
// CTAP2 PIN/UV auth protocols 1 and 2 (CTAP 2.1 §6.5.6, §6.5.7) and the
// stored ClientPIN state (docs/FIDO.md "ClientPIN"). The FIDO PIN is its own
// secret, not the master passphrase: a PIN hash next to the vault would make
// the passphrase cheap to guess.
#include <cstddef>
#include <cstdint>
#include <vector>

#include "platform.hpp"

namespace keyra::fido::pin {

constexpr uint8_t kMaxRetries = 8;
constexpr uint8_t kMaxConsecutive = 3;  // wrong PINs per power-up before PIN_AUTH_BLOCKED
constexpr size_t kMinPinCodePoints = 4, kMaxPinBytes = 63, kPaddedPin = 64;

// The vault record (opaque to the vault):
//   u8 version = 1 | u8 retries | LEFT(SHA-256(PIN), 16)
struct State {
  uint8_t retries = kMaxRetries;
  uint8_t hash[16] = {};
};
std::vector<uint8_t> encode(const State& s);
bool decode(const std::vector<uint8_t>& data, State& out);

// The keys one ECDH with the platform yields. Protocol 1 uses SHA-256(Z) for
// both; protocol 2 derives each with HKDF-SHA-256. Wiped when it goes out of scope.
struct Shared {
  int protocol = 0;
  uint8_t hmacKey[32] = {};
  uint8_t aesKey[32] = {};
  Shared() = default;
  Shared(const Shared&) = delete;
  Shared& operator=(const Shared&) = delete;
  ~Shared() { clear(); }
  void clear();
};

inline bool supported(int64_t protocol) { return protocol == 1 || protocol == 2; }

// RFC 5869 with L = 32 (one HMAC block of output).
bool hkdf32(Crypto& c, const uint8_t* salt, size_t saltLen, const uint8_t* ikm, size_t ikmLen, const char* info,
            uint8_t out[32]);
// ECDH with the platform's key (COSE x, y) and the KDF of `protocol`.
bool sharedSecret(Crypto& c, int protocol, const uint8_t priv[32], const uint8_t peer[65], Shared& out);
// Protocol 1: AES-256-CBC with a zero IV. Protocol 2: a random IV, sent in front.
bool encrypt(Crypto& c, const Shared& s, const uint8_t* in, size_t n, std::vector<uint8_t>& out);
// False when the length does not fit the protocol (n a multiple of 16, plus the IV for 2).
bool decrypt(Crypto& c, const Shared& s, const uint8_t* in, size_t n, std::vector<uint8_t>& out);
// HMAC-SHA-256 under key; protocol 1 keeps the first 16 bytes. verify() compares in constant time.
std::vector<uint8_t> authenticate(Crypto& c, int protocol, const uint8_t* key, size_t keyLen, const uint8_t* msg,
                                  size_t n);
bool verify(Crypto& c, int protocol, const uint8_t* key, size_t keyLen, const uint8_t* msg, size_t n,
            const std::vector<uint8_t>& sig);

bool equal(const uint8_t* a, const uint8_t* b, size_t n);  // constant time
// The PIN inside a decrypted 64-byte block (up to the first zero byte), checked
// against the length policy: 4-63 bytes and at least 4 Unicode code points.
bool unpad(const std::vector<uint8_t>& padded, std::vector<uint8_t>& pin);

}  // namespace keyra::fido::pin
