#include "tags.hpp"

#include <algorithm>
#include <cstring>

#include "tokens.hpp"

namespace keyra::api::tags {
namespace {

// tags.bin plaintext, version 1 (little-endian):
//   u8 1 | u8 n | n × { u32 id | u8 kind | u8 what | u32 entry | i64 created | i64 lastUsed
//                       | u8 nameLen | name | u8 targetLen | target
//                       | Simple: hash[32]
//                       | Secure: metaKey[16] | fileKey[16] | u8 bound | uid[7] | u32 counter }
constexpr uint8_t kVersion = 1;
constexpr char kBase32[] = "abcdefghijklmnopqrstuvwxyz234567";
constexpr uint8_t kPiccTag = 0xC7;  // UID mirrored, SDMReadCtr mirrored, UID length 7

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
  bool bytes(uint8_t* out, size_t n) {
    if (!take(n)) return false;
    std::memcpy(out, p, n);
    p += n;
    return true;
  }
  bool str(std::string& out, size_t max) {
    const uint8_t n = u8();
    if (!ok || n > max || !take(n)) return ok = false;
    out.assign(reinterpret_cast<const char*>(p), n);
    p += n;
    return true;
  }
};

bool isHex(char c) { return (c >= '0' && c <= '9') || (c >= 'a' && c <= 'f') || (c >= 'A' && c <= 'F'); }
uint8_t hexVal(char c) {
  return static_cast<uint8_t>(c <= '9' ? c - '0' : (c | 0x20) - 'a' + 10);
}

// RFC 4493 §2.3: shift left one bit, xor 0x87 when the top bit fell off.
void subkey(const uint8_t in[16], uint8_t out[16]) {
  const bool carry = in[0] & 0x80;
  for (int i = 0; i < 15; ++i) out[i] = static_cast<uint8_t>((in[i] << 1) | (in[i + 1] >> 7));
  out[15] = static_cast<uint8_t>(in[15] << 1);
  if (carry) out[15] ^= 0x87;
}

bool constantEqual(const uint8_t* a, const uint8_t* b, size_t n) {
  uint8_t diff = 0;
  for (size_t i = 0; i < n; ++i) diff |= static_cast<uint8_t>(a[i] ^ b[i]);
  return diff == 0;
}

void wipe(uint8_t* p, size_t n) {
  volatile uint8_t* v = p;
  while (n--) *v++ = 0;
}

}  // namespace

const char* kindName(Kind k) { return k == Kind::Secure ? "secure" : "simple"; }

std::optional<Kind> parseKind(std::string_view s) {
  if (s == "simple") return Kind::Simple;
  if (s == "secure") return Kind::Secure;
  return std::nullopt;
}

const char* whatName(What w) {
  switch (w) {
    case What::Username: return "username";
    case What::Password: return "password";
    case What::Totp: return "totp";
    case What::Both: break;
  }
  return "both";
}

std::optional<What> parseWhat(std::string_view s) {
  for (What w : {What::Username, What::Password, What::Both, What::Totp}) {
    if (s == whatName(w)) return w;
  }
  return std::nullopt;
}

bool validName(std::string_view s) { return tokens::validName(s); }

bool validTarget(std::string_view s) {
  if (s.empty() || s == "usb") return true;
  if (s.size() != 17) return false;
  for (size_t i = 0; i < s.size(); ++i) {
    if (i % 3 == 2 ? s[i] != ':' : !isHex(s[i])) return false;
  }
  return true;
}

std::string formatSecret(const uint8_t raw[kSecretBytes]) {
  std::string out;
  uint32_t buf = 0;
  int bits = 0;
  for (size_t i = 0; i < kSecretBytes; ++i) {  // 15 bytes = 120 bits = exactly 24 groups
    buf = (buf << 8) | raw[i];
    bits += 8;
    while (bits >= 5) {
      out.push_back(kBase32[(buf >> (bits - 5)) & 31]);
      bits -= 5;
    }
  }
  return out;
}

bool wellFormedSecret(std::string_view s) {
  return s.size() == kSecretChars &&
         std::all_of(s.begin(), s.end(), [](char c) { return (c >= 'a' && c <= 'z') || (c >= '2' && c <= '7'); });
}

