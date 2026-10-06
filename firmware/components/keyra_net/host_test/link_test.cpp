// Home-network policy (SPEC §8.2): backoff schedule, apMode fallback timing, and
// that only Link decides when to join.
#include "keyra_test.hpp"
#include "link.hpp"

using namespace keyra::net;

namespace {

constexpr int64_t kSec = 1000;

void backoffDoublesFrom2sTo5min() {
  CHECK_EQ(retryDelayMs(0), 2 * kSec);
  CHECK_EQ(retryDelayMs(1), 2 * kSec);
  CHECK_EQ(retryDelayMs(2), 4 * kSec);
  CHECK_EQ(retryDelayMs(3), 8 * kSec);
  CHECK_EQ(retryDelayMs(8), 256 * kSec);
  CHECK_EQ(retryDelayMs(9), 300 * kSec);
  CHECK_EQ(retryDelayMs(50), 300 * kSec);
}

void disabledNeverJoinsAndKeepsApOn() {
  Link l;
  l.boot(false, ApMode::Fallback, 0);
  CHECK(l.apOn());
  for (int64_t t = 0; t < 120 * kSec; t += 250) CHECK(!l.tick(t, false).connect);
  CHECK(l.apOn());
}

void retriesFollowTheScheduleAndResetOnSuccess() {
  Link l;
  int64_t t = 0;
  l.boot(true, ApMode::Always, t);
  CHECK(l.tick(t, false).connect);
  CHECK(l.attempting());
  CHECK(!l.tick(t + 100, false).connect);  // one attempt at a time
  l.disconnected(t += 3 * kSec);           // wrong password, say
  CHECK_EQ(l.failures(), 1);
  CHECK(!l.tick(t + 2 * kSec - 1, false).connect);
  CHECK(l.tick(t += 2 * kSec, false).connect);
  l.disconnected(t);
  CHECK_EQ(l.nextAttemptAt(), t + 4 * kSec);
  CHECK(l.tick(t += 4 * kSec, false).connect);
  l.connected(t);
  CHECK(l.up());
  CHECK_EQ(l.failures(), 0);
  // A lost link retries after the first step again, not where the old streak ended.
  l.disconnected(t += 600 * kSec);
  CHECK(!l.up());
  CHECK_EQ(l.nextAttemptAt(), t + 2 * kSec);
}

void attemptTimeoutAbortsOnceAndLateEventIsNotDoubleCounted() {
  Link l;
  l.boot(true, ApMode::Always, 0);
  CHECK(l.tick(0, false).connect);
  CHECK(!l.tick(Link::kAttemptTimeoutMs - 1, false).abort);
  const Link::Plan p = l.tick(Link::kAttemptTimeoutMs, false);
  CHECK(p.abort && !p.connect);
  CHECK_EQ(l.failures(), 1);
  l.disconnected(Link::kAttemptTimeoutMs + 50);  // the event the abort caused
  CHECK_EQ(l.failures(), 1);
}

void scanHoldsNewAttemptsBack() {
  Link l;
  l.boot(true, ApMode::Always, 0);
  CHECK(!l.tick(0, true).connect);
  CHECK(l.tick(10, false).connect);
}

void rejoinResetsBackoffAndIgnoresTheOldLinksDisconnect() {
  Link l;
  int64_t t = 0;
  l.boot(true, ApMode::Always, t);
  for (int i = 0; i < 6; ++i) {
    CHECK(l.tick(t = l.nextAttemptAt(), false).connect);
    l.disconnected(t += kSec);
  }
  CHECK_EQ(l.failures(), 6);
  CHECK(l.tick(t = l.nextAttemptAt(), false).connect);
  l.configure(true, ApMode::Always, true, t += 100);  // new password while joining
  CHECK_EQ(l.failures(), 0);
  CHECK(l.tick(t, false).connect);  // fresh join right away
  l.disconnected(t + 10);           // event from the attempt we cut short
  CHECK(l.attempting());
  CHECK_EQ(l.failures(), 0);
  l.connected(t + 2 * kSec);
  CHECK(l.up());
}

void disablingDropsTheLink() {
  Link l;
  l.boot(true, ApMode::Fallback, 0);
  l.tick(0, false);
  l.connected(kSec);
  l.configure(false, ApMode::Fallback, false, 2 * kSec);
  CHECK(!l.up());
  l.disconnected(2 * kSec + 5);
  l.tick(3 * kSec, false);
  CHECK(l.apOn());
  l.connected(4 * kSec);  // a stray GOT_IP after leaving
  CHECK(!l.up());
}

void alwaysModeKeepsTheApOn() {
  Link l;
  l.boot(true, ApMode::Always, 0);
  CHECK(l.apOn());
  l.tick(0, false);
  l.connected(kSec);
  l.tick(kSec + Link::kApOffAfterMs + 1, false);
  CHECK(l.apOn());
}

void fallbackAtBootComesUpAfter30sWithoutHome() {
  Link l;
  l.boot(true, ApMode::Fallback, 0);
  CHECK(!l.apOn());
  l.tick(0, false);
  l.disconnected(5 * kSec);
  l.tick(Link::kBootGraceMs - 1, false);
  CHECK(!l.apOn());
  l.tick(Link::kBootGraceMs, false);
  CHECK(l.apOn());
}

void fallbackBootJoinKeepsApOff() {
  Link l;
  l.boot(true, ApMode::Fallback, 0);
  l.tick(0, false);
  l.connected(4 * kSec);
  for (int64_t t = 0; t < 120 * kSec; t += 250) l.tick(t, false);
  CHECK(!l.apOn());
}

void fallbackLossBringsApBackAfter60sAndOffAfterRejoin() {
  Link l;
  int64_t t = 0;
  l.boot(true, ApMode::Fallback, t);
  l.tick(t, false);
  l.connected(t += kSec);
  l.tick(t += Link::kApOffAfterMs, false);
  CHECK(!l.apOn());
  const int64_t lost = t += 100 * kSec;
  l.disconnected(lost);
  // Retries keep failing; the AP stays dark for the full grace period.
  while (t < lost + Link::kLostGraceMs - 250) {
    t += 250;
    if (l.tick(t, false).connect) l.disconnected(t + 100);
    CHECK(!l.apOn());
  }
  l.tick(lost + Link::kLostGraceMs, false);
  CHECK(l.apOn());
  // Back home: the AP goes off only after the link has held for a while.
  t = l.nextAttemptAt();
  CHECK(l.tick(t, false).connect);
  l.connected(t);
  l.tick(t + Link::kApOffAfterMs - 1, false);
  CHECK(l.apOn());
  l.tick(t + Link::kApOffAfterMs, false);
  CHECK(!l.apOn());
}

void shortOutageDoesNotBringTheApBack() {
  Link l;
  l.boot(true, ApMode::Fallback, 0);
  l.tick(0, false);
  l.connected(kSec);
  l.tick(30 * kSec, false);
  CHECK(!l.apOn());
  l.disconnected(40 * kSec);
  CHECK(l.tick(42 * kSec, false).connect);
  l.connected(45 * kSec);
  l.tick(40 * kSec + Link::kLostGraceMs + 1, false);
  CHECK(!l.apOn());
}

void switchingToFallbackWaitsBeforeTurningTheApOff() {
  Link l;
  l.boot(true, ApMode::Always, 0);
  l.tick(0, false);
  l.connected(kSec);
  const int64_t t = 600 * kSec;
  l.configure(true, ApMode::Fallback, false, t);
  l.tick(t + 1, false);
  CHECK(l.apOn());
  l.tick(t + Link::kApOffAfterMs, false);
  CHECK(!l.apOn());
  // Disabling home Wi-Fi brings Keyra's own Wi-Fi straight back.
  l.configure(false, ApMode::Fallback, false, t + 100 * kSec);
  l.tick(t + 100 * kSec, false);
  CHECK(l.apOn());
}

void changingNetworkWhileApOffBringsItBackIfTheNewOneFails() {
  Link l;
  int64_t t = 0;
  l.boot(true, ApMode::Fallback, t);
  l.tick(t, false);
  l.connected(t += kSec);
  l.tick(t += Link::kApOffAfterMs, false);
  CHECK(!l.apOn());
  l.configure(true, ApMode::Fallback, true, t += kSec);  // typo in the new password
  l.disconnected(t + 5);                                  // the old link's event
  const int64_t changed = t;
  while (t < changed + Link::kLostGraceMs) {
    t += 250;
    if (l.tick(t, false).connect) l.disconnected(t + 100);
  }
  CHECK(l.apOn());
}

}  // namespace

int main() {
  backoffDoublesFrom2sTo5min();
  disabledNeverJoinsAndKeepsApOn();
  retriesFollowTheScheduleAndResetOnSuccess();
  attemptTimeoutAbortsOnceAndLateEventIsNotDoubleCounted();
  scanHoldsNewAttemptsBack();
  rejoinResetsBackoffAndIgnoresTheOldLinksDisconnect();
  disablingDropsTheLink();
  alwaysModeKeepsTheApOn();
  fallbackAtBootComesUpAfter30sWithoutHome();
  fallbackBootJoinKeepsApOff();
  fallbackLossBringsApBackAfter60sAndOffAfterRejoin();
  shortOutageDoesNotBringTheApBack();
  switchingToFallbackWaitsBeforeTurningTheApOff();
  changingNetworkWhileApOffBringsItBackIfTheNewOneFails();
  return KEYRA_TEST_RESULT();
}
