#include "policy.hpp"

#include <cstdio>

namespace keyra::ble {

Adv advertising(bool enabled, bool pairing, size_t bonds, bool linked, bool guest, Connect mode, bool wanted) {
  if (!enabled || guest) return Adv::Off;
  if (pairing) return Adv::Open;
  if (linked) return Adv::Off;
  if (wanted) return Adv::BondedOnly;
  return mode == Connect::Always && bonds > 0 ? Adv::BondedOnly : Adv::Off;
}

bool keepLink(Connect mode, bool pairing, bool trusted, const std::optional<Addr>& wanted, const Addr& peer) {
  if (!trusted) return pairing;
  if (wanted) return *wanted == peer;
  return mode == Connect::Always || pairing;
}

bool guestTakesOver(bool fresh, const std::optional<Addr>& wanted, const Addr& linked, const Addr& guest) {
  // An armed action decides; otherwise only a host the user has just paired
  // (they are holding it) displaces the linked one.
  if (wanted && *wanted == guest) return true;
  if (wanted && *wanted == linked) return false;
  return fresh;
}

bool mayPair(bool windowOpen, bool known, size_t bonds) {
  if (!windowOpen) return false;
  return known || bonds < kMaxBonds;
}

bool mayConnect(bool enabled, bool windowOpen, bool known) { return enabled && (windowOpen || known); }

std::string_view fitName(std::string_view name, size_t maxBytes) {
  if (name.size() <= maxBytes) return name;
  size_t n = maxBytes;
  // Back up over continuation bytes (10xxxxxx) so the cut lands on a lead byte.
  while (n > 0 && (static_cast<unsigned char>(name[n]) & 0xC0) == 0x80) --n;
  return name.substr(0, n);
}

namespace {

// Length of the well-formed UTF-8 sequence starting at s[i], or 0.
size_t utf8Seq(std::string_view s, size_t i) {
  const auto b = [&](size_t k) { return static_cast<unsigned char>(s[k]); };
  const unsigned char c = b(i);
  size_t n;
  uint32_t cp;
  if (c < 0x80) return 1;
  if (c >= 0xC2 && c <= 0xDF) n = 2, cp = c & 0x1F;
  else if (c >= 0xE0 && c <= 0xEF) n = 3, cp = c & 0x0F;
  else if (c >= 0xF0 && c <= 0xF4) n = 4, cp = c & 0x07;
  else return 0;
  if (i + n > s.size()) return 0;
  for (size_t k = 1; k < n; ++k) {
    if ((b(i + k) & 0xC0) != 0x80) return 0;
    cp = cp << 6 | (b(i + k) & 0x3F);
  }
  // Overlongs, surrogates and > U+10FFFF are not text.
  if ((n == 3 && cp < 0x800) || (n == 4 && (cp < 0x10000 || cp > 0x10FFFF)) || (cp >= 0xD800 && cp <= 0xDFFF))
    return 0;
  return n;
}

}  // namespace

std::string cleanName(std::string_view raw, size_t maxBytes) {
  std::string out;
  for (size_t i = 0; i < raw.size();) {
    const size_t n = utf8Seq(raw, i);
    if (n == 0) {
      ++i;
      continue;
    }
    const auto c = static_cast<unsigned char>(raw[i]);
    if (!(n == 1 && (c < 0x20 || c == 0x7F))) out.append(raw.substr(i, n));
    i += n;
  }
  const size_t first = out.find_first_not_of(' ');
  if (first == std::string::npos) return {};
  out = out.substr(first, out.find_last_not_of(' ') - first + 1);
  return std::string(fitName(out, maxBytes));
}

std::string formatAddr(const Addr& a) {
  char buf[18];
  std::snprintf(buf, sizeof buf, "%02X:%02X:%02X:%02X:%02X:%02X", a[0], a[1], a[2], a[3], a[4], a[5]);
  return buf;
}

bool parseAddr(std::string_view s, Addr& out) {
  if (s.size() != 17) return false;
  Addr a{};
  for (size_t i = 0; i < 6; ++i) {
    const size_t p = i * 3;
    if (i > 0 && s[p - 1] != ':') return false;
    uint8_t v = 0;
    for (size_t j = 0; j < 2; ++j) {
      const char c = s[p + j];
      uint8_t d;
      if (c >= '0' && c <= '9') d = static_cast<uint8_t>(c - '0');
      else if (c >= 'a' && c <= 'f') d = static_cast<uint8_t>(c - 'a' + 10);
      else if (c >= 'A' && c <= 'F') d = static_cast<uint8_t>(c - 'A' + 10);
      else return false;
      v = static_cast<uint8_t>(v << 4 | d);
    }
    a[i] = v;
  }
  out = a;
  return true;
}

}  // namespace keyra::ble
