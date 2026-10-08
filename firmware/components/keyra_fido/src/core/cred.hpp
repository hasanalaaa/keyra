#pragma once
// Credential IDs (= U2F key handles) and the resident record encoding
// (docs/FIDO.md "Keys and storage").
//
//   credential ID (62 bytes): 0x01 | nonce[12] | AES-256-GCM(Kwrap, nonce,
//       AAD = rpIdHash, privateKey[32] | flags[1]) | tag[16]
//
// The private key lives only inside the ID, encrypted; a resident credential
// additionally has a vault record (below) and is accepted only while it exists.
#include <cstdint>
#include <string>
#include <vector>

#include "platform.hpp"

namespace keyra::fido::cred {

constexpr size_t kIdLen = 62;
constexpr uint8_t kIdVersion = 0x01;
// flags: bit 0 discoverable, bit 1 hmac-secret was asked for at creation,
// bits 2-3 the credProtect level (0 = an ID from before credProtect = level 1).
// Bits 4-7 are zero; IDs made before these bits existed still read the same.
constexpr uint8_t kFlagResident = 0x01, kFlagHmacSecret = 0x02;
constexpr uint8_t kProtectShift = 2, kProtectMask = 0x0C;
inline uint8_t protectLevel(uint8_t flags) {
  const uint8_t level = (flags & kProtectMask) >> kProtectShift;
  return level == 0 ? 1 : level;
}
inline uint8_t withProtect(uint8_t flags, uint8_t level) {
  return static_cast<uint8_t>((flags & ~kProtectMask) | ((level & 3) << kProtectShift));
}

bool wrap(Crypto& c, const uint8_t key[32], const uint8_t rpIdHash[32], const uint8_t priv[32], uint8_t flags,
          uint8_t out[kIdLen]);
// False for IDs that are not ours, belong to another RP or another wrapping key.
bool unwrap(Crypto& c, const uint8_t key[32], const uint8_t rpIdHash[32], const uint8_t* id, size_t n,
            uint8_t priv[32], uint8_t& flags);

// Limits: user names are truncated to 64 bytes (CTAP allows that), the rest is refused.
constexpr size_t kMaxResident = 50;  // = vault::kMaxPasskeys
constexpr size_t kMaxRpId = 256, kMaxUserId = 64, kMaxName = 64;

struct Resident {
  uint32_t recordId = 0;  // vault record id, not stored inside
  std::string rpId;
  uint8_t rpIdHash[32] = {};
  std::vector<uint8_t> userId;
  std::string userName, displayName;
  int64_t created = 0;  // unix seconds, 0 = unknown
  std::vector<uint8_t> credId;
};

// CBOR map {1: rpId, 2: rpIdHash, 3: userId, 4: userName, 5: displayName, 6: created, 7: credId}.
std::vector<uint8_t> encode(const Resident& r);
bool decode(const std::vector<uint8_t>& data, Resident& out);

// Cuts s to at most max bytes without splitting a UTF-8 sequence.
std::string truncateUtf8(const std::string& s, size_t max);

}  // namespace keyra::fido::cred
