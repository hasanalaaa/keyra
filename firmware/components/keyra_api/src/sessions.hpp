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
  Issued create(int64_t nowMs);
  // The session's CSRF token when `token` is live; marks it used.
  std::optional<std::string> csrfFor(std::string_view token, int64_t nowMs);
  void clear();
  size_t size();

  // Idle tracking for auto-lock: any authenticated request or button use counts.
  void activity(int64_t nowMs);
  bool idleFor(int64_t nowMs, int64_t limitMs);

 private:
  struct Slot {
    bool used = false;
    std::string token, csrf;
    int64_t lastUsed = 0;
  };
  std::string randomHex();

  Random rng_;
  std::mutex mu_;
  std::array<Slot, kMaxSessions> slots_;
  int64_t lastActivity_ = 0;
};

}  // namespace keyra::api
