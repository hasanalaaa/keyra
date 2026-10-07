#pragma once
// Bluetooth LE keyboard (HID over GATT) for Keyra (SPEC §8.1). NimBLE,
// peripheral only, one host at a time. Bonding happens only inside a 120 s
// pairing window that the API opens after a button press; outside it Keyra
// advertises only to bonded hosts and refuses new pairings.
//
// Connect::OnDemand (default): idle Keyra neither advertises nor holds a link;
// an armed action wants one bonded host (want()), Keyra advertises to that
// host alone until it connects, and lets go ~20 s after typing (done()) or at
// once on cancel/expiry (drop()). Connect::Always: bonded hosts may reconnect
// whenever they are around.
//
// Thread-safe: every call may come from any task. Stack operations run on the
// NimBLE host task; these functions only post to it or read a snapshot.
#include <array>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "esp_err.h"

namespace keyra::ble {

// Identity address of a host, most significant byte first (as displayed).
using Addr = std::array<uint8_t, 6>;

constexpr size_t kMaxBonds = 4;
constexpr uint32_t kPairingWindowMs = 120000;

enum class Connect { OnDemand, Always };

struct Peer {
  Addr addr{};
  std::string name;      // the host's own GAP device name; empty until read
  int64_t lastSeen = 0;  // unix seconds of the last connection; 0 = unknown clock
};

struct Status {
  bool enabled = false;
  bool pairing = false;
  int64_t pairingLeftMs = 0;
  std::optional<Peer> connected;  // only a bonded host on an encrypted link counts
  std::vector<Peer> bonds;
};

// Starts NimBLE once. A failure leaves Bluetooth unavailable (logged); USB
// typing is unaffected.
esp_err_t init(const std::string& deviceName, bool enabled, Connect mode);
void setEnabled(bool enabled);              // off: stop advertising, drop the link
void setName(const std::string& deviceName);
void setConnect(Connect mode);

// Typing demand (driven by the pending action in keyra_api).
void want(const Addr& host);  // an action is armed for this bonded host
void done();                  // typed (or failed): keep the link briefly, then let go
void drop();                  // cancelled / expired / locked: let go now
std::optional<Addr> linked(); // the bonded host on an encrypted link, if any

enum class PairResult { Ok, Disabled, BondsFull, Unavailable };
PairResult canPair();       // checked before arming the button press
PairResult openPairing();   // opens the 120 s window (call after the press)
void closePairing();        // lock / factory reset: no window may outlive them
bool pairing();             // window open (cheap; polled for the LED)
Status status();

// Typing transport (used by keyra_hid).
bool ready();     // bonded host connected, link encrypted, keyboard reports subscribed
bool capsLock();  // from the host's LED output report
bool numLock();   // likewise
// One 8-byte boot-keyboard report. Waits ≤100 ms for a free buffer; false
// when the link is gone or the host stopped taking reports.
bool sendKey(uint8_t modifier, uint8_t keycode);

// Removes the bond (and disconnects that host). ESP_ERR_NOT_FOUND if unknown.
esp_err_t forget(const Addr& addr);
esp_err_t forgetAll();  // factory reset

std::string formatAddr(const Addr& a);              // "A4:C1:38:0B:7F:3A"
bool parseAddr(std::string_view s, Addr& out);      // case-insensitive, colons required

}  // namespace keyra::ble
