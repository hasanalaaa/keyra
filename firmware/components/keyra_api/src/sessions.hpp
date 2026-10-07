#pragma once
// Browser sessions (SPEC §5): up to 4, each a 32-byte random cookie token plus a
// CSRF token. Plain C++ with injected randomness so it runs in host tests.
#include <array>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <mutex>
#include <optional>
#include <string>
#include <string_view>

namespace keyra::api {

constexpr size_t kMaxSessions = 4;
constexpr size_t kTokenBytes = 32;
// After a press, the session that asked may see secrets for this long (SPEC §10.3).
constexpr int64_t kGraceMs = 60000;

// Length-revealing but content-constant-time comparison (tokens have fixed length).
bool constantTimeEqual(std::string_view a, std::string_view b);

class Sessions {
 public:
  using Random = std::function<void(uint8_t*, size_t)>;
  explicit Sessions(Random rng) : rng_(std::move(rng)) {}

  struct Issued {
    std::string token, csrf;
  };
  // Evicts the least recently used session when all slots are taken.
  // `trustId`: the trusted browser (SPEC §8.2) this session was opened from, 0 = none.
  Issued create(int64_t nowMs, uint32_t trustId = 0);
  // The session's CSRF token when `token` is live; marks it used.
  std::optional<std::string> csrfFor(std::string_view token, int64_t nowMs);
  void clear();
  // Starts the reveal grace for this live session; false when it is gone.
  bool grantGrace(std::string_view token, int64_t nowMs);
  // Milliseconds of grace left for this session (0 = none or unknown token).
  int64_t graceLeft(std::string_view token, int64_t nowMs);
  // Revoking a trusted browser ends the sessions it opened.
  size_t endTrusted(uint32_t trustId);
  size_t size();

  // Idle tracking for auto-lock: any authenticated request or button use counts.
  void activity(int64_t nowMs);
  bool idleFor(int64_t nowMs, int64_t limitMs);

 private:
  struct Slot {
    bool used = false;
    std::string token, csrf;
    int64_t lastUsed = 0;
    uint32_t trustId = 0;
    int64_t graceUntil = 0;
  };
  Slot* findLocked(std::string_view token);
  std::string randomHex();

  Random rng_;
  std::mutex mu_;
  std::array<Slot, kMaxSessions> slots_;
  int64_t lastActivity_ = 0;
};

}  // namespace keyra::api
