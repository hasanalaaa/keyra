#include "button_classifier.hpp"

namespace keyra::io {

Press ButtonClassifier::update(bool raw, uint32_t now) {
  if (!started_) {
    // A press already under way at power-up (e.g. held through a reset) is
    // not a user gesture aimed at us: wait for a release first.
    started_ = true;
    stable_ = candidate_ = raw;
    candidateSince_ = now;
    ignorePress_ = raw;
    return Press::None;
  }

  if (raw != candidate_) {
    candidate_ = raw;
    candidateSince_ = now;
  }

  if (candidate_ != stable_ && now - candidateSince_ >= kDebounceMs) {
    stable_ = candidate_;
    // Durations are measured edge-to-edge (first raw transition of the
    // stable run), so debounce latency does not skew Short/Long.
    if (stable_) {
      pressedAt_ = candidateSince_;
      longFired_ = false;
      return Press::None;
    }
    const bool ignored = ignorePress_;
    ignorePress_ = false;
    if (ignored || longFired_) return Press::None;
    return (candidateSince_ - pressedAt_ < kShortMaxMs) ? Press::Short : Press::None;
  }

  // candidate_ too: a release that is still being debounced is not "held".
  if (stable_ && candidate_ && !ignorePress_ && !longFired_ && now - pressedAt_ >= kLongMs) {
    longFired_ = true;
    return Press::Long;
  }
  return Press::None;
}

}  // namespace keyra::io
