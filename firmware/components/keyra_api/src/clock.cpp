#include "clock.hpp"

namespace keyra::api::clock {

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

bool shouldAdopt(int64_t deviceMs, int64_t clientMs, bool networkSynced) {
  if (!valid(clientMs) || (networkSynced && valid(deviceMs))) return false;
  if (!valid(deviceMs)) return true;
  const int64_t drift = deviceMs > clientMs ? deviceMs - clientMs : clientMs - deviceMs;
  return drift > kMaxDriftMs;
}

}  // namespace keyra::api::clock
