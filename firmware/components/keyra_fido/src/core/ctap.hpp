#pragma once
// CTAP2 authenticator commands and CTAP1/U2F APDUs (docs/FIDO.md). Pure C++:
// crypto, storage, the counter and the person are injected (platform.hpp).
#include <array>
#include <cstdint>
#include <string>
#include <vector>

#include "attest.hpp"
#include "cbor.hpp"
#include "cred.hpp"
#include "pin.hpp"
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
  kUserActionTimeout = 0x2F, kNotAllowed = 0x30, kPinInvalid = 0x31, kPinBlocked = 0x32, kPinAuthInvalid = 0x33,
  kPinAuthBlocked = 0x34, kPinNotSet = 0x35, kPinRequired = 0x36, kPinPolicyViolation = 0x37,
  kInvalidSubcommand = 0x3E, kOther = 0x7F,
};
enum PinSub : uint8_t {
  kGetPinRetries = 0x01, kGetKeyAgreement = 0x02, kSetPin = 0x03, kChangePin = 0x04, kGetPinToken = 0x05,
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

  // CTAPHID_CBOR: command byte + CBOR → status byte + CBOR. `cid`: the CTAPHID
  // channel it came on (GetNextAssertion only continues on the same channel).
  std::vector<uint8_t> cbor(const uint8_t* req, size_t n, User& user, int64_t nowMs, uint32_t cid);
  // The vault locked: forgets a GetAssertion in progress (it holds no key
  // anyway) and the PIN token, so a PIN entered before the lock is asked again.
  void vaultLocked() {
    next_ = Next{};
    token_ = Token{};
  }
  // CTAPHID_MSG: U2F APDU → response data + status word.
  std::vector<uint8_t> msg(const uint8_t* apdu, size_t n, User& user);

 private:
  struct Found {
    std::vector<uint8_t> credId;
    std::array<uint8_t, 32> priv{};
    std::array<uint8_t, 32> credRandom{};  // hmac-secret, set by rearm() when the credential has it
    uint8_t flags = 0;                     // cred.hpp flags from inside the ID
    size_t keyIndex = 0;                   // the wrapping key it opened under
    bool resident = false;
    std::vector<uint8_t> userId;
    std::string userName, displayName;
    int64_t created = 0;
  };
  // A decrypted hmac-secret request (CTAP 2.1 §12.5): the shared secret to
  // answer under and one or two salts. Arrays, so resetting overwrites them.
  struct HmacSecret {
    bool present = false;
    int protocol = 0;
    std::array<uint8_t, 32> hmacKey{}, aesKey{};
    std::array<uint8_t, 64> salts{};
    size_t saltLen = 0;  // 32 or 64
    HmacSecret() = default;
    HmacSecret(const HmacSecret&) = default;
    HmacSecret& operator=(const HmacSecret&) = default;
    ~HmacSecret() {
      secureWipe(hmacKey.data(), hmacKey.size());
      secureWipe(aesKey.data(), aesKey.size());
      secureWipe(salts.data(), salts.size());
    }
  };
  // GetNextAssertion state: which credentials are left, never their keys — each
  // is unwrapped again, with the vault checked, right before it signs.
  struct Next {
    std::vector<Found> rest;  // priv always zero
    uint32_t cid = 0;
    std::array<uint8_t, 32> rpIdHash{}, clientDataHash{};
    uint8_t flags = 0;
    int64_t until = 0;
    HmacSecret hmac;  // the same salts apply to every credential of the sequence
  };

  uint8_t makeCredential(const uint8_t* p, size_t n, User& user, std::vector<uint8_t>& out);
  uint8_t getAssertion(const uint8_t* p, size_t n, User& user, int64_t now, std::vector<uint8_t>& out);
  uint8_t getNextAssertion(int64_t now, std::vector<uint8_t>& out);
  uint8_t getInfo(std::vector<uint8_t>& out);
  uint8_t reset(User& user);
  uint8_t selection(User& user);

  // ClientPIN (ctap_pin.cpp).
  uint8_t clientPin(const uint8_t* p, size_t n, User& user, std::vector<uint8_t>& out);
  // The authenticator's ECDH key for PIN and hmac-secret exchanges; made on
  // first use and again after a wrong PIN (CTAP 2.1 §6.5.5.7).
  bool keyAgreement();
  // Platform COSE key → shared secret under `protocol`.
  uint8_t decapsulate(const cbor::Value* cose, int protocol, pin::Shared& out);
  // Decrements and stores the retries, then compares pinHashEnc with the PIN.
  uint8_t checkPinHash(const pin::Shared& s, const cbor::Value* pinHashEnc, pin::State& st);
  uint8_t readPin(pin::State& out);
  // pinUvAuthParam / pinAuth on MakeCredential and GetAssertion: the touch
  // probe, the token check, or, without one, whether this request counts as
  // user-verified. Without a PIN that is "the vault is unlocked" (docs/FIDO.md).
  uint8_t userVerification(const cbor::Value* pinAuth, const cbor::Value* protocol, const uint8_t clientDataHash[32],
                           bool uvOption, bool makeCredential, User& user, bool& uv);
  uint8_t readHmacSecret(const cbor::Value* in, HmacSecret& out);
  // The encrypted hmac-secret output for f (credRandom set by rearm()).
  bool hmacSecretOutput(const HmacSecret& in, const Found& f, std::vector<uint8_t>& out);

  // Unwraps f's private key again right before signing: no key lives across a
  // wait for the button, and a vault locked in between refuses (kOperationDenied).
  // With hmac-secret (uv: which of its two CredRandoms) it also derives f.credRandom.
  uint8_t rearm(const uint8_t rpIdHash[32], Found& f, bool hmacSecret = false, bool uv = false);
  // allowList / excludeList entry, or a resident record: the credential if it is ours and alive.
  bool lookup(const WrapKeys& keys, const uint8_t rpIdHash[32], const std::vector<uint8_t>& id,
              const std::vector<cred::Resident>& residents, Found& out);
  bool residents(std::vector<cred::Resident>& out);
  uint8_t assertion(const Found& f, const uint8_t rpIdHash[32], const uint8_t clientDataHash[32], uint8_t flags,
                    bool includeUser, size_t count, const HmacSecret& hmac, std::vector<uint8_t>& out);
  bool sign(const uint8_t priv[32], const std::vector<uint8_t>& authData, const uint8_t* clientDataHash,
            std::vector<uint8_t>& der);

  uint16_t u2fRegister(const uint8_t* body, User& user, std::vector<uint8_t>& out);
  uint16_t u2fAuthenticate(uint8_t p1, const uint8_t* body, size_t lc, User& user, std::vector<uint8_t>& out);

  Crypto& crypto_;
  Store& store_;
  Counter& counter_;
  AttestationStore& attestation_;
  Next next_;
  struct KeyAgreement {
    bool ready = false;
    std::array<uint8_t, 32> priv{};
    std::array<uint8_t, 65> pub{};
  } ka_;
  // pinToken (CTAP 2.0) / pinUvAuthToken: random per successful getPinToken,
  // forgotten on lock, reset, a PIN change and power-off.
  struct Token {
    bool valid = false;
    std::array<uint8_t, 32> key{};
  } token_;
  uint8_t consecutiveWrong_ = 0;  // wrong PINs since power-up or the last right one
};

}  // namespace keyra::fido