std::string simpleUrl(uint32_t id, std::string_view secret) {
  return "http://" + std::string(kHost) + "/t/" + std::to_string(id) + "/" + std::string(secret);
}

std::string secureUrl(uint32_t id) {
  return "http://" + std::string(kHost) + "/t/" + std::to_string(id) + "?p=" + std::string(32, '0') + "&m=" +
         std::string(16, '0');
}

std::string owner(uint32_t tagId) { return "tag:" + std::to_string(tagId); }

uint32_t ownerId(std::string_view owner) {
  constexpr std::string_view kTag = "tag:";
  if (owner.size() <= kTag.size() || owner.size() > kTag.size() + 10 || owner.substr(0, kTag.size()) != kTag)
    return 0;
  uint64_t v = 0;
  for (char c : owner.substr(kTag.size())) {
    if (c < '0' || c > '9') return 0;
    v = v * 10 + static_cast<uint64_t>(c - '0');
  }
  return v <= UINT32_MAX ? static_cast<uint32_t>(v) : 0;
}

bool parseHex(std::string_view s, uint8_t* out, size_t n) {
  if (s.size() != 2 * n || !std::all_of(s.begin(), s.end(), isHex)) return false;
  for (size_t i = 0; i < n; ++i) out[i] = static_cast<uint8_t>(hexVal(s[2 * i]) << 4 | hexVal(s[2 * i + 1]));
  return true;
}

bool cmac(Block encrypt, const uint8_t key[16], const uint8_t* msg, size_t len, uint8_t out[16]) {
  uint8_t l[16] = {}, k1[16], k2[16];
  if (!encrypt(key, l, l)) return false;
  subkey(l, k1);
  subkey(k1, k2);
  const size_t blocks = len == 0 ? 1 : (len + 15) / 16;
  const bool whole = len != 0 && len % 16 == 0;
  uint8_t x[16] = {}, last[16] = {};
  const size_t tail = (blocks - 1) * 16;
  for (size_t i = 0; i < len - tail; ++i) last[i] = msg[tail + i];
  if (!whole) last[len - tail] = 0x80;
  for (int i = 0; i < 16; ++i) last[i] ^= whole ? k1[i] : k2[i];
  bool ok = true;
  for (size_t b = 0; ok && b + 1 < blocks; ++b) {
    for (int i = 0; i < 16; ++i) x[i] ^= msg[16 * b + i];
    ok = encrypt(key, x, x);
  }
  for (int i = 0; i < 16; ++i) x[i] ^= last[i];
  ok = ok && encrypt(key, x, out);
  wipe(l, 16), wipe(k1, 16), wipe(k2, 16), wipe(x, 16), wipe(last, 16);
  return ok;
}

Verdict verifySun(const Tag& t, const Cipher& c, const Sun& sun, Uid& uid, uint32_t& counter) {
  if (t.kind != Kind::Secure) return Verdict::BadMac;
  uint8_t plain[16];
  if (!c.decrypt(t.metaKey.data(), sun.picc, plain)) return Verdict::Crypto;
  // A wrong meta-read key decrypts to noise: the tag byte is the first check.
  const bool shape = plain[0] == kPiccTag;
  std::copy(plain + 1, plain + 1 + kUidBytes, uid.begin());
  counter = uint32_t{plain[8]} | uint32_t{plain[9]} << 8 | uint32_t{plain[10]} << 16;
  // SV2 = 3CC3 0001 0080 ‖ UID ‖ SDMReadCtr (LSB first): exactly one block.
  uint8_t sv2[16] = {0x3C, 0xC3, 0x00, 0x01, 0x00, 0x80};
  std::memcpy(sv2 + 6, plain + 1, kUidBytes + 3);
  wipe(plain, sizeof plain);
  uint8_t session[16], full[16];
  bool ok = cmac(c.encrypt, t.fileKey.data(), sv2, sizeof sv2, session) &&
            cmac(c.encrypt, session, nullptr, 0, full);
  wipe(session, sizeof session);
  if (!ok) return Verdict::Crypto;
  uint8_t mac[8];
  for (int i = 0; i < 8; ++i) mac[i] = full[2 * i + 1];  // MACt: the odd-numbered bytes
  const bool macOk = constantEqual(mac, sun.mac, sizeof mac);
  if (!shape || !macOk) return Verdict::BadMac;
  if (t.bound && uid != t.uid) return Verdict::WrongUid;
  if (t.bound && counter <= t.counter) return Verdict::Replayed;
  return Verdict::Ok;
}

