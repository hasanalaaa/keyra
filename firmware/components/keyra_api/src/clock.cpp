#include "clock.hpp"

namespace keyra::api::clock {
namespace {

int64_t distance(int64_t a, int64_t b) { return a > b ? a - b : b - a; }

}  // namespace

bool valid(int64_t unixMs) { return unixMs >= kValidAfterMs && unixMs < kValidBeforeMs; }

std::optional<int64_t> parse(std::string_view h) {
  if (h.empty() || h.size() > 15) return std::nullopt;
  int64_t v = 0;
  for (char c : h) {
    if (c < '0' || c > '9') return std::nullopt;
    v = v * 10 + (c - '0');
  }
  return v;
}

bool Trust::adopt(int64_t deviceMs, int64_t monoMs, int64_t clientMs, bool networkSynced, bool trusted) {
  if (networkSynced && valid(deviceMs)) {
    set_ = true;
    offsetMs_ = deviceMs - monoMs;
    return false;
  }
  if (!valid(clientMs)) return false;
  if (!valid(deviceMs) || trusted) {
    set_ = true;
    offsetMs_ = clientMs - monoMs;
    return !valid(deviceMs) || distance(deviceMs, clientMs) > kMaxDriftMs;
  }
  if (!set_) {  // set by SNTP before any request saw it synced
    set_ = true;
    offsetMs_ = deviceMs - monoMs;
  }
  return distance(deviceMs, clientMs) > kMaxDriftMs && distance(offsetMs_ + monoMs, clientMs) <= kMaxSessionSkewMs;
}

}  // namespace keyra::api::clock
