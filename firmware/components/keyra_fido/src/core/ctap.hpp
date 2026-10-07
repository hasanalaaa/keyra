#pragma once
// CTAP2 authenticator commands and CTAP1/U2F APDUs (docs/FIDO.md). Pure C++:
// crypto, storage, the counter and the person are injected (platform.hpp).
#include <array>
#include <cstdint>
#include <string>
#include <vector>

#include "attest.hpp"
#include "cred.hpp"
#include "platform.hpp"

namespace keyra::fido {

// AAGUID b722a2aa-5acc-4835-9c91-5fa93812679d (random, the same for every Keyra).
constexpr std::array<uint8_t, 16> kAaguid = {0xb7, 0x22, 0xa2, 0xaa, 0x5a, 0xcc, 0x48, 0x35,
                                             0x9c, 0x91, 0x5f, 0xa9, 0x38, 0x12, 0x67, 0x9d};
constexpr int64_t kResetWindowMs = 10000;   // authenticatorReset only right after power-up
constexpr int64_t kNextAssertionMs = 30000;  // GetNextAssertion validity

namespace ctap {
enum Cmd : uint8_t {
  kMakeCredential = 0x01, kGetAssertion = 0x02, kGetInfo = 0x04, kClientPin = 0x06,
  kReset = 0x07, kGetNextAssertion = 0x08, kSelection = 0x0B,
};
enum Status : uint8_t {
  kOk = 0x00, kInvalidCommand = 0x01, kInvalidParameter = 0x02, kInvalidLength = 0x03,
  kCborUnexpectedType = 0x11, kInvalidCbor = 0x12, kMissingParameter = 0x14, kLimitExceeded = 0x15,
  kCredentialExcluded = 0x19, kUnsupportedAlgorithm = 0x26, kOperationDenied = 0x27, kKeyStoreFull = 0x28,
  kUnsupportedOption = 0x2B, kInvalidOption = 0x2C, kKeepaliveCancel = 0x2D, kNoCredentials = 0x2E,
  kUserActionTimeout = 0x2F, kNotAllowed = 0x30, kPinNotSet = 0x35, kOther = 0x7F,
};
}  // namespace ctap

namespace u2f {
constexpr uint16_t kSwOk = 0x9000, kSwConditions = 0x6985, kSwWrongData = 0x6A80, kSwWrongLength = 0x6700,
                   kSwIns = 0x6D00, kSwCla = 0x6E00, kSwOther = 0x6F00;
}  // namespace u2f

class Authenticator {
 public:
  Authenticator(Crypto& c, Store& s, Counter& n, AttestationStore& a)
      : crypto_(c), store_(s), counter_(n), attestation_(a) {}

  // CTAPHID_CBOR: command byte + CBOR → status byte + CBOR.
  std::vector<uint8_t> cbor(const uint8_t* req, size_t n, User& user, int64_t nowMs);
  // CTAPHID_MSG: U2F APDU → response data + status word.
  std::vector<uint8_t> msg(const uint8_t* apdu, size_t n, User& user);

 private:
  struct Found {
    std::vector<uint8_t> credId;
    std::array<uint8_t, 32> priv{};
    bool resident = false;
    std::vector<uint8_t> userId;
    std::string userName, displayName;
    int64_t created = 0;
  };
  struct Next {  // GetNextAssertion state
    std::vector<Found> rest;
    std::array<uint8_t, 32> rpIdHash{}, clientDataHash{};
    uint8_t flags = 0;
    int64_t until = 0;
  };

  uint8_t makeCredential(const uint8_t* p, size_t n, User& user, std::vector<uint8_t>& out);
  uint8_t getAssertion(const uint8_t* p, size_t n, User& user, int64_t now, std::vector<uint8_t>& out);
  uint8_t getNextAssertion(int64_t now, std::vector<uint8_t>& out);
  uint8_t getInfo(std::vector<uint8_t>& out);
  uint8_t reset(User& user);
  uint8_t selection(User& user);

  // allowList / excludeList entry, or a resident record: the credential if it is ours and alive.
  bool lookup(const uint8_t key[32], const uint8_t rpIdHash[32], const std::vector<uint8_t>& id,
              const std::vector<cred::Resident>& residents, Found& out);
  bool residents(std::vector<cred::Resident>& out);
  uint8_t assertion(const Found& f, const uint8_t rpIdHash[32], const uint8_t clientDataHash[32], uint8_t flags,
                    bool includeUser, size_t count, std::vector<uint8_t>& out);
  bool sign(const uint8_t priv[32], const std::vector<uint8_t>& authData, const uint8_t* clientDataHash,
            std::vector<uint8_t>& der);

  uint16_t u2fRegister(const uint8_t* body, User& user, std::vector<uint8_t>& out);
  uint16_t u2fAuthenticate(uint8_t p1, const uint8_t* body, size_t lc, User& user, std::vector<uint8_t>& out);

  Crypto& crypto_;
  Store& store_;
  Counter& counter_;
  AttestationStore& attestation_;
  Next next_;
};

}  // namespace keyra::fido
