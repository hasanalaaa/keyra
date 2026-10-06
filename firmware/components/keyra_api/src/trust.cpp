#include "trust.hpp"

#include <algorithm>

namespace keyra::api::trust {
namespace {

constexpr uint8_t kVersion = 1;

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
  uint8_t u8() {
    if (!take(1)) return 0;
    return *p++;
  }
  uint64_t le(int bytes) {
    if (!take(static_cast<size_t>(bytes))) return 0;
    uint64_t v = 0;
    for (int i = 0; i < bytes; ++i) v |= uint64_t{p[i]} << (8 * i);
    p += bytes;
    return v;
  }
};

bool has(std::string_view s, std::string_view part) { return s.find(part) != std::string_view::npos; }

}  // namespace

std::optional<size_t> Store::find(const Digest& hash) const {
  std::optional<size_t> found;
  for (size_t i = 0; i < list_.size(); ++i) {
    uint8_t diff = 0;
    for (size_t k = 0; k < hash.size(); ++k) diff |= static_cast<uint8_t>(list_[i].hash[k] ^ hash[k]);
    if (diff == 0) found = i;
  }
  return found;
}

uint32_t Store::add(const Browser& b) {
  uint32_t evicted = 0;
  if (list_.size() >= kMaxBrowsers) {
    auto oldest = std::min_element(list_.begin(), list_.end(), [](const Browser& x, const Browser& y) {
      return std::max(x.lastSeen, x.created) < std::max(y.lastSeen, y.created);
    });
    evicted = oldest->id;
    list_.erase(oldest);
  }
  list_.push_back(b);
  return evicted;
}

bool Store::remove(uint32_t id) {
  const auto it = std::find_if(list_.begin(), list_.end(), [id](const Browser& b) { return b.id == id; });
  if (it == list_.end()) return false;
  list_.erase(it);
  return true;
}

bool Store::contains(uint32_t id) const {
  return std::any_of(list_.begin(), list_.end(), [id](const Browser& b) { return b.id == id; });
}

void Store::touch(size_t index, int64_t now) {
  if (index < list_.size() && now != 0) list_[index].lastSeen = now;
}

std::vector<uint8_t> Store::serialize() const {
  std::vector<uint8_t> o = {kVersion, static_cast<uint8_t>(list_.size())};
  for (const Browser& b : list_) {
    put32(o, b.id);
    o.insert(o.end(), b.hash.begin(), b.hash.end());
    put64(o, b.created);
    put64(o, b.lastSeen);
    const size_t n = std::min(b.name.size(), kMaxName);
    o.push_back(static_cast<uint8_t>(n));
    o.insert(o.end(), b.name.begin(), b.name.begin() + static_cast<std::ptrdiff_t>(n));
  }
  return o;
}

std::optional<Store> Store::parse(const uint8_t* data, size_t len) {
  Reader r{data, len};
  if (r.u8() != kVersion) return std::nullopt;
  const uint8_t count = r.u8();
  if (!r.ok || count > kMaxBrowsers) return std::nullopt;
  Store s;
  for (uint8_t i = 0; i < count; ++i) {
    Browser b;
    b.id = static_cast<uint32_t>(r.le(4));
    if (!r.take(b.hash.size())) return std::nullopt;
    std::copy(r.p, r.p + b.hash.size(), b.hash.begin());
    r.p += b.hash.size();
    b.created = static_cast<int64_t>(r.le(8));
    b.lastSeen = static_cast<int64_t>(r.le(8));
    const uint8_t n = r.u8();
    if (!r.ok || n > kMaxName || !r.take(n)) return std::nullopt;
    b.name.assign(reinterpret_cast<const char*>(r.p), n);
    r.p += n;
    if (b.id == 0 || s.contains(b.id)) return std::nullopt;
    s.list_.push_back(std::move(b));
  }
  if (!r.ok || r.left != 0) return std::nullopt;
  return s;
}

std::string browserName(std::string_view ua) {
  // Order matters: Edge and Opera also say "Chrome"; Chrome also says "Safari".
  const char* browser = has(ua, "Edg") ? "Edge"
                        : has(ua, "OPR/") ? "Opera"
                        : has(ua, "Firefox/") || has(ua, "FxiOS/") ? "Firefox"
                        : has(ua, "SamsungBrowser/") ? "Samsung Internet"
                        : has(ua, "CriOS/") || has(ua, "Chrome/") ? "Chrome"
                        : has(ua, "Safari/") ? "Safari"
                                             : nullptr;
  const char* platform = has(ua, "iPhone") ? "iPhone"
                         : has(ua, "iPad") ? "iPad"
                         : has(ua, "Android") ? "Android"
                         : has(ua, "CrOS") ? "ChromeOS"
                         : has(ua, "Macintosh") ? "Mac"
                         : has(ua, "Windows") ? "Windows"
                         : has(ua, "Linux") ? "Linux"
                                            : nullptr;
  if (browser && platform) return std::string(browser) + " on " + platform;
  if (browser) return browser;
  if (platform) return std::string("Browser on ") + platform;
  std::string out;
  for (char c : ua) {
    if (out.size() >= kMaxName) break;
    if (c >= 0x20 && c <= 0x7E) out += c;
  }
  return out.empty() ? "Browser" : out;
}

}  // namespace keyra::api::trust
