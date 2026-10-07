#include <set>

#include "keyra_test.hpp"
#include "sessions.hpp"

using namespace keyra::api;

namespace {

uint8_t g_counter = 0;
void fakeRandom(uint8_t* p, size_t n) {
  for (size_t i = 0; i < n; ++i) p[i] = static_cast<uint8_t>(g_counter * 31 + i);
  ++g_counter;
}

void issuesDistinctHexTokens() {
  Sessions s(fakeRandom);
  auto a = s.create(1);
  CHECK_EQ(a.token.size(), 64u);
  CHECK_EQ(a.csrf.size(), 64u);
  CHECK(a.token != a.csrf);
  CHECK(a.token.find_first_not_of("0123456789abcdef") == std::string::npos);
  auto csrf = s.csrfFor(a.token, 2);
  CHECK(csrf && *csrf == a.csrf);
  CHECK(!s.csrfFor(a.csrf, 2).has_value());
  CHECK(!s.csrfFor("", 2).has_value());
  CHECK(!s.csrfFor(a.token.substr(0, 63), 2).has_value());
}

void evictsLeastRecentlyUsedBeyondFour() {
  Sessions s(fakeRandom);
  auto a = s.create(1), b = s.create(2), c = s.create(3), d = s.create(4);
  s.csrfFor(a.token, 5);  // a is now the most recent; b is the LRU
  auto e = s.create(6);
  CHECK_EQ(s.size(), kMaxSessions);
  CHECK(s.csrfFor(a.token, 7).has_value());
  CHECK(!s.csrfFor(b.token, 7).has_value());
  CHECK(s.csrfFor(c.token, 7).has_value());
  CHECK(s.csrfFor(d.token, 7).has_value());
  CHECK(s.csrfFor(e.token, 7).has_value());
}

void clearEndsAll() {
  Sessions s(fakeRandom);
  auto a = s.create(1);
  s.clear();
  CHECK_EQ(s.size(), 0u);
  CHECK(!s.csrfFor(a.token, 2).has_value());
}

void idleTracking() {
  Sessions s(fakeRandom);
  s.activity(1000);
  CHECK(!s.idleFor(1000 + 59999, 60000));
  CHECK(s.idleFor(1000 + 60000, 60000));
  s.create(70000);
  CHECK(!s.idleFor(70001, 60000));
}

void constantTimeCompare() {
  CHECK(constantTimeEqual("abc", "abc"));
  CHECK(!constantTimeEqual("abc", "abd"));
  CHECK(!constantTimeEqual("abc", "abcd"));
  CHECK(constantTimeEqual("", ""));
}

void revokingATrustedBrowserEndsOnlyItsSessions() {
  Sessions s(fakeRandom);
  auto plain = s.create(1);
  auto a = s.create(2, 7), b = s.create(3, 7), other = s.create(4, 9);
  CHECK_EQ(s.endTrusted(7), 2u);
  CHECK(!s.csrfFor(a.token, 5).has_value());
  CHECK(!s.csrfFor(b.token, 5).has_value());
  CHECK(s.csrfFor(plain.token, 5).has_value());
  CHECK(s.csrfFor(other.token, 5).has_value());
  CHECK_EQ(s.endTrusted(0), 0u);  // untrusted sessions are never matched
  CHECK_EQ(s.size(), 2u);
}

// SPEC §12.3: a press opens a 60 s reveal window for the session that asked only.
void graceIsPerSessionAndExpires() {
  Sessions s(fakeRandom);
  auto a = s.create(1), b = s.create(1);
  CHECK_EQ(s.graceLeft(a.token, 10), int64_t{0});
  CHECK(s.grantGrace(a.token, 100));
  CHECK_EQ(s.graceLeft(a.token, 100), kGraceMs);
  CHECK_EQ(s.graceLeft(a.token, 100 + kGraceMs - 1), int64_t{1});
  CHECK_EQ(s.graceLeft(a.token, 100 + kGraceMs), int64_t{0});
  CHECK_EQ(s.graceLeft(b.token, 200), int64_t{0});  // another browser
  CHECK(!s.grantGrace("nope", 1));
  CHECK_EQ(s.graceLeft(a.csrf, 200), int64_t{0});  // the CSRF token is not a session
  CHECK(s.grantGrace(b.token, 300));
  s.clear();  // lock ends every grace with its session
  CHECK_EQ(s.graceLeft(b.token, 301), int64_t{0});
  CHECK(!s.grantGrace(b.token, 302));
  // A new session in a reused slot starts without grace.
  auto c = s.create(400);
  CHECK_EQ(s.graceLeft(c.token, 401), int64_t{0});
}

}  // namespace

int main() {
  graceIsPerSessionAndExpires();
  issuesDistinctHexTokens();
  evictsLeastRecentlyUsedBeyondFour();
  clearEndsAll();
  idleTracking();
  constantTimeCompare();
  revokingATrustedBrowserEndsOnlyItsSessions();
  return KEYRA_TEST_RESULT();
}
