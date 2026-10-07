#pragma once
// Pending-action state machine (SPEC §5 "Button semantics"). Plain C++ with an
// injected clock so it runs in host tests; the ESP glue lives in action_runner.
//
// One slot holds at most one item: a type action or a presence-gated op.
// Arming either replaces whatever was in the slot. Items expire after 60 s.
#include <cstdint>
#include <functional>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <vector>

#include "keyra/sequence.hpp"
#include "target.hpp"

namespace keyra::actions {
using api::BtAddr;
}

namespace keyra::actions {

using api::Target;

constexpr int64_t kExpiryMs = 60000;
constexpr int64_t kFlashMs = 1500;  // Success/Error LED after a finished action
constexpr int64_t kBlinkMs = 150;   // "nothing to do" acknowledgement

// Sequence: an auto-type sequence (SPEC §10.4). Probe: the Layout Doctor's
// keyboard check (SPEC §10.3).
enum class What { Username, Password, Both, Totp, Test, Text, Sequence, Probe };
// HomeWifi: join/change/leave the home network (session). TrustBrowser: approve a
// browser that unlocks through the home network (no session yet, SPEC §8.2).
// BlePair: open the Bluetooth pairing window (session, SPEC §8.1).
// Reveal/Backup/Recovery: a press that lets this session read secrets, download
// a backup or create/remove the recovery key for a short grace (SPEC §12.3).
// Unprotect: turn "Protect reveal with Keyra's button" off.
enum class Op { Setup, Wifi, RestoreReplace, FactoryReset, HomeWifi, TrustBrowser, BlePair,
                Reveal, Backup, Recovery, Unprotect };
// NoUsb: output is USB-only and no computer is plugged in. NoHost: nothing
// connected on the selected output (auto or Bluetooth). HostChanged: the USB
// computer the action was armed for went away before the press (SPEC §12.4).
enum class Code { Typed, Cancelled, Expired, NoUsb, NoHost, UnsupportedChar, Failed, HostChanged };
enum class Button { Short, Long };
enum class Indicator { Setup, Locked, Idle, Pending, Typing, AwaitPresence, Success, Error, Off, Pairing };

// Free text for What::Text (SPEC §9.2). The slot and the typing job share this
// one buffer; its destructor wipes it, so the text leaves RAM as soon as the
// last holder lets go: after typing, cancel, expiry, replacement or lock.
struct FreeText {
  std::string text;
  bool twice = false;         // type it, the separator, then it again
  bool enterBetween = false;  // separator: Enter instead of Tab
  FreeText() = default;
  FreeText(const FreeText&) = delete;
  FreeText& operator=(const FreeText&) = delete;
  ~FreeText();
};

// The parsed sequence of a What::Sequence action, fixed when it is armed. Its
// literal text may be secret, so it is wiped with the last holder.
struct SeqJob {
  std::vector<seq::Step> steps;
  uint8_t parts = 1;    // {PRESS} splits it: each part needs its own button press
  std::string preview;  // masked, for the phone (seq::preview)
  SeqJob() = default;
  SeqJob(const SeqJob&) = delete;
  SeqJob& operator=(const SeqJob&) = delete;
  ~SeqJob() { seq::wipe(steps); }
};

struct TypeRequest {
  uint32_t id = 0;  // 0 for the test string and free text
  std::string title;
  What what = What::Username;
  bool submit = false;
  Target target;  // chosen when armed; a Bluetooth host must connect before the press counts
  std::shared_ptr<const FreeText> text;  // What::Text only
  std::shared_ptr<const SeqJob> seq;      // What::Sequence only
  uint8_t part = 0;                       // What::Sequence: the part the next press types
  // macOS/iOS host currently in a non-Latin input language: Ctrl+Space
  // before typing and again after (SPEC §10.5).
  bool switchLang = false;
  uint32_t usbSession = 0;  // set by arm(): the USB connection a USB action is bound to (0 = none)
};

struct Pending {
  TypeRequest req;  // never carries the free text: only the slot and the job do
  int64_t expiresInMs = 0;
};

struct Result {
  bool ok = false;
  Code code = Code::Failed;
  int64_t agoMs = 0;
  std::string title;
  What what = What::Username;
};

enum class OpCode { Done, Failed, Expired, Cancelled };

// Outcome of the last presence-gated op, so a client that armed it can tell a
// failed commit or an expiry apart from success once `presence` disappears.
struct OpResult {
  Op op = Op::Setup;
  OpCode code = OpCode::Done;
  int64_t agoMs = 0;
};

struct Presence {
  Op op = Op::Setup;
  bool awaiting = false;  // false while the approved op is running
  int64_t expiresInMs = 0;
};

// The presence op body. It owns any secrets it needs and must wipe them in its
// captured objects' destructors, because a cancelled/expired op is just dropped.
using Commit = std::function<bool()>;

enum class Effect { None, Blink, Run, Approve, Cancelled, Lock };

struct Decision {
  Effect effect = Effect::None;
  TypeRequest run;  // Effect::Run
  Op op = Op::Setup;
  Commit commit;    // Effect::Approve: caller runs it, then calls commitFinished()
};

class Machine {
 public:
  using Clock = std::function<int64_t()>;  // monotonic milliseconds
  explicit Machine(Clock now) : now_(std::move(now)) {}

