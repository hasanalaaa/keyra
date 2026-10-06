#pragma once
// The decisions of keyra_ble as pure functions (host-tested): what to
// advertise, who may pair, the pairing window, name and address text.
#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>

#include "keyra/ble.hpp"

namespace keyra::ble {

enum class Adv {
  Off,
  Open,        // pairing window: discoverable, anyone may connect
  BondedOnly,  // not discoverable; the controller's filter accept list admits bonded hosts only
};

// One host at a time: nothing is advertised while a link is up. `wanted`: an
// armed action is waiting for one bonded host (then only that host is put on
// the accept list). OnDemand advertises for nothing else; Always also lets
// any bonded host reconnect while idle.
Adv advertising(bool enabled, bool pairing, size_t bonds, bool connected, Connect mode, bool wanted);

// Whether an established link may stay up. Untrusted links (not yet bonded and
// encrypted) only live inside the pairing window. A trusted link must be the
// wanted host when an action waits for one; with nothing wanted, OnDemand
// drops it (iOS hides its on-screen keyboard while a keyboard is connected).
bool keepLink(Connect mode, bool pairing, bool trusted, const std::optional<Addr>& wanted, const Addr& peer);

// The host Keyra should be connected to for typing: set when an action is
// armed for it, held kLingerMs after typing so a quick second action reuses
// the link, cleared at once on cancel / expiry / lock.
class Demand {
 public:
  static constexpr int64_t kLingerMs = 20000;
  void want(const Addr& a) {
    addr_ = a;
    active_ = true;
    lingerUntil_ = 0;
  }
  void done(int64_t nowMs) {
    if (!active_) return;
    active_ = false;
    lingerUntil_ = nowMs + kLingerMs;
  }
  void drop() {
    active_ = false;
    lingerUntil_ = 0;
  }
  std::optional<Addr> target(int64_t nowMs) const {
    if (active_ || nowMs < lingerUntil_) return addr_;
    return std::nullopt;
  }
  int64_t lingerLeftMs(int64_t nowMs) const { return !active_ && nowMs < lingerUntil_ ? lingerUntil_ - nowMs : 0; }

 private:
  Addr addr_{};
  bool active_ = false;
  int64_t lingerUntil_ = 0;
};

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
