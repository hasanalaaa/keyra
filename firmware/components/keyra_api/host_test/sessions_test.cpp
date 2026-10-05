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

}  // namespace

int main() {
  issuesDistinctHexTokens();
  evictsLeastRecentlyUsedBeyondFour();
  clearEndsAll();
  idleTracking();
  constantTimeCompare();
  return KEYRA_TEST_RESULT();
}