  Pending arm(TypeRequest req);
  bool cancel();  // pending type action → last = cancelled
  int64_t awaitPresence(Op op, Commit commit);  // replaces whatever is pending
  // For ops requested without a session (setup, factory reset): never displaces
  // another item, so a stranger on the Wi-Fi cannot swap the action a user is
  // about to approve. nullopt when the slot or the committer is busy.
  std::optional<int64_t> tryAwaitPresence(Op op, Commit commit);
  // Lock ends every session, so items armed through a session must not outlive it.
  void dropSessionItems();

  // Whether the armed action's Bluetooth host is connected and ready. Until it
  // is, a short press does nothing (the action stays armed) and expiry
  // reports no_host instead of expired.
  void setLinkReady(bool ready);
  // USB host presence (enumerated and not suspended), polled by the runtime.
  // Each connection gets a new number; a USB action armed while one was up is
  // bound to it and ends with host_changed as soon as that connection ends,
  // so it can never type into the next computer (or the same one after a
  // re-plug). Armed with nothing plugged in, it stays unbound (no_usb at the press).
  void setUsbMounted(bool mounted);
  // Effect::Run moves the request (with any free text) out to the caller.
  Decision onButton(Button b, bool unlocked);
  // A sequence part typed fine with more to come: the action is armed again
  // for its next part (fresh 60 s), unless something else was armed meanwhile.
  void typingFinished(const TypeRequest& req, Code code);
  void commitFinished(bool ok);

  std::optional<Pending> pending();
  std::optional<Result> last();
  std::optional<Presence> presence();
  std::optional<OpResult> opResult();
  // `blePairing`: the Bluetooth pairing window is open. It shows only when
  // nothing more urgent (a prompt, typing, a result flash) does.
  Indicator indicator(bool initialized, bool unlocked, bool blePairing);

 private:
  enum class Kind { None, Type, Presence };
  void expireLocked(int64_t now);
  void clearSlotLocked();
  void flashLocked(Indicator ind, int64_t now, int64_t ms);
  void recordOpLocked(Op op, OpCode code, int64_t at);

  Clock now_;
  std::mutex mu_;
  Kind kind_ = Kind::None;
  TypeRequest req_;
  Op op_ = Op::Setup;
  Commit commit_;
  int64_t deadline_ = 0;
  bool typing_ = false;
  bool linkReady_ = false;
  bool usbMounted_ = false;
  uint32_t usbSession_ = 0;
  std::optional<Op> running_;
  bool hasLast_ = false;
  Result last_;
  int64_t lastAt_ = 0;
  std::optional<OpResult> opResult_;
  int64_t opResultAt_ = 0;
  Indicator flash_ = Indicator::Off;
  int64_t flashUntil_ = 0;
};

// Auto-lock when the computer the vault was used with goes away (SPEC §12.4).
// Pure policy; the runtime polls it about every 100 ms.
class HostWatch {
 public:
  static constexpr int64_t kUsbGoneMs = 1000;  // rides out a bus reset, still locks well within 2 s
  // True when the vault should lock now: it is unlocked, a USB host was seen
  // since it unlocked (so charger-only power never locks), and that host has
  // been gone (unplugged or suspended) for kUsbGoneMs.
  bool pollUsb(int64_t now, bool unlocked, bool usbMounted, bool enabled);
  // Keyra typed into this Bluetooth host while unlocked.
  void bleUsed(const BtAddr& addr) { bleUsed_ = addr; }
  // A bonded host's link ended without Keyra ending it. True when that was the
  // host Keyra typed into since the unlock and the setting is on.
  bool bleLost(const BtAddr& addr, bool unlocked, bool enabled);

 private:
  bool usbSeen_ = false;
  int64_t usbGoneAt_ = -1;
  std::optional<BtAddr> bleUsed_;
};

const char* whatName(What w);
std::optional<What> parseWhat(const std::string& s);
const char* opName(Op op);
const char* codeName(Code c);
const char* opCodeName(OpCode c);

}  // namespace keyra::actions
