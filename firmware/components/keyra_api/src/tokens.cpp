#include "tokens.hpp"

#include <algorithm>

#include "validate.hpp"

namespace keyra::api::tokens {
namespace {

// tokens.bin plaintext, version 1 (little-endian):
//   u8 1 | u8 n | n × { u32 id | hash[32] | u8 kind | u8 all | i64 created | i64 lastUsed
//                       | u8 nameLen | name | u8 m | m × u32 entry id }
// kind: 0 agent, 1 app, 2 extension (added without a version change: records
// written before it hold only 0 and 1).
constexpr uint8_t kVersion = 1;
constexpr char kBase32[] = "abcdefghijklmnopqrstuvwxyz234567";

void put32(std::vector<uint8_t>& o, uint32_t v) {
  for (int i = 0; i < 4; ++i) o.push_back(static_cast<uint8_t>(v >> (8 * i)));
}
void put64(std::vector<uint8_t>& o, int64_t v) {
  for (int i = 0; i < 8; ++i) o.push_back(static_cast<uint8_t>(static_cast<uint64_t>(v) >> (8 * i)));
}

struct Reader {
  const uint8_t* p;
  size_t left;
  bool ok = true;
  bool take(size_t n) {
    if (!ok || left < n) return ok = false;
    left -= n;
    return true;
  }
  uint8_t u8() { return take(1) ? *p++ : 0; }
  uint64_t le(int bytes) {
    if (!take(static_cast<size_t>(bytes))) return 0;
    uint64_t v = 0;
    for (int i = 0; i < bytes; ++i) v |= uint64_t{p[i]} << (8 * i);
    p += bytes;
    return v;
  }
};

bool scopeValid(const Token& t) {
  if (t.scope.size() > kMaxScope) return false;
  return std::none_of(t.scope.begin(), t.scope.end(), [](uint32_t id) { return id == 0; });
}

}  // namespace

std::string format(const uint8_t raw[kSecretBytes]) {
  std::string out(kPrefix);
  // 20 bytes = 160 bits = exactly 32 five-bit groups.
  uint32_t buf = 0;
  int bits = 0;
  for (size_t i = 0; i < kSecretBytes; ++i) {
    buf = (buf << 8) | raw[i];
    bits += 8;
    while (bits >= 5) {
      out.push_back(kBase32[(buf >> (bits - 5)) & 31]);
      bits -= 5;
    }
  }
  buf = 0;
  return out;
}

bool wellFormed(std::string_view token) {
  if (token.size() != kTokenChars || token.substr(0, kPrefix.size()) != kPrefix) return false;
  return std::all_of(token.begin() + kPrefix.size(), token.end(),
                     [](char c) { return (c >= 'a' && c <= 'z') || (c >= '2' && c <= '7'); });
}

std::string_view bearer(std::string_view header) {
  constexpr std::string_view kScheme = "Bearer ";
  if (header.size() <= kScheme.size()) return {};
  for (size_t i = 0; i < kScheme.size(); ++i) {
    const char a = header[i], b = kScheme[i];
    if ((a | 0x20) != (b | 0x20)) return {};  // the scheme is case-insensitive (RFC 7235)
  }
  std::string_view t = header.substr(kScheme.size());
  while (!t.empty() && t.front() == ' ') t.remove_prefix(1);
  while (!t.empty() && t.back() == ' ') t.remove_suffix(1);
  return t;
}

const char* kindName(Kind k) {
  switch (k) {
    case Kind::App: return "app";
    case Kind::Extension: return "extension";
    case Kind::Agent: break;
  }
  return "agent";
}

std::optional<Kind> parseKind(std::string_view s) {
  if (s == "agent") return Kind::Agent;
  if (s == "app") return Kind::App;
  if (s == "extension") return Kind::Extension;
  return std::nullopt;
}

bool validName(std::string_view s) {
  if (s.empty() || s.size() > kMaxName || validate::utf8Length(s) < 0) return false;
  return std::none_of(s.begin(), s.end(), [](char c) {
    const auto u = static_cast<unsigned char>(c);
    return u < 0x20 || u == 0x7f;
  });
}

bool inScope(const Token& t, uint32_t entryId) {
  return entryId != 0 && (t.all || std::find(t.scope.begin(), t.scope.end(), entryId) != t.scope.end());
}

std::string owner(uint32_t tokenId) { return "token:" + std::to_string(tokenId); }

uint32_t ownerId(std::string_view owner) {
  constexpr std::string_view kTag = "token:";
  if (owner.size() <= kTag.size() || owner.size() > kTag.size() + 10 || owner.substr(0, kTag.size()) != kTag)
    return 0;
  uint64_t v = 0;
  for (char c : owner.substr(kTag.size())) {
    if (c < '0' || c > '9') return 0;
    v = v * 10 + static_cast<uint64_t>(c - '0');
  }
  return v <= UINT32_MAX ? static_cast<uint32_t>(v) : 0;
}

std::string urlHost(std::string_view url) {
  const size_t scheme = url.find("://");
  if (scheme != std::string_view::npos) url.remove_prefix(scheme + 3);
  url = url.substr(0, url.find_first_of("/?#"));
  const size_t at = url.rfind('@');
  if (at != std::string_view::npos) url.remove_prefix(at + 1);
  if (!url.empty() && url.front() == '[') {  // IPv6 literal: keep the brackets' content
    const size_t close = url.find(']');
    url = close == std::string_view::npos ? std::string_view() : url.substr(1, close - 1);
  } else {
    url = url.substr(0, url.find(':'));
  }
  std::string out;
  for (char c : url) {
    const auto u = static_cast<unsigned char>(c);
    if (u <= 0x20 || u == 0x7f) return {};  // not a host name: show nothing rather than guess
    out.push_back(c >= 'A' && c <= 'Z' ? static_cast<char>(c - 'A' + 'a') : c);
  }
  return out;
}

std::optional<size_t> Store::find(const Digest& hash) const {
  std::optional<size_t> found;
  for (size_t i = 0; i < list_.size(); ++i) {
    uint8_t diff = 0;
    for (size_t k = 0; k < hash.size(); ++k) diff |= static_cast<uint8_t>(list_[i].hash[k] ^ hash[k]);
    if (diff == 0) found = i;
  }
  return found;
}

bool Store::add(const Token& t) {
  if (full() || t.id == 0 || contains(t.id) || !scopeValid(t) || !validName(t.name)) return false;
  list_.push_back(t);
  return true;
}

bool Store::remove(uint32_t id) {
  const auto it = std::find_if(list_.begin(), list_.end(), [id](const Token& t) { return t.id == id; });
  if (it == list_.end()) return false;
  list_.erase(it);
  return true;
}

bool Store::contains(uint32_t id) const {
  return std::any_of(list_.begin(), list_.end(), [id](const Token& t) { return t.id == id; });
}

bool Store::touch(size_t index, int64_t now) {
  if (index >= list_.size() || now <= 0) return false;
  Token& t = list_[index];
  if (now - t.lastUsed < kTouchEverySec) return false;
  t.lastUsed = now;
  return true;
}

bool Store::addToScope(uint32_t tokenId, uint32_t entryId) {
  for (Token& t : list_) {
    if (t.id != tokenId) continue;
    if (t.all || inScope(t, entryId)) return true;
    if (entryId == 0 || t.scope.size() >= kMaxScope) return false;
    t.scope.push_back(entryId);
    return true;
  }
  return false;
}

std::vector<uint8_t> Store::serialize() const {
  std::vector<uint8_t> o = {kVersion, static_cast<uint8_t>(list_.size())};
  for (const Token& t : list_) {
    put32(o, t.id);
    o.insert(o.end(), t.hash.begin(), t.hash.end());
    o.push_back(static_cast<uint8_t>(t.kind));
    o.push_back(t.all ? 1 : 0);
    put64(o, t.created);
    put64(o, t.lastUsed);
    o.push_back(static_cast<uint8_t>(t.name.size()));  // add() keeps it ≤ kMaxName
    o.insert(o.end(), t.name.begin(), t.name.end());
    o.push_back(static_cast<uint8_t>(t.scope.size()));
    for (uint32_t id : t.scope) put32(o, id);
  }
  return o;
}

std::optional<Store> Store::parse(const uint8_t* data, size_t len) {
  Store s;
  if (len == 0) return s;  // no record yet
  Reader r{data, len};
  if (r.u8() != kVersion) return std::nullopt;
  const uint8_t count = r.u8();
  if (!r.ok || count > kMaxTokens) return std::nullopt;
  for (uint8_t i = 0; i < count; ++i) {
    Token t;
    t.id = static_cast<uint32_t>(r.le(4));
    if (!r.take(t.hash.size())) return std::nullopt;
    std::copy(r.p, r.p + t.hash.size(), t.hash.begin());
    r.p += t.hash.size();
    const uint8_t kind = r.u8(), all = r.u8();
    if (kind > static_cast<uint8_t>(Kind::Extension) || all > 1) return std::nullopt;
    t.kind = static_cast<Kind>(kind);
    t.all = all == 1;
    t.created = static_cast<int64_t>(r.le(8));
    t.lastUsed = static_cast<int64_t>(r.le(8));
    const uint8_t n = r.u8();
    if (!r.ok || n > kMaxName || !r.take(n)) return std::nullopt;
    t.name.assign(reinterpret_cast<const char*>(r.p), n);
    r.p += n;
    const uint8_t m = r.u8();
    if (!r.ok || m > kMaxScope) return std::nullopt;
    for (uint8_t k = 0; k < m; ++k) t.scope.push_back(static_cast<uint32_t>(r.le(4)));
    if (!r.ok || !s.add(t)) return std::nullopt;
  }
  if (!r.ok || r.left != 0) return std::nullopt;
  return s;
}

bool RateLimit::allow(uint32_t key, int64_t nowMs, int64_t& retryAfterMs) {
  retryAfterMs = 0;
  Bucket* b = nullptr;
  for (Bucket& x : buckets_) {
    if (x.used && x.key == key) b = &x;
  }
  if (!b) {
    // A free bucket, else the one idle longest (a revoked token's).
    b = &buckets_[0];
    for (Bucket& x : buckets_) {
      if (!x.used) {
        b = &x;
        break;
      }
      if (x.lastMs < b->lastMs) b = &x;
    }
    *b = Bucket{};
    b->used = true;
    b->key = key;
  }
  b->lastMs = nowMs;
  if (b->count == kMax) {
    const int64_t oldest = b->at[b->next];  // the ring is full: next holds the oldest
    if (nowMs - oldest < kWindowMs) {
      retryAfterMs = oldest + kWindowMs - nowMs;
      return false;
    }
  } else {
    ++b->count;
  }
  b->at[b->next] = nowMs;
  b->next = (b->next + 1) % kMax;
  return true;
}

void RateLimit::forget(uint32_t key) {
  for (Bucket& x : buckets_) {
    if (x.used && x.key == key) x = Bucket{};
  }
}

}  // namespace keyra::api::tokens
