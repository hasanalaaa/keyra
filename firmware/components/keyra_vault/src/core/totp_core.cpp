#include "totp_core.hpp"

#include <cstring>

#include "secure_buf.hpp"
#include "text.hpp"

namespace keyra::vault::totp {
namespace {

bool iequalsPrefix(const std::string& s, const char* prefix) {
  size_t n = std::strlen(prefix);
  if (s.size() < n) return false;
  for (size_t i = 0; i < n; ++i) {
    char c = s[i];
    if (c >= 'A' && c <= 'Z') c = char(c - 'A' + 'a');
    if (c != prefix[i]) return false;
  }
  return true;
}

bool iequals(const std::string& a, const char* b) {
  return a.size() == std::strlen(b) && iequalsPrefix(a, b);
}

int hexValue(char c) {
  if (c >= '0' && c <= '9') return c - '0';
  if (c >= 'a' && c <= 'f') return c - 'a' + 10;
  if (c >= 'A' && c <= 'F') return c - 'A' + 10;
  return -1;
}

bool percentDecode(const char* p, size_t n, std::string& out) {
  out.clear();
  for (size_t i = 0; i < n; ++i) {
    if (p[i] == '%') {
      if (i + 2 >= n) return false;
      int hi = hexValue(p[i + 1]), lo = hexValue(p[i + 2]);
      if (hi < 0 || lo < 0) return false;
      out += char(hi * 16 + lo);
      i += 2;
    } else {
      out += p[i];
    }
  }
  return true;
}

void scrub(std::string& s) {
  if (!s.empty()) mem::zeroize(&s[0], s.size());
  s.clear();
}

bool parseUri(const std::string& uri, Params& out) {
  // Scheme and type are case-insensitive in practice (some exporters upper-case them).
  if (iequalsPrefix(uri, "otpauth://hotp")) return false;
  if (!iequalsPrefix(uri, "otpauth://totp/") && !iequalsPrefix(uri, "otpauth://totp?")) return false;
  size_t q = uri.find('?');
  if (q == std::string::npos) return false;

  // Parsed in place (no per-pair substrings) so only `value`/`secret` ever hold
  // the secret, and both are wiped before returning.
  std::string secret, key, value;
  secret.reserve(uri.size());
  value.reserve(uri.size());
  bool haveSecret = false, ok = true;
  size_t pos = q + 1;
  while (ok && pos < uri.size()) {
    size_t amp = uri.find('&', pos);
    if (amp == std::string::npos) amp = uri.size();
    size_t eq = uri.find('=', pos);
    if (amp == pos) {
      pos = amp + 1;
      continue;
    }
    if (eq == std::string::npos || eq > amp) {
      ok = false;
      break;
    }
    key.assign(uri, pos, eq - pos);
    ok = percentDecode(uri.data() + eq + 1, amp - eq - 1, value);
    pos = amp + 1;
    if (!ok) break;
    if (iequals(key, "secret")) {
      secret = value;
      haveSecret = true;
    } else if (iequals(key, "algorithm")) {
      if (iequals(value, "sha1")) out.hash = Hash::Sha1;
      else if (iequals(value, "sha256")) out.hash = Hash::Sha256;
      else if (iequals(value, "sha512")) out.hash = Hash::Sha512;
      else ok = false;
    } else if (iequals(key, "digits")) {
      if (value == "6") out.digits = 6;
      else if (value == "8") out.digits = 8;
      else ok = false;
    } else if (iequals(key, "period")) {
      if (value == "30") out.period = 30;
      else if (value == "60") out.period = 60;
      else ok = false;
    }  // issuer, image, … are labels only
  }
  ok = ok && haveSecret && text::base32Decode(secret, out.secret);
  scrub(secret);
  scrub(value);
  return ok;
}

}  // namespace

bool parse(const std::string& secretOrUri, Params& out) {
  out = Params{};
  if (iequalsPrefix(secretOrUri, "otpauth:")) return parseUri(secretOrUri, out);
  return text::base32Decode(secretOrUri, out.secret);
}

bool code(Crypto& crypto, const std::string& secretOrUri, int64_t unixTime, char out[11],
          int* period, int* remaining) {
  if (unixTime < 0) return false;
  Params prm;
  bool ok = parse(secretOrUri, prm);
  uint8_t mac[64];
  size_t macLen = 0;
  if (ok) {
    uint64_t counter = uint64_t(unixTime) / uint64_t(prm.period);
    uint8_t msg[8];
    for (int i = 7; i >= 0; --i, counter >>= 8) msg[i] = uint8_t(counter);
    ok = crypto.hmac(prm.hash, prm.secret.data(), prm.secret.size(), msg, sizeof msg, mac, &macLen) &&
         macLen >= 20;
  }
  if (ok) {
    // RFC 4226 §5.3 dynamic truncation.
    size_t off = mac[macLen - 1] & 0x0F;
    uint32_t bin = (uint32_t(mac[off] & 0x7F) << 24) | (uint32_t(mac[off + 1]) << 16) |
                   (uint32_t(mac[off + 2]) << 8) | uint32_t(mac[off + 3]);
    uint32_t mod = prm.digits == 8 ? 100000000u : 1000000u;
    uint32_t value = bin % mod;
    for (int i = prm.digits - 1; i >= 0; --i, value /= 10) out[i] = char('0' + value % 10);
    out[prm.digits] = '\0';
    if (period) *period = prm.period;
    if (remaining) *remaining = prm.period - int(unixTime % prm.period);
  }
  mem::zeroize(mac, sizeof mac);
  if (!prm.secret.empty()) mem::zeroize(prm.secret.data(), prm.secret.size());
  return ok;
}

}  // namespace keyra::vault::totp
