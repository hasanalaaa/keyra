// RFC 6238 Appendix B vectors (SHA1/256/512), base32 and otpauth parsing.
#include <string>

#include "check.hpp"
#include "core/text.hpp"
#include "core/totp_core.hpp"
#include "host_fakes.hpp"
#include "keyra/vault.hpp"

using namespace keyra::vault;

static test::OpenSslCrypto gCrypto;

static std::string base32(const std::string& raw) {
  static const char a[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZ234567";
  std::string out;
  uint32_t buf = 0;
  int bits = 0;
  for (unsigned char c : raw) {
    buf = (buf << 8) | c;
    bits += 8;
    while (bits >= 5) out += a[(buf >> (bits -= 5)) & 31];
  }
  if (bits) out += a[(buf << (5 - bits)) & 31];
  while (out.size() % 8) out += '=';
  return out;
}

static std::string codeOf(const std::string& s, int64_t t, int* period = nullptr,
                          int* remaining = nullptr) {
  char out[11] = "x";
  if (!totp::code(gCrypto, s, t, out, period, remaining)) return "ERR";
  return out;
}

TEST(rfc6238_vectors) {
  const std::string k1 = "12345678901234567890";
  const std::string k256 = "12345678901234567890123456789012";
  const std::string k512 = k256 + "34567890123456789012345678901234";
  struct V {
    int64_t t;
    const char *sha1, *sha256, *sha512;
  } vectors[] = {
      {59, "94287082", "46119246", "90693936"},
      {1111111109, "07081804", "68084774", "25091201"},
      {1111111111, "14050471", "67062674", "99943326"},
      {1234567890, "89005924", "91819424", "93441116"},
      {2000000000, "69279037", "90698825", "38618901"},
      {20000000000, "65353130", "77737706", "47863826"},
  };
  for (const auto& v : vectors) {
    auto uri = [](const std::string& key, const char* alg) {
      return "otpauth://totp/RFC:test?secret=" + base32(key) + "&algorithm=" + alg + "&digits=8";
    };
    CHECK(codeOf(uri(k1, "SHA1"), v.t) == v.sha1);
    CHECK(codeOf(uri(k256, "SHA256"), v.t) == v.sha256);
    CHECK(codeOf(uri(k512, "SHA512"), v.t) == v.sha512);
  }
}

TEST(defaults_period_remaining) {
  const std::string secret = base32("12345678901234567890");
  int period = 0, remaining = 0;
  CHECK(codeOf(secret, 59, &period, &remaining) == "287082");  // 6 digits, SHA1, 30 s
  CHECK(period == 30 && remaining == 1);
  CHECK(codeOf(secret, 60, &period, &remaining) != "ERR" && remaining == 30);
  std::string u60 = "otpauth://totp/x?secret=" + secret + "&period=60";
  CHECK(codeOf(u60, 59, &period, &remaining) != "ERR" && period == 60 && remaining == 1);
  // period 60 at t=119 uses counter 1, same as period 30 at t=30..59 → counter 1.
  CHECK(codeOf(u60, 119) == codeOf(secret, 45));
  CHECK(codeOf(secret, -1) == "ERR");
  // Public wrapper accepts null outputs.
  char out[11];
  CHECK(keyra::totp::code(secret, 59, out, nullptr, nullptr) && std::string(out) == "287082");
}

TEST(base32_edge_cases) {
  const std::string want = codeOf("JBSWY3DPEHPK3PXP", 1000);
  CHECK(want != "ERR");
  CHECK(codeOf("jbswy3dpehpk3pxp", 1000) == want);
  CHECK(codeOf("JBSW Y3DP EHPK 3PXP", 1000) == want);
  CHECK(codeOf("jbsw y3dp ehpk 3pxp", 1000) == want);

  std::vector<uint8_t> out;
  CHECK(text::base32Decode("MY======", out) && out == std::vector<uint8_t>{'f'});
  CHECK(text::base32Decode("MY", out) && out == std::vector<uint8_t>{'f'});
  CHECK(text::base32Decode("MZXW6YQ=", out) && std::string(out.begin(), out.end()) == "foob");
  CHECK(text::base32Decode("MZXW6YQ", out) && std::string(out.begin(), out.end()) == "foob");
  CHECK(text::base32Decode("MZXW6YTBOI======", out) && std::string(out.begin(), out.end()) == "foobar");
  CHECK(!text::base32Decode("", out));
  CHECK(!text::base32Decode("   ", out));
  CHECK(!text::base32Decode("MZXW1", out));   // '1' not in alphabet
  CHECK(!text::base32Decode("MZXW0", out));   // '0'
  CHECK(!text::base32Decode("MZXW8", out));   // '8'
  CHECK(!text::base32Decode("MY==MY", out));  // data after padding
  CHECK(!text::base32Decode("MZ-XW", out));
  CHECK(codeOf("not base32!", 1) == "ERR");
}

TEST(otpauth_parsing) {
  const std::string s = base32("12345678901234567890");
  totp::Params p;
  CHECK(totp::parse("otpauth://totp/ACME%20Co:alice@example.com?secret=" + s +
                        "&issuer=ACME%20Co&algorithm=SHA256&digits=8&period=60",
                    p));
  CHECK(p.hash == Hash::Sha256 && p.digits == 8 && p.period == 60 && p.secret.size() == 20);
  CHECK(totp::parse("OTPAUTH://TOTP/x?SECRET=" + s + "&Algorithm=sha512", p));
  CHECK(p.hash == Hash::Sha512 && p.digits == 6 && p.period == 30);
  CHECK(totp::parse("otpauth://totp/x?issuer=a&&secret=" + s, p));  // empty pair tolerated
  // Spaces in the secret arrive percent-encoded.
  CHECK(totp::parse("otpauth://totp/x?secret=GEZD%20GNBV%20GY3T%20QOJQ%20GEZD%20GNBV%20GY3T%20QOJQ", p));
  CHECK(p.secret.size() == 20);
  CHECK(totp::parse("otpauth://totp?secret=" + s, p));  // no label

  CHECK(!totp::parse("otpauth://hotp/x?secret=" + s + "&counter=1", p));
  CHECK(!totp::parse("otpauth://totp/x?issuer=nobody", p));  // no secret
  CHECK(!totp::parse("otpauth://totp/x", p));
  CHECK(!totp::parse("otpauth://totp/x?secret=" + s + "&digits=7", p));
  CHECK(!totp::parse("otpauth://totp/x?secret=" + s + "&period=45", p));
  CHECK(!totp::parse("otpauth://totp/x?secret=" + s + "&algorithm=MD5", p));
  CHECK(!totp::parse("otpauth://totp/x?secret=" + s + "&digits", p));
  CHECK(!totp::parse("otpauth://totp/x?secret=%ZZ", p));
  CHECK(!totp::parse("otpauth://steam/x?secret=" + s, p));
  CHECK(!totp::parse("https://example.com/?secret=" + s, p));  // treated as base32 → invalid
}

TEST_MAIN()
