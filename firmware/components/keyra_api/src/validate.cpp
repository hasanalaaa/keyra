#include "validate.hpp"

#include <cstdint>

namespace keyra::api::validate {
namespace {

bool printableUtf8(std::string_view s, size_t maxBytes) {
  if (s.empty() || s.size() > maxBytes || utf8Length(s) < 0) return false;
  for (unsigned char c : s) {
    if (c < 0x20 || c == 0x7F) return false;
  }
  return true;
}

}  // namespace

long utf8Length(std::string_view s) {
  long count = 0;
  for (size_t i = 0; i < s.size();) {
    const auto c = static_cast<uint8_t>(s[i]);
    size_t len;
    uint32_t cp;
    if (c < 0x80) {
      len = 1;
      cp = c;
    } else if ((c & 0xE0) == 0xC0) {
      len = 2;
      cp = c & 0x1F;
    } else if ((c & 0xF0) == 0xE0) {
      len = 3;
      cp = c & 0x0F;
    } else if ((c & 0xF8) == 0xF0) {
      len = 4;
      cp = c & 0x07;
    } else {
      return -1;
    }
    if (i + len > s.size()) return -1;
    for (size_t k = 1; k < len; ++k) {
      const auto cc = static_cast<uint8_t>(s[i + k]);
      if ((cc & 0xC0) != 0x80) return -1;
      cp = (cp << 6) | (cc & 0x3F);
    }
    static constexpr uint32_t kMinForLen[] = {0, 0, 0x80, 0x800, 0x10000};
    if (cp < kMinForLen[len] || cp > 0x10FFFF || (cp >= 0xD800 && cp <= 0xDFFF)) return -1;
    i += len;
    ++count;
  }
  return count;
}

bool passphrase(std::string_view s) {
  const long n = utf8Length(s);
  return n >= 10 && n <= 128;
}

bool homePassword(std::string_view s) {
  if (s.size() < 8 || s.size() > 63) return false;
  for (unsigned char c : s) {
    if (c < 0x20 || c > 0x7E) return false;
  }
  return true;
}

bool wifiPassword(std::string_view s) { return s != kDefaultWifiPassword && homePassword(s); }

bool ssid(std::string_view s) { return printableUtf8(s, 32); }
bool deviceName(std::string_view s) { return printableUtf8(s, 32); }

}  // namespace keyra::api::validate
