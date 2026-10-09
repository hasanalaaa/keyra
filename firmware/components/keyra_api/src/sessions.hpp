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
// After a press, the session that asked may see secrets for this long (SPEC §12.3).
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
  // `generation`: the vault unlock it belongs to (vault::unlockGeneration()).
  Issued create(int64_t nowMs, uint32_t trustId = 0, uint32_t generation = 0);
  // The session's CSRF token when `token` is live and from this unlock
  // `generation`; marks it used. A token from an earlier unlock is dead even
  // if the vault was locked without clear() (an internal lock).
  std::optional<std::string> csrfFor(std::string_view token, int64_t nowMs, uint32_t generation = 0);
  void clear();
  // What a press let this session do (SPEC §12.3). A press grants the one it
  // was asked for: revealing secrets for 60 s, or one backup / one recovery-key
  // change, which use their press up (consumeGrace).
  // Token: create one access token (SPEC §17), used up like Recovery.
  enum class Grace { Reveal, Backup, Recovery, Token };
  // Starts that grace for this live session; false when it is gone.
  bool grantGrace(std::string_view token, int64_t nowMs, Grace g);
  // Milliseconds of that grace left for this session (0 = none or unknown token).
  int64_t graceLeft(std::string_view token, int64_t nowMs, Grace g);
  // True (and the grace ends) when this session has that grace now.
  bool consumeGrace(std::string_view token, int64_t nowMs, Grace g);
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
    uint32_t generation = 0;
    std::array<int64_t, 4> graceUntil{};  // by Grace
  };
  Slot* findLocked(std::string_view token);
  std::string randomHex();

  Random rng_;
  std::mutex mu_;
  std::array<Slot, kMaxSessions> slots_;
  int64_t lastActivity_ = 0;
};

}  // namespace keyra::api
