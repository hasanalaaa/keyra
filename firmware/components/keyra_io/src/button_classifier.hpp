#pragma once
// BOOT-button press classifier. Pure C++ (host-tested); the GPIO task feeds it
// a raw sample every 10 ms.
#include <cstdint>

namespace keyra::io {

enum class Press { None, Short, Long };

class ButtonClassifier {
 public:
  static constexpr uint32_t kDebounceMs = 30;
  static constexpr uint32_t kShortMaxMs = 600;   // Short = released before this
  static constexpr uint32_t kLongMs = 1500;      // Long = held this long (fires while held)

  // rawPressed: electrical level (GPIO0 low). nowMs: monotonic, may wrap.
  Press update(bool rawPressed, uint32_t nowMs);

 private:
  bool started_ = false;
  bool stable_ = false;          // debounced level
  bool candidate_ = false;       // last raw level
  uint32_t candidateSince_ = 0;  // when the raw level last changed
  uint32_t pressedAt_ = 0;       // edge time of the debounced press
  bool longFired_ = false;
  bool ignorePress_ = false;     // press already in progress at boot
};

}  // namespace keyra::io
