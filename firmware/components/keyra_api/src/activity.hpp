#pragma once
// Activity log (SPEC §15): what happened on this Keyra, newest last, at most
// kMaxEvents. Never a secret: entry titles and device names at most. Stored
// encrypted in the vault (vault::activityRead/Write), so it is only readable,
// and only grows, while unlocked. Encoding and trimming here are pure and
// host-tested; log() (activity_log.cpp) does the I/O.
#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace keyra::api::activity {

// Values are stored: append only, never renumber.
enum class Kind : uint8_t {
  Unlock = 1,          // detail: 0 passphrase, 1 recovery key
  FailedUnlocks = 2,   // n: wrong passphrases/keys tried before this unlock
  Lock = 3,            // detail: LockWhy
  Typed = 4,           // id + title; detail: 0 USB, 1 Bluetooth
  Revealed = 5,        // id + title
  Backup = 6,
  Restore = 7,         // n: accounts added or updated
  Passphrase = 8,
  RecoveryCreated = 9,
  RecoveryRemoved = 10,
  BleForgot = 11,      // title: device name or address
  TrustedRemoved = 12, // title: browser name
  EntryDeleted = 13,   // id + title
  TextTyped = 14,      // detail as Typed; the text itself is never logged
  BlePairing = 15,     // the pairing window was opened with a press
  RotateStarted = 16,  // "change every password" (SPEC §13.1) started
  RotateEnded = 17,    // ... and ended
  EntryBurned = 18,    // id + title: deleted after its last allowed typing (SPEC §16)
};

enum class LockWhy : uint8_t { Manual = 0, Idle = 1, Usb = 2, Ble = 3, Button = 4 };

struct Event {
  Kind kind = Kind::Unlock;
  int64_t at = 0;       // unix seconds, 0 = clock unknown
  uint32_t id = 0;      // entry id
  uint32_t n = 0;       // a count
  uint8_t detail = 0;
  std::string title;    // ≤ kMaxTitle bytes, cut on a UTF-8 boundary
};

constexpr size_t kMaxEvents = 200;
constexpr size_t kMaxTitle = 64;

std::vector<uint8_t> encode(const std::vector<Event>& events);
// False on a malformed record (the caller starts a new log rather than lose
// the ability to log); unknown kinds are kept as they are.
bool decode(const std::vector<uint8_t>& bytes, std::vector<Event>& out);
// Appends `e` (title clipped) and drops the oldest beyond kMaxEvents.
void append(std::vector<Event>& events, Event e);

const char* kindName(Kind k);  // stable API token, e.g. "typed"

// activity_log.cpp (device only). log() stamps the time and appends while the
// vault is unlocked; when it is locked the event is dropped (nothing can be
// written without the key). Never fails loudly: logging must not break the
// action it records.
void log(Event e);
void log(Kind k, uint32_t id = 0, std::string title = {}, uint8_t detail = 0, uint32_t n = 0);
bool list(std::vector<Event>& out);  // false when the stored log cannot be read

}  // namespace keyra::api::activity
