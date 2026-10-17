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
  const std::vector<std::string> ended = s.endTrusted(7);
  CHECK(ended.size() == 2 && ended[0] == a.token && ended[1] == b.token);  // to withdraw their button items
  CHECK(!s.csrfFor(a.token, 5).has_value());
  CHECK(!s.csrfFor(b.token, 5).has_value());
  CHECK(s.csrfFor(plain.token, 5).has_value());
  CHECK(s.csrfFor(other.token, 5).has_value());
  CHECK(s.endTrusted(0).empty());  // untrusted sessions are never matched
  CHECK_EQ(s.size(), 2u);
}

using A = Sessions::Approval;
using P = Sessions::Approval::Purpose;
using R = Sessions::Redeem;

// SPEC §12.5b: one press approves one request: this account, this version of
// it, this session. It is used up by that request and by nothing else.
void approvalIsOneShot() {
  Sessions s(fakeRandom);
  auto a = s.create(1);
  const A entry5{P::RevealEntry, 5, 9, {}};
  CHECK(s.redeem(a.token, 10, entry5) == R::None);  // nothing approved yet
  CHECK(s.approve(a.token, 100, entry5));
  CHECK(s.redeem(a.token, 101, entry5) == R::Ok);
  CHECK(s.redeem(a.token, 102, entry5) == R::None);  // used up
}

void approvalIsForItsRequesterOnly() {
  Sessions s(fakeRandom);
  auto a = s.create(1), b = s.create(1);
  const A entry5{P::RevealEntry, 5, 9, {}};
  CHECK(s.approve(a.token, 100, entry5));
  CHECK(s.redeem(b.token, 101, entry5) == R::None);  // another browser
  CHECK(s.redeem(a.csrf, 101, entry5) == R::None);   // the CSRF token is not a session
  CHECK(s.redeem("nope", 101, entry5) == R::None);
  CHECK(s.redeem(a.token, 102, entry5) == R::Ok);    // still there for its own session
  CHECK(!s.approve("nope", 1, entry5));
}

void approvalIsForItsEntryAndPurposeOnly() {
  Sessions s(fakeRandom);
  auto a = s.create(1);
  CHECK(s.approve(a.token, 100, {P::RevealEntry, 5, 9, {}}));
  CHECK(s.redeem(a.token, 101, {P::RevealEntry, 6, 9, {}}) == R::None);  // another account
  CHECK(s.redeem(a.token, 101, {P::RevealHealth, 0, 0, {}}) == R::None);            // another purpose
  CHECK(s.redeem(a.token, 101, {P::Backup, 0, 0, {}}) == R::None);
  CHECK(s.redeem(a.token, 102, {P::RevealEntry, 5, 9, {}}) == R::Ok);    // a miss did not use it up
  // Creating and removing the recovery key are two requests.
  CHECK(s.approve(a.token, 200, {P::RecoveryCreate, 0, 0, {}}));
  CHECK(s.redeem(a.token, 201, {P::RecoveryRemove, 0, 0, {}}) == R::None);
  CHECK(s.redeem(a.token, 202, {P::RecoveryCreate, 0, 0, {}}) == R::Ok);
  // A token or tag: the same body only.
  CHECK(s.approve(a.token, 300, {P::TokenCreate, 0, 0, "digest-1"}));
  CHECK(s.redeem(a.token, 301, {P::TokenCreate, 0, 0, "digest-2"}) == R::None);
  CHECK(s.redeem(a.token, 301, {P::TagCreate, 0, 0, "digest-1"}) == R::None);
  CHECK(s.redeem(a.token, 302, {P::TokenCreate, 0, 0, "digest-1"}) == R::Ok);
}

void approvalForAnEditedEntryIsChanged() {
  Sessions s(fakeRandom);
  auto a = s.create(1);
  CHECK(s.approve(a.token, 100, {P::RevealEntry, 5, 9, {}}));
  CHECK(s.redeem(a.token, 101, {P::RevealEntry, 5, 10, {}}) == R::Changed);  // edited after the request
  CHECK(s.redeem(a.token, 102, {P::RevealEntry, 5, 9, {}}) == R::None);       // dropped, not kept for later
}

void approvalExpires() {
  Sessions s(fakeRandom);
  auto a = s.create(1);
  CHECK(s.approve(a.token, 100, {P::Backup, 0, 0, {}}));
  CHECK(s.redeem(a.token, 100 + kApprovalMs, {P::Backup, 0, 0, {}}) == R::None);
  CHECK(s.approve(a.token, 200, {P::Backup, 0, 0, {}}));
  CHECK(s.redeem(a.token, 200 + kApprovalMs - 1, {P::Backup, 0, 0, {}}) == R::Ok);
}

