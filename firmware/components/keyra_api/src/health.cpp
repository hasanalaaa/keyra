#include "health.hpp"

#include <algorithm>
#include <array>
#include <cctype>
#include <cmath>
#include <string>

namespace keyra::api::health {
namespace {

// Kept equal to COMMON in web/src/lib/strength.ts.
constexpr std::array<std::string_view, 51> kCommon = {
    "password", "123456", "12345678", "123456789", "1234567890", "qwerty", "qwertyuiop", "keyra1234",
    "iloveyou", "admin", "welcome", "letmein", "monkey", "dragon", "football", "baseball", "abc123",
    "111111", "000000", "123123", "654321", "sunshine", "princess", "master", "shadow", "superman",
    "trustno1", "passw0rd", "password1", "password123", "qwerty123", "1q2w3e4r", "zaq12wsx", "starwars",
    "whatever", "freedom", "hello123", "login", "access", "secret", "michael", "charlie", "jordan",
    "mustang", "batman", "computer", "internet", "samsung", "google", "asdfghjkl",
};

// Code points of well-formed UTF-8; a stray byte counts as itself (the vault
// only stores valid UTF-8, so that never happens in practice).
std::vector<uint32_t> codePoints(std::string_view s) {
  std::vector<uint32_t> out;
  for (size_t i = 0; i < s.size();) {
    const auto c = static_cast<unsigned char>(s[i]);
    size_t n = c < 0x80 ? 1 : (c & 0xE0) == 0xC0 ? 2 : (c & 0xF0) == 0xE0 ? 3 : (c & 0xF8) == 0xF0 ? 4 : 1;
    if (i + n > s.size()) n = 1;
    uint32_t cp = n == 1 ? c : c & (0x7F >> n);
    for (size_t k = 1; k < n; ++k) cp = cp << 6 | (static_cast<unsigned char>(s[i + k]) & 0x3F);
    out.push_back(cp);
    i += n;
  }
  return out;
}

bool asciiSymbol(uint32_t c) {
  return c == ' ' || (c >= '!' && c <= '/') || (c >= ':' && c <= '@') || (c >= '[' && c <= '`') ||
         (c >= '{' && c <= '~');
}

uint32_t lower(uint32_t c) { return c >= 'A' && c <= 'Z' ? c + 32 : c; }

bool common(std::string_view pw) {
  if (pw.size() > 16) return false;
  std::string l(pw);
  for (char& ch : l) ch = static_cast<char>(std::tolower(static_cast<unsigned char>(ch)));
  return std::find(kCommon.begin(), kCommon.end(), l) != kCommon.end();
}

}  // namespace

double bits(std::string_view password) {
  if (password.empty()) return 0;
  const std::vector<uint32_t> cp = codePoints(password);
  bool lowerC = false, upperC = false, digit = false, symbol = false, arabic = false;
  for (uint32_t c : cp) {
    lowerC |= c >= 'a' && c <= 'z';
    upperC |= c >= 'A' && c <= 'Z';
    digit |= c >= '0' && c <= '9';
    symbol |= asciiSymbol(c);
    arabic |= c >= 0x0600 && c <= 0x06FF;
  }
  int pool = (lowerC ? 26 : 0) + (upperC ? 26 : 0) + (digit ? 10 : 0) + (symbol ? 33 : 0) + (arabic ? 36 : 0);
  if (pool == 0) pool = 33;  // other scripts: treat like symbols
  double b = static_cast<double>(cp.size()) * std::log2(pool);
  // Penalties: runs of the same character (≥ 3) and ascending/descending sequences (abc, 321).
  int run = 1, seq = 1;
  int64_t prev = 0;
  for (size_t i = 1; i < cp.size(); ++i) {
    const int64_t d = static_cast<int64_t>(lower(cp[i])) - static_cast<int64_t>(lower(cp[i - 1]));
    run = d == 0 ? run + 1 : 1;
    if (run == 3) b -= 8;
    if (d == 1 || d == -1) seq = seq > 1 && d == prev ? seq + 1 : 2;
    else seq = 1;
    if (seq == 3) b -= 8;
    prev = d;
  }
  return std::max(0.0, b);
}

int level(std::string_view password) {
  if (password.empty()) return 0;
  if (common(password)) return 1;
  const double b = bits(password);
  if (b < 36) return 1;
  if (b < 60) return 2;
  if (b < 80) return 3;
  return 4;
}

Report check(const std::vector<Item>& items, int64_t now) {
  Report r;
  std::vector<const Item*> with;
  for (const Item& it : items) {
    if (it.password.empty()) continue;
    with.push_back(&it);
    const int lv = level(it.password);
    if (lv < kWeakBelow) r.weak.emplace_back(it.id, lv);
    if (now > 0 && it.setAt > 0 && now - it.setAt > kOldAfterSec) r.old.emplace_back(it.id, it.setAt);
  }
  r.checked = with.size();
  // Sorting pointers groups equal passwords without copying any of them.
  std::sort(with.begin(), with.end(), [](const Item* a, const Item* b) {
    return a->password != b->password ? a->password < b->password : a->id < b->id;
  });
  for (size_t i = 0; i < with.size();) {
    size_t j = i + 1;
    while (j < with.size() && with[j]->password == with[i]->password) ++j;
    if (j - i > 1) {
      std::vector<uint32_t> g;
      for (size_t k = i; k < j; ++k) g.push_back(with[k]->id);
      r.reused.push_back(std::move(g));
    }
    i = j;
  }
  return r;
}

}  // namespace keyra::api::health