void accept(Tag& t, const Uid& uid, uint32_t counter) {
  t.bound = true;
  t.uid = uid;
  t.counter = counter;
}

bool secretMatches(const Tag& t, const Digest& hash) {
  return t.kind == Kind::Simple && constantEqual(t.hash.data(), hash.data(), hash.size());
}

Tag* Store::find(uint32_t id) {
  for (Tag& t : list_) {
    if (t.id == id) return &t;
  }
  return nullptr;
}

bool Store::add(const Tag& t) {
  if (full() || t.id == 0 || t.entry == 0 || contains(t.id) || !validName(t.name) || !validTarget(t.target) ||
      t.counter > 0xFFFFFF)
    return false;
  list_.push_back(t);
  return true;
}

bool Store::remove(uint32_t id) {
  const auto it = std::find_if(list_.begin(), list_.end(), [id](const Tag& t) { return t.id == id; });
  if (it == list_.end()) return false;
  list_.erase(it);
  return true;
}

bool Store::contains(uint32_t id) const {
  return std::any_of(list_.begin(), list_.end(), [id](const Tag& t) { return t.id == id; });
}

std::vector<uint8_t> Store::serialize() const {
  std::vector<uint8_t> o = {kVersion, static_cast<uint8_t>(list_.size())};
  for (const Tag& t : list_) {
    put32(o, t.id);
    o.push_back(static_cast<uint8_t>(t.kind));
    o.push_back(static_cast<uint8_t>(t.what));
    put32(o, t.entry);
    put64(o, t.created);
    put64(o, t.lastUsed);
    o.push_back(static_cast<uint8_t>(t.name.size()));  // add() keeps it ≤ kMaxName
    o.insert(o.end(), t.name.begin(), t.name.end());
    o.push_back(static_cast<uint8_t>(t.target.size()));
    o.insert(o.end(), t.target.begin(), t.target.end());
    if (t.kind == Kind::Simple) {
      o.insert(o.end(), t.hash.begin(), t.hash.end());
    } else {
      o.insert(o.end(), t.metaKey.begin(), t.metaKey.end());
      o.insert(o.end(), t.fileKey.begin(), t.fileKey.end());
      o.push_back(t.bound ? 1 : 0);
      o.insert(o.end(), t.uid.begin(), t.uid.end());
      put32(o, t.counter);
    }
  }
  return o;
}

std::optional<Store> Store::parse(const uint8_t* data, size_t len) {
  Store s;
  if (len == 0) return s;  // no record yet
  Reader r{data, len};
  if (r.u8() != kVersion) return std::nullopt;
  const uint8_t count = r.u8();
  if (!r.ok || count > kMaxTags) return std::nullopt;
  for (uint8_t i = 0; i < count; ++i) {
    Tag t;
    t.id = static_cast<uint32_t>(r.le(4));
    const uint8_t kind = r.u8(), what = r.u8();
    if (kind > static_cast<uint8_t>(Kind::Secure) || what > static_cast<uint8_t>(What::Totp)) return std::nullopt;
    t.kind = static_cast<Kind>(kind);
    t.what = static_cast<What>(what);
    t.entry = static_cast<uint32_t>(r.le(4));
    t.created = static_cast<int64_t>(r.le(8));
    t.lastUsed = static_cast<int64_t>(r.le(8));
    if (!r.str(t.name, kMaxName) || !r.str(t.target, 17)) return std::nullopt;
    if (t.kind == Kind::Simple) {
      r.bytes(t.hash.data(), t.hash.size());
    } else {
      r.bytes(t.metaKey.data(), t.metaKey.size());
      r.bytes(t.fileKey.data(), t.fileKey.size());
      const uint8_t bound = r.u8();
      if (bound > 1) return std::nullopt;
      t.bound = bound == 1;
      r.bytes(t.uid.data(), t.uid.size());
      t.counter = static_cast<uint32_t>(r.le(4));
    }
    if (!r.ok || !s.add(t)) return std::nullopt;
  }
  if (!r.ok || r.left != 0) return std::nullopt;
  return s;
}

}  // namespace keyra::api::tags
