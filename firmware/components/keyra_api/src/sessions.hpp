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
#include <vector>

namespace keyra::api {

constexpr size_t kMaxSessions = 4;
constexpr size_t kTokenBytes = 32;
// A press approves one request; the session must make it within this long (SPEC §12.5b).
constexpr int64_t kApprovalMs = 30000;

// Length-revealing but content-constant-time comparison (tokens have fixed length).
bool constantTimeEqual(std::string_view a, std::string_view b);

class Sessions {
 public:
  using Random = std::function<void(uint8_t*, size_t)>;
  explicit Sessions(Random rng) : rng_(std::move(rng)) {}

  struct Issued {
    std::string token, csrf;
    std::string evicted;  // the session pushed out to make room (empty = none)
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
  // What one press approved (SPEC §12.5b): exactly one request of this
  // session, named by what it does, the resource and, for an account, the
  // version of it the press was asked for. Used up by that request, never by
  // another one, and gone after kApprovalMs, with the session, or when the
  // session's next press approves something else.
  struct Approval {
    enum class Purpose : uint8_t { RevealEntry, RevealHealth, Backup, RecoveryCreate, RecoveryRemove, TokenCreate, TagCreate };
    Purpose purpose = Purpose::RevealEntry;
    uint32_t resource = 0;  // RevealEntry: the entry id
    uint32_t revision = 0;  // RevealEntry: vault::Entry::revision when the press was asked for
    std::string request;    // TokenCreate/TagCreate: digest of the request body, so the press approves that one
  };
  enum class Redeem { Ok, None, Changed };
  // Stores `a` for this live session (replacing an unused one); false when the session is gone.
  bool approve(std::string_view token, int64_t nowMs, Approval a);
  // Ok (and the approval is used up) when this session holds an unexpired
  // approval for exactly `want`. Changed (and it is dropped) when it names the
  // same request but another revision of the account: the account was edited
  // after the press was asked for. None otherwise; an approval for something
  // else stays for its own request.
  Redeem redeem(std::string_view token, int64_t nowMs, const Approval& want);
  // Revoking a trusted browser ends the sessions it opened; returns their
  // tokens, so what they left waiting for the button can be withdrawn too.
  std::vector<std::string> endTrusted(uint32_t trustId);
  // Whether a request admitted for `token` in unlock `generation` may still
  // act: the session is live, the vault unlocked and not unlocked again since.
  // Work deferred to another task re-checks this before it changes anything.
  bool stillValid(std::string_view token, uint32_t generation, uint32_t currentGeneration, bool unlocked);
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
    std::optional<Approval> approval;
    int64_t approvalUntil = 0;
  };
  Slot* findLocked(std::string_view token);
  std::string randomHex();

  Random rng_;
  std::mutex mu_;
  std::array<Slot, kMaxSessions> slots_;
  int64_t lastActivity_ = 0;
};

}  // namespace keyra::api
