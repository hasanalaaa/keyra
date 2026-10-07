#pragma once
// The button side of a FIDO request, shared by the FIDO task (asks) and the
// actions task (owns the button, keyra_api/runtime.cpp). Thread-safe.
//
// While a request waits (for an unlock or a press) the LED shows the FIDO
// pattern and the button belongs to it: a short press approves (only when the
// vault is unlocked, i.e. while waiting for presence), a long press refuses.
//
// U2F has no keepalive: the host polls, so a press is latched and handed to
// the next poll if it comes within kLatchMs; the prompt itself lapses kPollGapMs
// after the last poll (the host gave up).
#include <cstdint>
#include <mutex>

namespace keyra::fido {

class TouchGate {
 public:
  enum class Phase { Idle, Unlock, Presence };
  enum class Answer { None, Approved, Denied };
  static constexpr int64_t kPollGapMs = 3000;
  static constexpr int64_t kLatchMs = 3000;

  // Blocking requests (CTAP2): begin → poll answer() → end.
  void begin(Phase p);
  void end();
  Answer answer();

  // U2F: true consumes a fresh press; otherwise (re)starts the prompt.
  bool takeLatched(int64_t nowMs);
  void tick(int64_t nowMs);  // ends a U2F prompt nobody polls any more

  // Button routing (actions task).
  bool awaiting();
  void press(bool shortPress, int64_t nowMs);

 private:
  std::mutex mu_;
  Phase phase_ = Phase::Idle;
  Answer answer_ = Answer::None;
  bool u2f_ = false;
  int64_t u2fUntil_ = 0;
  int64_t answeredAt_ = 0;
};

}  // namespace keyra::fido
