#pragma once
// Wall-clock adoption from X-Keyra-Time (SPEC §5): the device has no RTC.
#include <cstdint>
#include <optional>
#include <string_view>

namespace keyra::api::clock {

constexpr int64_t kValidAfterMs = 1704067200000LL;   // 2024-01-01: anything earlier is "never set"
constexpr int64_t kValidBeforeMs = 4102444800000LL;  // 2100-01-01: reject absurd client clocks
constexpr int64_t kMaxDriftMs = 5000;

bool valid(int64_t unixMs);
std::optional<int64_t> parse(std::string_view header);
// `networkSynced`: SNTP set the clock recently, so the client is not trusted over it.
bool shouldAdopt(int64_t deviceMs, int64_t clientMs, bool networkSynced = false);

}  // namespace keyra::api::clock
