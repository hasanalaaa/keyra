// Access tokens (SPEC §17): format, hashing at rest, scope, limits, revoke, the
// stored record and the rate limit.
#include <openssl/sha.h>

#include <algorithm>
#include <cstring>
#include <string>

#include "keyra_test.hpp"
#include "tokens.hpp"

using namespace keyra::api::tokens;

namespace {

Digest sha256(const std::string& s) {
  Digest d{};
  SHA256(reinterpret_cast<const unsigned char*>(s.data()), s.size(), d.data());
  return d;
}

Token token(uint32_t id, const std::string& secret, Kind kind = Kind::Agent) {
  Token t;
  t.id = id;
  t.hash = sha256(secret);
  t.kind = kind;
  t.name = "Claude";
  t.created = 1790000000;
  return t;
}

std::string secretOf(uint8_t seed) {
  uint8_t raw[kSecretBytes];
  for (size_t i = 0; i < kSecretBytes; ++i) raw[i] = static_cast<uint8_t>(seed * 31 + i * 7);
  return format(raw);
}

void formatAndParse() {
  uint8_t zeros[kSecretBytes] = {};
  CHECK(format(zeros) == "keyra_aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa");
  uint8_t ones[kSecretBytes];
  std::memset(ones, 0xff, sizeof ones);
  CHECK(format(ones) == "keyra_77777777777777777777777777777777");
  // RFC 4648 test vector "foobar" → MZXW6YTBOI, padded here to 20 bytes of input.
  uint8_t foobar[kSecretBytes] = {'f', 'o', 'o', 'b', 'a', 'r'};
  CHECK(format(foobar).substr(0, 6 + 10) == "keyra_mzxw6ytboi");
  const std::string t = secretOf(3);
  CHECK(t.size() == kTokenChars && wellFormed(t));
  CHECK(secretOf(3) != secretOf(4));
  CHECK(!wellFormed(""));
  CHECK(!wellFormed(t.substr(0, kTokenChars - 1)));
  CHECK(!wellFormed(t + "a"));
  CHECK(!wellFormed("KEYRA_" + t.substr(6)));
  std::string upper = t;
  upper[10] = 'A';
  CHECK(!wellFormed(upper));
  std::string digit = t;
  digit[10] = '1';  // not in the base32 alphabet
  CHECK(!wellFormed(digit));

  CHECK(bearer("Bearer " + t) == t);
  CHECK(bearer("bearer  " + t + " ") == t);
  CHECK(bearer("Basic abc").empty());
  CHECK(bearer("Bearer").empty());
  CHECK(bearer("").empty());
}

void namesAndKinds() {
  CHECK(validName("Claude"));
  CHECK(validName("\xD9\x88\xD9\x83\xD9\x8A\xD9\x84"));  // وكيل
  CHECK(!validName(""));
  CHECK(!validName(std::string(kMaxName + 1, 'a')));
  CHECK(validName(std::string(kMaxName, 'a')));
  CHECK(!validName("tab\there"));
  CHECK(!validName("\xC3"));  // cut UTF-8
  CHECK(parseKind("agent") == Kind::Agent && parseKind("app") == Kind::App && !parseKind("admin"));
  CHECK(std::string(kindName(Kind::App)) == "app");
  CHECK(owner(42) == "token:42");
  CHECK(owner(42).size() != 64);  // can never equal a session token (64 hex)
  CHECK(ownerId(owner(42)) == 42 && ownerId(owner(4294967295u)) == 4294967295u);
  CHECK(ownerId("") == 0 && ownerId(std::string(64, 'a')) == 0 && ownerId("token:") == 0);
  CHECK(ownerId("token:4294967296") == 0 && ownerId("token:12x") == 0);
}

void hosts() {
  CHECK(urlHost("https://GitHub.com/login?next=/") == "github.com");
  CHECK(urlHost("github.com") == "github.com");
  CHECK(urlHost("http://user:pw@bank.example:8443/x") == "bank.example");
  CHECK(urlHost("https://[::1]:8080/") == "::1");
  CHECK(urlHost("") == "");
  CHECK(urlHost("android://com.example.app") == "com.example.app");
  CHECK(urlHost("not a url") == "");
  CHECK(urlHost("https://xn--mgbh0fb.example#frag") == "xn--mgbh0fb.example");
}

void hashedAtRestAndFound() {
  const std::string a = secretOf(1), b = secretOf(2);
  Store s;
  CHECK(s.add(token(10, a)));
  CHECK(s.add(token(11, b, Kind::App)));
  CHECK(s.find(sha256(a)) == size_t{0});
  CHECK(s.find(sha256(b)) == size_t{1});
  CHECK(!s.find(sha256(secretOf(9))).has_value());
  CHECK(!s.find(sha256(a + "x")).has_value());
  // Only the digest is stored: neither the token nor its random part is in the record.
  const auto blob = s.serialize();
  const std::string bytes(blob.begin(), blob.end());
  CHECK(bytes.find(a) == std::string::npos && bytes.find(a.substr(6)) == std::string::npos);
  CHECK(bytes.find(b.substr(6, 12)) == std::string::npos);
  const Digest d = sha256(a);
  CHECK(bytes.find(std::string(d.begin(), d.end())) != std::string::npos);
}

void scopeRules() {
  Token all = token(1, "x");
  CHECK(inScope(all, 5) && inScope(all, 4000000000u));
  CHECK(!inScope(all, 0));
  Token some = token(2, "y");
  some.all = false;
  some.scope = {7, 9};
  CHECK(inScope(some, 7) && inScope(some, 9));
  CHECK(!inScope(some, 8));
  CHECK(!mayWrite(some));
  some.kind = Kind::App;
  CHECK(mayWrite(some));

  Store s;
  CHECK(s.add(some));
  CHECK(s.addToScope(2, 11));
  CHECK(inScope(s.all()[0], 11));
  CHECK(s.addToScope(2, 11) && s.all()[0].scope.size() == 3);  // already in: nothing added
  CHECK(!s.addToScope(3, 11));                                  // unknown token
  CHECK(!s.addToScope(2, 0));
  for (uint32_t id = 100; s.all()[0].scope.size() < kMaxScope; ++id) CHECK(s.addToScope(2, id));
  CHECK(!s.addToScope(2, 999));  // full
  CHECK(s.add(all));
  CHECK(s.addToScope(1, 999) && s.all()[1].scope.empty());  // all-entries token needs nothing

  Token tooMany = token(3, "z");
  tooMany.all = false;
  tooMany.scope.assign(kMaxScope + 1, 5);
  CHECK(!s.add(tooMany));
  Token zeroId = token(4, "w");
  zeroId.all = false;
  zeroId.scope = {0};
  CHECK(!s.add(zeroId));
}

void limitsAndRevoke() {
  Store s;
  for (uint32_t id = 1; id <= kMaxTokens; ++id) CHECK(s.add(token(id, secretOf(static_cast<uint8_t>(id)))));
  CHECK(s.full());
  CHECK(!s.add(token(99, secretOf(99))));  // no eviction: a ninth is refused
  CHECK(!s.add(token(0, "zero")));
  Store t;
  CHECK(t.add(token(5, "a")));
  CHECK(!t.add(token(5, "b")));  // id taken
  Token unnamed = token(6, "c");
  unnamed.name.clear();
  CHECK(!t.add(unnamed));

  const std::string third = secretOf(3);
  CHECK(s.find(sha256(third)).has_value());
  CHECK(s.remove(3));
  CHECK(!s.remove(3));
  CHECK(!s.contains(3));
  CHECK(!s.find(sha256(third)).has_value());  // revoked: the same token no longer matches
  CHECK(!s.full());
}

void touchIsThrottled() {
  Store s;
  s.add(token(1, "a"));
  CHECK(s.touch(0, 1000));
  CHECK(s.all()[0].lastUsed == 1000);
  CHECK(!s.touch(0, 1000 + kTouchEverySec - 1));
  CHECK(s.all()[0].lastUsed == 1000);
  CHECK(s.touch(0, 1000 + kTouchEverySec));
  CHECK(!s.touch(0, 0));  // clock unknown: nothing to record
  CHECK(!s.touch(5, 5000));
}

void recordRoundTrip() {
  Store s;
  Token a = token(7, "a", Kind::App);
  a.name = "\xD9\x87\xD8\xA7\xD8\xAA\xD9\x81\xD9\x8A";  // هاتفي
  a.lastUsed = 1790000500;
  Token b = token(9, "b");
  b.all = false;
  b.scope = {3, 4000000000u};
  CHECK(s.add(a) && s.add(b));
  const auto blob = s.serialize();
  const auto back = Store::parse(blob.data(), blob.size());
  CHECK(back.has_value());
  if (back) {
    CHECK(back->all().size() == 2);
    const Token& x = back->all()[0];
    CHECK(x.id == 7 && x.kind == Kind::App && x.name == a.name && x.all && x.hash == a.hash);
    CHECK(x.created == a.created && x.lastUsed == a.lastUsed);
    const Token& y = back->all()[1];
    CHECK(y.id == 9 && !y.all && y.scope == b.scope && y.kind == Kind::Agent);
  }
  const auto empty = Store::parse(nullptr, 0);
  CHECK(empty && empty->all().empty());
  // Anything malformed is refused, not guessed.
  for (size_t cut = 1; cut < blob.size(); ++cut) CHECK(!Store::parse(blob.data(), cut).has_value());
  auto bad = blob;
  bad[0] = 2;  // unknown version
  CHECK(!Store::parse(bad.data(), bad.size()));
  bad = blob;
  bad.push_back(0);  // trailing byte
  CHECK(!Store::parse(bad.data(), bad.size()));
  bad = blob;
  bad[2 + 4 + 32] = 7;  // unknown kind
  CHECK(!Store::parse(bad.data(), bad.size()));
  bad = blob;
  bad[1] = kMaxTokens + 1;
  CHECK(!Store::parse(bad.data(), bad.size()));
}

void rateLimit() {
  RateLimit rl;
  int64_t retry = -1;
  for (size_t i = 0; i < RateLimit::kMax; ++i) CHECK(rl.allow(1, 1000 + int64_t(i) * 100, retry) && retry == 0);
  CHECK(!rl.allow(1, 2000, retry));
  CHECK(retry == 1000 + RateLimit::kWindowMs - 2000);
  CHECK(rl.allow(2, 2000, retry));  // another token has its own budget
  CHECK(!rl.allow(1, 1000 + RateLimit::kWindowMs - 1, retry) && retry == 1);
  CHECK(rl.allow(1, 1000 + RateLimit::kWindowMs, retry));  // the oldest left the window
  CHECK(!rl.allow(1, 1000 + RateLimit::kWindowMs, retry));  // ...and only that one
  CHECK(rl.allow(1, 1100 + RateLimit::kWindowMs, retry));
  // Refusals do not count: waiting the advertised time always works.
  RateLimit r2;
  for (size_t i = 0; i < RateLimit::kMax; ++i) r2.allow(0, 0, retry);
  for (int i = 0; i < 50; ++i) CHECK(!r2.allow(0, 5000, retry));
  CHECK(r2.allow(0, RateLimit::kWindowMs, retry));
  // Forget (revoked token) starts it fresh; many keys never break the table.
  for (size_t i = 0; i < RateLimit::kMax; ++i) rl.allow(3, 50000, retry);
  CHECK(!rl.allow(3, 50001, retry));
  rl.forget(3);
  CHECK(rl.allow(3, 50002, retry));
  for (uint32_t k = 100; k < 140; ++k) CHECK(rl.allow(k, 60000 + k, retry));
}

}  // namespace

int main() {
  formatAndParse();
  namesAndKinds();
  hosts();
  hashedAtRestAndFound();
  scopeRules();
  limitsAndRevoke();
  touchIsThrottled();
  recordRoundTrip();
  rateLimit();
  return KEYRA_TEST_RESULT();
}
