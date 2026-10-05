#include "text.hpp"

namespace keyra::vault::text {

bool validUtf8(const std::string& s) {
  const auto* p = reinterpret_cast<const uint8_t*>(s.data());
  size_t i = 0, n = s.size();
  while (i < n) {
    uint8_t c = p[i];
    size_t len;
    uint32_t cp, min;
    if (c < 0x80) {
      ++i;
      continue;
    } else if ((c & 0xE0) == 0xC0) {
      len = 2, cp = c & 0x1F, min = 0x80;
    } else if ((c & 0xF0) == 0xE0) {
      len = 3, cp = c & 0x0F, min = 0x800;
    } else if ((c & 0xF8) == 0xF0) {
      len = 4, cp = c & 0x07, min = 0x10000;
    } else {
      return false;
    }
    if (n - i < len) return false;
    for (size_t k = 1; k < len; ++k) {
      if ((p[i + k] & 0xC0) != 0x80) return false;
      cp = (cp << 6) | (p[i + k] & 0x3F);
    }
    if (cp < min || cp > 0x10FFFF || (cp >= 0xD800 && cp <= 0xDFFF)) return false;
    i += len;
  }
  return true;
}

size_t codePoints(const std::string& s) {
  size_t count = 0;
  for (unsigned char c : s) count += (c & 0xC0) != 0x80;
  return count;
}

void appendUtf8(std::string& out, uint32_t cp) {
  if (cp < 0x80) {
    out += char(cp);
  } else if (cp < 0x800) {
    out += char(0xC0 | (cp >> 6));
    out += char(0x80 | (cp & 0x3F));
  } else if (cp < 0x10000) {
    out += char(0xE0 | (cp >> 12));
    out += char(0x80 | ((cp >> 6) & 0x3F));
    out += char(0x80 | (cp & 0x3F));
  } else {
    out += char(0xF0 | (cp >> 18));
    out += char(0x80 | ((cp >> 12) & 0x3F));
    out += char(0x80 | ((cp >> 6) & 0x3F));
    out += char(0x80 | (cp & 0x3F));
  }
}

static const char kB64[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";

std::string base64Encode(const uint8_t* p, size_t n) {
  std::string out;
  out.reserve((n + 2) / 3 * 4);
  for (size_t i = 0; i < n; i += 3) {
    uint32_t v = uint32_t(p[i]) << 16;
    if (i + 1 < n) v |= uint32_t(p[i + 1]) << 8;
    if (i + 2 < n) v |= p[i + 2];
    out += kB64[(v >> 18) & 63];
    out += kB64[(v >> 12) & 63];
    out += i + 1 < n ? kB64[(v >> 6) & 63] : '=';
    out += i + 2 < n ? kB64[v & 63] : '=';
  }
  return out;
}

static int b64Value(char c) {
  if (c >= 'A' && c <= 'Z') return c - 'A';
  if (c >= 'a' && c <= 'z') return c - 'a' + 26;
  if (c >= '0' && c <= '9') return c - '0' + 52;
  if (c == '+') return 62;
  if (c == '/') return 63;
  return -1;
}

bool base64Decode(const std::string& in, std::vector<uint8_t>& out) {
  out.clear();
  if (in.size() % 4) return false;
  out.reserve(in.size() / 4 * 3);
  for (size_t i = 0; i < in.size(); i += 4) {
    const bool last = i + 4 == in.size();
    int v[4];
    int pad = 0;
    for (int k = 0; k < 4; ++k) {
      char c = in[i + k];
      // '=' only in the final quantum's last one or two positions.
      if (c == '=' && last && k >= 2) {
        v[k] = 0;
        ++pad;
        continue;
      }
      if (pad) return false;
      v[k] = b64Value(c);
      if (v[k] < 0) return false;
    }
    uint32_t w = (uint32_t(v[0]) << 18) | (uint32_t(v[1]) << 12) | (uint32_t(v[2]) << 6) | v[3];
    out.push_back(uint8_t(w >> 16));
    if (pad < 2) out.push_back(uint8_t(w >> 8));
    if (pad < 1) out.push_back(uint8_t(w));
  }
  return true;
}

bool base32Decode(const std::string& in, std::vector<uint8_t>& out) {
  out.clear();
  out.reserve(in.size() * 5 / 8 + 1);  // no regrowth: callers wipe the one buffer
  uint32_t buffer = 0;
  int bits = 0;
  bool padding = false;
  for (char c : in) {
    if (c == ' ') continue;
    if (c == '=') {
      padding = true;
      continue;
    }
    if (padding) return false;  // data after padding
    int v;
    if (c >= 'A' && c <= 'Z') v = c - 'A';
    else if (c >= 'a' && c <= 'z') v = c - 'a';
    else if (c >= '2' && c <= '7') v = c - '2' + 26;
    else return false;
    buffer = (buffer << 5) | uint32_t(v);
    bits += 5;
    if (bits >= 8) {
      bits -= 8;
      out.push_back(uint8_t(buffer >> bits));
    }
  }
  return !out.empty();
}

}  // namespace keyra::vault::text
