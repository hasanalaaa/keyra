#pragma once
// Password health (SPEC §13): weak, reused and old passwords, worked out on the
// device so the phone only ever receives entry ids and flags. Pure, host-tested.
#include <cstddef>
#include <cstdint>
#include <string_view>
#include <utility>
#include <vector>

namespace keyra::api::health {

// The same estimate as the web app's strength meter (web/src/lib/strength.ts),
// so a password the phone calls weak is the one the device flags.
double bits(std::string_view password);
// 1 weak, 2 fair, 3 good, 4 strong (DESIGN §4.7 bands; common passwords are 1).
// 0 for an empty password.
int level(std::string_view password);

constexpr int kWeakBelow = 3;                          // levels 1 and 2 are flagged
constexpr int64_t kOldAfterSec = 365LL * 24 * 60 * 60;  // a year with the same password

struct Item {
  uint32_t id = 0;
  std::string_view password;
  int64_t setAt = 0;  // unix seconds the current password was set (0 = unknown)
};

struct Report {
  size_t checked = 0;                               // entries that have a password
  std::vector<std::pair<uint32_t, int>> weak;       // id, level
  std::vector<std::vector<uint32_t>> reused;        // groups of ≥ 2 ids sharing one password
  std::vector<std::pair<uint32_t, int64_t>> old;    // id, setAt
};

// `now`: unix seconds, or 0 when the clock is unknown (nothing is then "old").
Report check(const std::vector<Item>& items, int64_t now);

}  // namespace keyra::api::health
