#pragma once
// Wall-clock adoption from X-Keyra-Time (SPEC §5): the device has no RTC.
#include <cstdint>
#include <optional>
#include <string_view>

namespace keyra::api::clock {

constexpr int64_t kValidAfterMs = 1704067200000LL;   // 2024-01-01: anything earlier is "never set"
constexpr int64_t kValidBeforeMs = 4102444800000LL;  // 2100-01-01: reject absurd client clocks
constexpr int64_t kMaxDriftMs = 5000;
// Once the clock is valid, how far a session request may set it from the trusted time.
constexpr int64_t kMaxSessionSkewMs = 5 * 60 * 1000;

bool valid(int64_t unixMs);
std::optional<int64_t> parse(std::string_view header);

// The time the device last learnt from a trusted source, run forward on the
// monotonic clock. Trusted: setup, unlock (the passphrase or recovery key was
// right), SNTP, and the first time the clock is set at all. A session request
// may correct the clock only within kMaxSessionSkewMs of this, so repeated
// requests cannot walk it to harvest future 2FA codes or shift the activity
// log; they are measured against this estimate, never against what an earlier
// session request set.
class Trust {
 public:
  // Whether to set the clock to `clientMs` now. `networkSynced`: SNTP set the
  // clock recently, so no client is trusted over it.
  bool adopt(int64_t deviceMs, int64_t monoMs, int64_t clientMs, bool networkSynced, bool trusted);

 private:
  bool set_ = false;
  int64_t offsetMs_ = 0;  // trusted unix ms minus monotonic ms
};

}  // namespace keyra::api::clock
