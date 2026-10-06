#pragma once
// Home-network supervisor (SPEC §8.2): when to try joining, how long to back off,
// and whether Keyra's own AP should be on. Pure C++ with explicit time so the
// policy is host-tested; net.cpp turns its answers into driver calls. This is
// the only place that decides to (re)connect: the driver never auto-reconnects.
#include <cstdint>

#include "keyra/net_types.hpp"

namespace keyra::net {

// Delay before attempt n+1 after n consecutive failures: 2 s doubling to 5 min.
int64_t retryDelayMs(int failures);

class Link {
 public:
  static constexpr int64_t kFirstRetryMs = 2000;
  static constexpr int64_t kMaxRetryMs = 5 * 60 * 1000;
  // A join that has produced neither an IP nor a failure by then is abandoned.
  static constexpr int64_t kAttemptTimeoutMs = 20000;
  // apMode fallback: AP comes back after the home network has been unavailable
  // this long (boot: not joined yet; later: link lost).
  static constexpr int64_t kBootGraceMs = 30000;
  static constexpr int64_t kLostGraceMs = 60000;
  // ...and goes off only after the link has held this long, so the phone that
  // turned the setting on (often via the AP) still sees the result.
  static constexpr int64_t kApOffAfterMs = 15000;

  struct Plan {
    bool connect = false;  // call esp_wifi_connect() now (the attempt is already counted as started)
    bool abort = false;    // the attempt timed out: call esp_wifi_disconnect()
  };

  void boot(bool enabled, ApMode mode, int64_t now);
  // A new home config. `rejoin`: the network or password changed (or it was just
  // enabled), so a fresh join starts now with the backoff reset. When a join or
  // link was live, the caller disconnects it and the resulting event is ignored.
  void configure(bool enabled, ApMode mode, bool rejoin, int64_t now);
  void connected(int64_t now);     // got an IP
  void disconnected(int64_t now);  // attempt failed or link lost
  // `hold`: a scan is waiting, so no new attempt starts.
  Plan tick(int64_t now, bool hold);

  bool apOn() const { return apOn_; }
  bool up() const { return up_; }
  bool attempting() const { return attempting_; }
  int failures() const { return failures_; }
  int64_t nextAttemptAt() const { return nextAttemptAt_; }

 private:
  void failed(int64_t now);

  bool enabled_ = false;
  ApMode mode_ = ApMode::Always;
  bool up_ = false;
  int64_t upSince_ = 0;
  bool attempting_ = false;
  int64_t attemptAt_ = 0;
  bool ignoreNextDisconnect_ = false;
  int failures_ = 0;
  int64_t nextAttemptAt_ = 0;
  bool apOn_ = true;
  int64_t apDueAt_ = 0;
};

}  // namespace keyra::net
