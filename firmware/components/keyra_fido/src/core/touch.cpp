#include "touch.hpp"

namespace keyra::fido {

void TouchGate::begin(Phase p) {
  std::lock_guard<std::mutex> g(mu_);
  phase_ = p;
  answer_ = Answer::None;
  u2f_ = false;
}

void TouchGate::end() {
  std::lock_guard<std::mutex> g(mu_);
  phase_ = Phase::Idle;
  answer_ = Answer::None;
  u2f_ = false;
}

TouchGate::Answer TouchGate::answer() {
  std::lock_guard<std::mutex> g(mu_);
  return answer_;
}

bool TouchGate::takeLatched(int64_t now) {
  std::lock_guard<std::mutex> g(mu_);
  if (u2f_ && answer_ == Answer::Approved && now - answeredAt_ <= kLatchMs) {
    phase_ = Phase::Idle;
    answer_ = Answer::None;
    u2f_ = false;
    return true;
  }
  if (u2f_ && answer_ == Answer::Denied && now - answeredAt_ <= kLatchMs) {
    return false;  // refused: stay quiet until the refusal lapses, then ask again
  }
  if (phase_ == Phase::Idle || u2f_) {  // never hijack a blocking CTAP2 wait
    phase_ = Phase::Presence;
    if (!u2f_ || answer_ != Answer::None) answer_ = Answer::None;
    u2f_ = true;
    u2fUntil_ = now + kPollGapMs;
  }
  return false;
}

void TouchGate::tick(int64_t now) {
  std::lock_guard<std::mutex> g(mu_);
  if (u2f_ && now > u2fUntil_ && !(answer_ != Answer::None && now - answeredAt_ <= kLatchMs)) {
    phase_ = Phase::Idle;
    answer_ = Answer::None;
    u2f_ = false;
  }
}

bool TouchGate::awaiting() {
  std::lock_guard<std::mutex> g(mu_);
  return phase_ != Phase::Idle && answer_ == Answer::None;
}

void TouchGate::press(bool shortPress, int64_t now) {
  std::lock_guard<std::mutex> g(mu_);
  if (phase_ == Phase::Idle || answer_ != Answer::None) return;
  if (!shortPress) {
    answer_ = Answer::Denied;
  } else if (phase_ == Phase::Presence) {
    answer_ = Answer::Approved;
  } else {
    return;  // waiting for an unlock: a short press cannot stand in for it
  }
  answeredAt_ = now;
}

}  // namespace keyra::fido
