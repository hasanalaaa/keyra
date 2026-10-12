#include "link.hpp"

namespace keyra::net {

int64_t retryDelayMs(int failures, int64_t cap) {
  if (failures <= 1) return Link::kFirstRetryMs < cap ? Link::kFirstRetryMs : cap;
  int64_t d = Link::kFirstRetryMs;
  for (int i = 1; i < failures && d < cap; ++i) d *= 2;
  return d < cap ? d : cap;
}

void Link::boot(bool enabled, ApMode mode, int64_t now) {
  enabled_ = enabled;
  mode_ = mode;
  up_ = attempting_ = ignoreNextDisconnect_ = false;
  failures_ = 0;
  nextAttemptAt_ = now;
  // Fallback starts dark and lights the AP only if the join doesn't happen in time.
  apOn_ = !(enabled && mode == ApMode::Fallback);
  apDueAt_ = now + kBootGraceMs;
}

void Link::configure(bool enabled, ApMode mode, bool rejoin, int64_t now) {
  const bool dropLive = (attempting_ || up_) && (rejoin || !enabled);
  if (dropLive) {
    ignoreNextDisconnect_ = true;
    attempting_ = up_ = false;
  }
  if (rejoin || !enabled) {
    failures_ = 0;
    nextAttemptAt_ = now;
  }
  enabled_ = enabled;
  mode_ = mode;
  // The AP never switches off as a direct result of a settings change: the
  // settle period restarts so the phone that made the change sees its outcome.
  if (up_) upSince_ = now;
  if (!up_) apDueAt_ = now + kLostGraceMs;
}

void Link::connected(int64_t now) {
  if (!enabled_) return;
  up_ = true;
  upSince_ = now;
  attempting_ = false;
  failures_ = 0;
}

void Link::disconnected(int64_t now, bool refused) {
  if (ignoreNextDisconnect_) {
    ignoreNextDisconnect_ = false;
    return;
  }
  if (up_) {
    up_ = false;
    apDueAt_ = now + kLostGraceMs;
    failed(now, false);  // a link that worked: the password is fine
  } else if (attempting_) {
    attempting_ = false;
    failed(now, refused);
  }
  // Otherwise a late event for an attempt already given up on: nothing to count.
}

void Link::lostWithoutEvent(int64_t now) {
  if (!up_) return;
  disconnected(now);
  ignoreNextDisconnect_ = true;
}

void Link::failed(int64_t now, bool refused) {
  ++failures_;
  nextAttemptAt_ = now + retryDelayMs(failures_, refused ? kMaxRefusedRetryMs : kMaxRetryMs);
}

Link::Plan Link::tick(int64_t now, bool hold) {
  Plan p;
  if (!enabled_) {
    apOn_ = true;
    return p;
  }
  if (attempting_ && now - attemptAt_ >= kAttemptTimeoutMs) {
    attempting_ = false;
    failed(now, false);
    p.abort = true;
  }
  if (!up_ && !attempting_ && !hold && now >= nextAttemptAt_) {
    attempting_ = true;
    attemptAt_ = now;
    p.connect = true;
  }
  if (mode_ == ApMode::Always) {
    apOn_ = true;
  } else if (up_ && now - upSince_ >= kApOffAfterMs) {
    apOn_ = false;
  } else if (!up_ && !apOn_ && now >= apDueAt_) {
    apOn_ = true;
  }
  return p;
}

}  // namespace keyra::net
