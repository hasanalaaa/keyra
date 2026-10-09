#pragma once
// NFC tap tags (SPEC §18): a tag holds a URL on Keyra; opening it arms one
// account for typing, never reads one. Simple tags carry a static secret
// (only its SHA-256 is kept); secure tags are NTAG 424 DNA with SUN messages
// (NXP AN12196: encrypted PICCData + SDMMAC, rolling counter). Plain C++,
// host-tested; AES comes in as a one-block primitive, storage and HTTP live
// in handlers_tags.cpp.
#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace keyra::api::tags {

constexpr size_t kMaxTags = 16;
constexpr size_t kMaxName = 48;      // bytes of UTF-8
constexpr size_t kSecretBytes = 15;  // 120 bits from the hardware RNG
constexpr size_t kSecretChars = 24;  // base32 of kSecretBytes
constexpr size_t kUidBytes = 7;
constexpr std::string_view kHost = "keyra.local";
constexpr int64_t kTouchEverySec = 60;  // simple tags: lastUsed written at most this often

using Digest = std::array<uint8_t, 32>;
using Key = std::array<uint8_t, 16>;
using Uid = std::array<uint8_t, kUidBytes>;

// Stored values: append only.
enum class Kind : uint8_t { Simple = 0, Secure = 1 };
enum class What : uint8_t { Username = 0, Password = 1, Both = 2, Totp = 3 };

struct Tag {
  uint32_t id = 0;  // random, non-zero; in the URL and DELETE /api/tags/{id}
  Kind kind = Kind::Simple;
  std::string name;
  uint32_t entry = 0;
  What what = What::Both;
  std::string target;  // "" = the device's default, "usb", or a bond address "AA:BB:CC:DD:EE:FF"
  int64_t created = 0, lastUsed = 0;  // unix seconds, 0 = unknown / never
  Digest hash{};                      // Simple: SHA-256 of the secret
  Key metaKey{}, fileKey{};           // Secure: SDM meta-read key (PICCData), SDM file-read key (MAC)
  bool bound = false;                 // Secure: a good tap was seen; uid and counter are its
  Uid uid{};
  uint32_t counter = 0;  // Secure: SDMReadCtr of the last accepted tap (24 bits)
};

const char* kindName(Kind k);  // "simple" | "secure"
std::optional<Kind> parseKind(std::string_view s);
const char* whatName(What w);  // "username" | "password" | "both" | "totp"
std::optional<What> parseWhat(std::string_view s);
bool validName(std::string_view s);    // like an access token's name
bool validTarget(std::string_view s);  // "", "usb" or "XX:XX:XX:XX:XX:XX" (hex)

// Base32 (RFC 4648, lowercase, no padding) of a simple tag's secret.
std::string formatSecret(const uint8_t raw[kSecretBytes]);
bool wellFormedSecret(std::string_view s);
// What to write on the tag: "http://keyra.local/t/<id>/<secret>", or for a
// secure tag the SDM template with zeros where the tag mirrors its data:
// "http://keyra.local/t/<id>?p=<32 zeros>&m=<16 zeros>".
std::string simpleUrl(uint32_t id, std::string_view secret);
std::string secureUrl(uint32_t id);

// Who owns an item a tag armed in the pending machine; 0 back for anything else.
std::string owner(uint32_t tagId);
uint32_t ownerId(std::string_view owner);

// Exactly 2n hex digits (either case) into n bytes.
bool parseHex(std::string_view s, uint8_t* out, size_t n);

// One AES-128 block, encrypt or decrypt (ECB); false on a crypto failure.
using Block = bool (*)(const uint8_t key[16], const uint8_t in[16], uint8_t out[16]);
struct Cipher {
  Block encrypt;
  Block decrypt;
};
// AES-CMAC (RFC 4493) over the encrypt primitive.
bool cmac(Block encrypt, const uint8_t key[16], const uint8_t* msg, size_t len, uint8_t out[16]);

// A secure tag's tap (AN12196 §3.3–3.4) as the phone sent it.
struct Sun {
  uint8_t picc[16];  // encrypted PICCData (the URL's p=)
  uint8_t mac[8];    // SDMMAC (the URL's m=)
};
enum class Verdict { Ok, BadMac, WrongUid, Replayed, Crypto };
// Decrypts PICCData with the meta-read key (tag byte 0xC7: UID and counter
// mirrored, 7-byte UID), derives the session MAC key from SV2 with the
// file-read key, checks SDMMAC over empty input (SDMMACInputOffset =
// SDMMACOffset), then the bound UID and that the counter grew. On Ok, `uid`
// and `counter` are the tap's (accept() records them).
Verdict verifySun(const Tag& t, const Cipher& c, const Sun& sun, Uid& uid, uint32_t& counter);
void accept(Tag& t, const Uid& uid, uint32_t counter);
// Constant-time check of a simple tag's secret digest.
bool secretMatches(const Tag& t, const Digest& hash);

class Store {
 public:
  const std::vector<Tag>& all() const { return list_; }
  Tag* find(uint32_t id);
  bool full() const { return list_.size() >= kMaxTags; }
  bool add(const Tag& t);  // false when full, the id is 0 or taken, or a field is invalid
  bool remove(uint32_t id);
  bool contains(uint32_t id) const;

  // Versioned record. parse() rejects anything malformed rather than guess.
  std::vector<uint8_t> serialize() const;
  static std::optional<Store> parse(const uint8_t* data, size_t len);

 private:
  std::vector<Tag> list_;
};

}  // namespace keyra::api::tags
