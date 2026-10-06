#include "link.hpp"

namespace keyra::net {

int64_t retryDelayMs(int failures) {
  if (failures <= 1) return Link::kFirstRetryMs;
  int64_t d = Link::kFirstRetryMs;
  for (int i = 1; i < failures && d < Link::kMaxRetryMs; ++i) d *= 2;
  return d < Link::kMaxRetryMs ? d : Link::kMaxRetryMs;
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

void Link::disconnected(int64_t now) {
  if (ignoreNextDisconnect_) {
    ignoreNextDisconnect_ = false;
    return;
  }
  if (up_) {
    up_ = false;
    apDueAt_ = now + kLostGraceMs;
    failed(now);
  } else if (attempting_) {
    attempting_ = false;
    failed(now);
  }
  // Otherwise a late event for an attempt already given up on: nothing to count.
}

void Link::failed(int64_t now) {
  ++failures_;
  nextAttemptAt_ = now + retryDelayMs(failures_);
}

Link::Plan Link::tick(int64_t now, bool hold) {
  Plan p;
  if (!enabled_) {
    apOn_ = true;
    return p;
  }
  if (attempting_ && now - attemptAt_ >= kAttemptTimeoutMs) {
    attempting_ = false;
    failed(now);
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