void approvalEndsWithTheSession() {
  Sessions s(fakeRandom);
  auto a = s.create(1);
  CHECK(s.approve(a.token, 100, {P::Backup, 0, 0, {}}));
  s.clear();  // lock
  CHECK(s.redeem(a.token, 101, {P::Backup, 0, 0, {}}) == R::None);
  auto b = s.create(200);  // a new session in a reused slot starts without one
  CHECK(s.redeem(b.token, 201, {P::Backup, 0, 0, {}}) == R::None);
  // A newer press replaces an unused approval.
  CHECK(s.approve(b.token, 300, {P::RevealEntry, 1, 1, {}}));
  CHECK(s.approve(b.token, 301, {P::RevealEntry, 2, 1, {}}));
  CHECK(s.redeem(b.token, 302, {P::RevealEntry, 1, 1, {}}) == R::None);
  CHECK(s.redeem(b.token, 302, {P::RevealEntry, 2, 1, {}}) == R::Ok);
}

// Work deferred to the slow worker (a restore, a passphrase change) runs only
// for a session that is still live in the same unlock when it gets its turn.
void deferredWorkRechecksTheSession() {
  Sessions s(fakeRandom);
  const auto a = s.create(1, 7, 3), b = s.create(2, 0, 3);
  CHECK(s.stillValid(a.token, 3, 3, true));
  CHECK(!s.stillValid(a.token, 3, 3, false));  // locked meanwhile
  CHECK(!s.stillValid(a.token, 3, 4, true));   // locked and unlocked again
  CHECK(!s.stillValid("nope", 3, 3, true));
  s.endTrusted(7);                             // its trusted browser was removed
  CHECK(!s.stillValid(a.token, 3, 3, true));
  CHECK(s.stillValid(b.token, 3, 3, true));
  s.clear();                                   // lock
  CHECK(!s.stillValid(b.token, 3, 3, true));
}

// A fifth browser pushes the least recently used session out; its token comes
// back so what it armed can be withdrawn.
void evictionNamesTheEvicted() {
  Sessions s(fakeRandom);
  const auto a = s.create(1), b = s.create(2), c = s.create(3), d = s.create(4);
  CHECK(a.evicted.empty() && b.evicted.empty() && c.evicted.empty() && d.evicted.empty());
  const auto e = s.create(5);
  CHECK(e.evicted == a.token);
}

// F1: a session that was shown an account's password may replace it without
// another press; nobody else, and not after the account changed.
void revealedMarkIsPerSessionAndRevision() {
  Sessions s(fakeRandom);
  const auto a = s.create(1), b = s.create(2);
  CHECK(!s.knows(a.token, 5, 1));
  s.noteRevealed(a.token, 5, 1);
  CHECK(s.knows(a.token, 5, 1));
  CHECK(s.knows(a.token, 5, 1));       // not used up: the session still knows it
  CHECK(!s.knows(b.token, 5, 1));      // another session does not
  CHECK(!s.knows(a.token, 5, 2));      // the account changed since
  CHECK(!s.knows(a.token, 6, 1));
  CHECK(!s.knows("nope", 5, 1));
  s.noteRevealed(a.token, 6, 4);       // revealing another account replaces it
  CHECK(!s.knows(a.token, 5, 1) && s.knows(a.token, 6, 4));
  s.noteRevealed("nope", 7, 1);        // an unknown session marks nothing
  CHECK(!s.knows("nope", 7, 1));
  s.clear();                           // lock
  CHECK(!s.knows(a.token, 6, 4));
  const auto c = s.create(3);          // a reused slot starts without it
  CHECK(!s.knows(c.token, 6, 4));
}

}  // namespace

// A session belongs to the unlock it was made in: after the vault locked
// itself and unlocked again, the old token stays dead.
void testGenerationTiesSessionToUnlock() {
  Sessions s(fakeRandom);
  const auto a = s.create(0, 0, 1);
  CHECK(s.csrfFor(a.token, 1, 1).has_value());
  CHECK(!s.csrfFor(a.token, 2, 2).has_value());
}

int main() {
  approvalIsOneShot();
  approvalIsForItsRequesterOnly();
  approvalIsForItsEntryAndPurposeOnly();
  approvalForAnEditedEntryIsChanged();
  approvalExpires();
  approvalEndsWithTheSession();
  testGenerationTiesSessionToUnlock();
  issuesDistinctHexTokens();
  evictsLeastRecentlyUsedBeyondFour();
  clearEndsAll();
  idleTracking();
  constantTimeCompare();
  revokingATrustedBrowserEndsOnlyItsSessions();
  deferredWorkRechecksTheSession();
  evictionNamesTheEvicted();
  revealedMarkIsPerSessionAndRevision();
  return KEYRA_TEST_RESULT();
}
