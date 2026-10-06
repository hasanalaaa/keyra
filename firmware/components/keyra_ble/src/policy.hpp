#pragma once
// The decisions of keyra_ble as pure functions (host-tested): what to
// advertise, who may pair, the pairing window, name and address text.
#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>

#include "keyra/ble.hpp"

namespace keyra::ble {

enum class Adv {
  Off,
  Open,        // pairing window: discoverable, anyone may connect
  BondedOnly,  // not discoverable; the controller's filter accept list admits bonded hosts only
};

// One host at a time: nothing is advertised while a link is up.
Adv advertising(bool enabled, bool pairing, size_t bonds, bool connected);

// Whether a pairing request may proceed. `known`: this host already has a
// bond here and is re-pairing (its old keys get replaced, so it never needs a
// free slot). New hosts need the window and a free slot.
bool mayPair(bool windowOpen, bool known, size_t bonds);

// Whether a freshly connected host may stay connected before it encrypts.
// Outside the window only bonded hosts get through the accept list; this is
// the same rule checked again in software.
bool mayConnect(bool enabled, bool windowOpen, bool known);

class Window {
 public:
  void open(int64_t nowMs) { deadline_ = nowMs + kPairingWindowMs; }
  void close() { deadline_ = 0; }
  bool active(int64_t nowMs) const { return deadline_ != 0 && nowMs < deadline_; }
  int64_t leftMs(int64_t nowMs) const { return active(nowMs) ? deadline_ - nowMs : 0; }

 private:
  int64_t deadline_ = 0;
};

// Longest prefix of a UTF-8 name within `maxBytes` that does not split a
// character (advertising packets are 31 bytes; names may be Arabic).
std::string_view fitName(std::string_view name, size_t maxBytes);

// A host's self-reported name made safe to store and show: invalid UTF-8 and
// control characters dropped, outer spaces trimmed, then fitName().
std::string cleanName(std::string_view raw, size_t maxBytes);

}  // namespace keyra::ble
