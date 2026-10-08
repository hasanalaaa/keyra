#pragma once
// Access tokens (SPEC §17): bearer tokens for apps and AI agents that may list
// titles and arm typing, never read a secret. Only SHA-256(token) and metadata
// are kept, in the vault's sealed tokens.bin. Plain C++, host-tested; hashing,
// randomness, storage and HTTP live in handlers_agent.cpp.
#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace keyra::api::tokens {

constexpr size_t kMaxTokens = 8;
constexpr size_t kMaxName = 48;   // bytes of UTF-8
constexpr size_t kMaxScope = 32;  // entry ids per token
constexpr size_t kSecretBytes = 20;  // 160 bits from the hardware RNG
constexpr std::string_view kPrefix = "keyra_";
constexpr size_t kTokenChars = 6 + 32;  // prefix + base32 of kSecretBytes
// lastUsed is written back at most this often (each write is a flash write).
constexpr int64_t kTouchEverySec = 60;

using Digest = std::array<uint8_t, 32>;

// Stored values: append only.
enum class Kind : uint8_t { Agent = 0, App = 1 };

struct Token {
  uint32_t id = 0;  // random, non-zero; the {id} of DELETE /api/tokens/{id}
  Digest hash{};    // SHA-256 of the whole token string
  Kind kind = Kind::Agent;
  std::string name;
  bool all = true;               // scope: every entry, else exactly `scope`
  std::vector<uint32_t> scope;   // entry ids, ≤ kMaxScope
  int64_t created = 0, lastUsed = 0;  // unix seconds, 0 = unknown / never
};

// "keyra_" + RFC 4648 base32 (lowercase, no padding) of the secret.
std::string format(const uint8_t raw[kSecretBytes]);
// Shape check only (prefix, length, alphabet): cheap refusal before hashing.
bool wellFormed(std::string_view token);
// The token of an `Authorization: Bearer <token>` header value, or empty.
std::string_view bearer(std::string_view header);

const char* kindName(Kind k);  // "agent" | "app"
std::optional<Kind> parseKind(std::string_view s);
// 1–kMaxName bytes of well-formed UTF-8 without control characters.
bool validName(std::string_view s);

bool inScope(const Token& t, uint32_t entryId);
// Saving a new account and generating a password are for apps only.
inline bool mayWrite(const Token& t) { return t.kind == Kind::App; }
// Who owns an item this token armed in the pending machine (never a session token).
std::string owner(uint32_t tokenId);
// The token id of such an owner; 0 for anything else (a session, nobody).
uint32_t ownerId(std::string_view owner);
// What GET /api/agent/entries shows of an entry's URL: its host name only,
// lowercase, without scheme, user info, port, path or query ("" when none).
std::string urlHost(std::string_view url);

class Store {
 public:
  const std::vector<Token>& all() const { return list_; }
  // Index of the token with this hash, comparing against every record so
  // timing does not reveal which one matched or how much of it.
  std::optional<size_t> find(const Digest& hash) const;
  bool full() const { return list_.size() >= kMaxTokens; }
  bool add(const Token& t);  // false when full, the id is 0 or taken, or the scope is too long
  bool remove(uint32_t id);
  bool contains(uint32_t id) const;
  // Records a use; true when lastUsed moved by ≥ kTouchEverySec (worth saving).
  bool touch(size_t index, int64_t now);
  // A scoped token gets an entry it saved added to its scope; false when it
  // is unknown or the scope is full (all-entries tokens need nothing).
  bool addToScope(uint32_t tokenId, uint32_t entryId);

  // Versioned record. parse() rejects anything malformed rather than guess.
  std::vector<uint8_t> serialize() const;
  static std::optional<Store> parse(const uint8_t* data, size_t len);

 private:
  std::vector<Token> list_;
};

// Sliding-window limit: at most kMax requests in any kWindowMs per key (a
// token id; 0 = all unrecognised tokens together).
class RateLimit {
 public:
  static constexpr size_t kMax = 10;
  static constexpr int64_t kWindowMs = 10000;
  // True and counted when allowed; else false with the wait in retryAfterMs.
  bool allow(uint32_t key, int64_t nowMs, int64_t& retryAfterMs);
  void forget(uint32_t key);

 private:
  struct Bucket {
    bool used = false;
    uint32_t key = 0;
    std::array<int64_t, kMax> at{};  // ring of the last kMax request times
    size_t count = 0, next = 0;
    int64_t lastMs = 0;
  };
  std::array<Bucket, kMaxTokens + 1> buckets_{};
};

}  // namespace keyra::api::tokens
