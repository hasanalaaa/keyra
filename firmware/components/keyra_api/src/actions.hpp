#pragma once
// Pending-action state machine (SPEC §5 "Button semantics"). Plain C++ with an
// injected clock so it runs in host tests; the ESP glue lives in action_runner.
//
// One slot holds at most one item: a type action or a presence-gated op.
// Arming either replaces whatever was in the slot. Items expire after 60 s.
#include <array>
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
// A press made while the armed action's Bluetooth host is still connecting is
// kept this long: the host usually connects a moment later (SPEC §8.1).
constexpr int64_t kConnectLatchMs = 3000;

// Sequence: an auto-type sequence (SPEC §10.4). Probe: the Layout Doctor's
// keyboard check (SPEC §10.3).
enum class What { Username, Password, Both, Totp, Test, Text, Sequence, Probe };
// HomeWifi: join/change/leave the home network (session). TrustBrowser: approve a
// browser that unlocks through the home network (no session yet, SPEC §8.2).
// BlePair: open the Bluetooth pairing window (session, SPEC §8.1).
// Reveal/Backup/Recovery: a press that lets this session read secrets, download
// a backup or create/remove the recovery key for a short grace (SPEC §12.3).
// Unprotect: turn "Protect reveal with Keyra's button" off.
// DeleteEntry/DeletePasskey: remove one account or passkey (session, SPEC §5).
// PasskeysBackupOn: let backups carry the passkeys again (passkeysInBackup, SPEC §11).
// TokenCreate: let this session create one access token (SPEC §17).
// AgentSave: store the account an app token sent (owned by that token, SPEC §17).
// TagCreate: let this session create one NFC tap tag (SPEC §18).
enum class Op { Setup, Wifi, Restore, FactoryReset, HomeWifi, TrustBrowser, BlePair,
                Reveal, Backup, Recovery, Unprotect, Update, DeleteEntry, DeletePasskey, PasskeysBackupOn,
                TokenCreate, AgentSave, TagCreate };
// NoUsb: output is USB-only and no computer is plugged in. NoHost: nothing
// connected on the selected output (auto or Bluetooth). HostChanged: the USB
// computer the action was armed for went away before the press (SPEC §12.4).
// EntryChanged: the account was edited after the action was armed (SPEC §5).
enum class Code { Typed, Cancelled, Expired, NoUsb, NoHost, UnsupportedChar, Failed, HostChanged, EntryChanged };
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
  // An extension armed this for a page whose host does not match the login
  // (SPEC §9.4): that page's host, shown with the pending item. Else empty.
  std::string host{};
  // vault::Entry::revision when armed (entry actions and sequences): the press
  // types that version of the account or nothing (entryChanged()).
  uint32_t revision = 0;
  uint32_t usbSession = 0;  // set by arm(): the USB connection a USB action is bound to (0 = none)
  uint32_t serial = 0;      // set by arm(): names this request in track()
  std::string requestId{};  // set by arm(): names this request to its owner (SPEC §12.6a)
};

struct Pending {
  TypeRequest req;  // never carries the free text: only the slot and the job do
  int64_t expiresInMs = 0;
  std::string owner;  // who armed it (a session token or tokens::owner()); empty = none
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
  // Makes the secret that lets the requester (only) withdraw a presence op.
  using TokenGen = std::function<std::string()>;
  explicit Machine(Clock now, TokenGen token = {}) : now_(std::move(now)), token_(std::move(token)) {}

  // A presence op that now waits for the press, with the secret that lets its
  // requester (only) withdraw it (SPEC §12.6).
  struct Armed {
    int64_t expiresIn = 0;
    std::string cancel;
    uint32_t serial = 0;    // names this op in track()
    std::string requestId;  // names it to its owner (SPEC §12.6a)
  };

  // Where one armed item stands, by the serial arm() (Pending.req.serial) or
  // awaitPresence() (Armed.serial) gave it, so a token client can follow its
  // own request even when other results came after it (SPEC §17).
  struct Track {
    enum class Stage { Unknown, Armed, Connecting, Running, Finished } stage = Stage::Unknown;
    bool presence = false;
    Code code = Code::Failed;          // type, Finished
    OpCode opCode = OpCode::Failed;    // presence, Finished
    int64_t expiresInMs = 0;           // Armed / Connecting
    bool superseded = false;           // Finished as cancelled: a newer request of its owner took the slot
  };
  Track track(uint32_t serial);
  // The same by the requestId arm() or awaitPresence() handed out (SPEC
  // §12.6a), for `caller` only: the owner that armed it, or anyone when it
  // has no owner (setup, factory reset, trust: the id itself is unguessable).
  // nullopt: unknown, too long ago, or someone else's.
  std::optional<Track> trackRequest(const std::string& requestId, const std::string& caller);
  // What a cancel by requestId did. Superseded: that request had already been
  // replaced by a newer one of the same owner (the newer one is untouched).
  enum class Withdraw { Cancelled, Superseded, NotCancelled };

  // Each item belongs to whoever armed it (`owner`: the session token; empty =
  // no session). The slot is taken only when it is free or holds an item of
  // the same owner — never someone else's: otherwise a second browser could
  // swap the action the user is about to approve with the button. nullopt =
  // busy. A replaced item of the same owner ends as cancelled.
  std::optional<Pending> arm(TypeRequest req, const std::string& owner);
  // POST /api/type/cancel by the session `caller`: withdraws the waiting type
  // action (last = cancelled) when the caller armed it, or when `foreign`
  // says its owner is not a session (an access token or a tap tag, shown on
  // the session's screen). Another session's action stays: false, nothing changes.
  // With `requestId` (non-empty) only that very action, else Superseded or
  // NotCancelled; nothing else changes.
  Withdraw cancel(const std::string& caller, const std::function<bool(const std::string& owner)>& foreign,
                  const std::string& requestId = {});
  // Withdraws the waiting item (type action or presence op) when `owner` armed
  // it, and keeps a sequence of theirs that is typing now from arming its next
  // part; false (and nothing changes) otherwise. Called when a session ends
  // without a lock (trusted browser removed, session evicted) and for tokens.
  bool cancelOwned(const std::string& owner);
  // cancelOwned() for one request of `owner` (an access token's cancel with a
  // requestId): only when that request is the waiting item.
  Withdraw cancelOwnedRequest(const std::string& owner, const std::string& requestId);
  // The waiting presence op, when it is `op` and `token` is the cancel token
  // handed to whoever armed it (the screen that asked gave up): dropped
  // without running, result = cancelled. Anyone else gets false — otherwise a
  // stranger could cancel a user's setup and arm their own in its place.
  // With `requestId` (non-empty) the waiting op must also be that request.
  Withdraw cancelPresence(Op op, const std::string& token, const std::string& requestId = {});
  std::optional<Armed> awaitPresence(Op op, Commit commit, const std::string& owner);
  // For ops requested without a session (setup, factory reset, trust): owned
  // by nobody, so they never displace anything and nothing displaces them.
  std::optional<Armed> tryAwaitPresence(Op op, Commit commit) { return awaitPresence(op, std::move(commit), {}); }
  // Lock ends every session, so items armed through a session must not outlive it.
  void dropSessionItems();

  // Whether the armed action's Bluetooth host is connected and ready. Until it
  // is, a short press types nothing (the action stays armed) and expiry
  // reports no_host instead of expired.
  void setLinkReady(bool ready);
  // A short press made while connecting, once the host is ready within
  // kConnectLatchMs of it: Effect::Run as if pressed now, once. Else Effect::None
  // (and a press older than that is forgotten).
  Decision takeLatchedPress();
  // USB host presence (enumerated and not suspended), polled by the runtime.
  // Each connection gets a new number; a USB action armed while one was up is
  // bound to it and ends with host_changed as soon as that connection ends,
  // so it can never type into the next computer (or the same one after a
  // re-plug). Armed with nothing plugged in, it stays unbound (no_usb at the press).
  void setUsbMounted(bool mounted);
  // Effect::Run moves the request (with any free text) out to the caller.
  // `pressedAt`: when the button reported the press (now_()'s clock). A press
  // older than the item in the slot, or than the end of the last commit, does
  // nothing (Effect::None): it was queued while the actions task was busy, and
  // must not approve what somebody armed after it was made.
  Decision onButton(Button b, bool unlocked, int64_t pressedAt);
  // Whether a press was made before the last commit finished: it waited in
  // the queue, and is not a FIDO touch either. (Only the commit counts here:
  // arming a type action must not swallow a touch meant for a website.)
  bool pressedBeforeCommitEnd(int64_t pressedAt);
  // A sequence part typed fine with more to come: the action is armed again
  // for its next part (fresh 60 s) with the same owner, unless something else
  // was armed meanwhile or its owner was withdrawn (cancelOwned, lock).
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
  void recordOpLocked(Op op, OpCode code, int64_t at, bool superseded = false);
  // Whether `owner` may put an item in the slot now (see arm()); a replaced
  // item of the same owner is recorded as cancelled.
  bool takeSlotLocked(const std::string& owner, int64_t now, bool presence);
  // Records how the item with this serial ended, for track().
  void finishedLocked(uint32_t serial, bool presence, Code code, OpCode opCode, bool superseded = false);
  // Hands the armed type action to the caller (Effect::Run) and frees the slot.
  void runLocked(Decision& d);
  void lastLocked(const TypeRequest& req, Code code, int64_t at, bool superseded = false);
  uint32_t nextSerialLocked();
  // A new requestId for the item `serial` of `owner`, remembered for trackRequest().
  std::string issueLocked(uint32_t serial, const std::string& owner);
  struct Issued {
    uint32_t serial = 0;
    std::string requestId, owner;
  };
  // The issued entry for this requestId when `caller` may use it, else nullptr.
  const Issued* findIssuedLocked(const std::string& requestId, const std::string& caller) const;
  Track trackLocked(uint32_t serial, int64_t now);
  // Superseded when the request ended replaced by a newer one, else NotCancelled.
  Withdraw staleLocked(uint32_t serial) const;

  Clock now_;
  std::mutex mu_;
  Kind kind_ = Kind::None;
  TypeRequest req_;
  Op op_ = Op::Setup;
  Commit commit_;
  TokenGen token_;
  std::string cancelToken_;  // of the waiting presence op
  std::string owner_;        // session that armed the waiting item; empty = none
  int64_t deadline_ = 0;
  int64_t freshFrom_ = 0;    // presses made before this are stale (see onButton)
  int64_t committedAt_ = 0;  // when the last commit finished
  bool typing_ = false;
  bool linkReady_ = false;
  int64_t latchedAt_ = 0;  // a press while connecting (0 = none), for takeLatchedPress()
  bool usbMounted_ = false;
  uint32_t usbSession_ = 0;
  std::optional<Op> running_;
  uint32_t serial_ = 0;         // of the item in the slot
  uint32_t lastSerial_ = 0;     // last one handed out
  uint32_t typingSerial_ = 0;   // while typing_
  std::string typingOwner_;     // while typing_: owner_ of the request being typed
  bool typingWithdrawn_ = false;  // while typing_: its owner is gone, arm no next part
  uint32_t runningSerial_ = 0;  // while running_
  std::string requestId_;       // of the item in the slot
  struct Finished {
    uint32_t serial = 0;
    bool presence = false;
    Code code = Code::Failed;
    OpCode opCode = OpCode::Failed;
    bool superseded = false;
  };
  std::array<Finished, 16> finished_{};  // ring of the latest outcomes
  size_t finishedNext_ = 0;
  std::array<Issued, 16> issued_{};      // ring of the latest requestIds
  size_t issuedNext_ = 0;
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

// Whether the account `req` types was edited since it was armed: `current` is
// the vault::Entry::revision read together with the values about to be typed.
inline bool entryChanged(const TypeRequest& req, uint32_t current) { return req.revision != current; }

// A requestId as Keyra hands them out: 32 lowercase hex characters (SPEC §12.6a).
bool validRequestId(const std::string& s);
// The status words of SPEC §12.6a for a tracked request: none, armed,
// waiting, then typed (type) or done (presence), cancelled, expired, failed.
const char* trackState(const Machine::Track& t);
// Finished only: the §5 Result code (type) or the presence result code, or
// "superseded" when a newer request of the same owner replaced it. Else nullptr.
const char* trackCode(const Machine::Track& t);

const char* whatName(What w);
std::optional<What> parseWhat(const std::string& s);
const char* opName(Op op);
std::optional<Op> parseOp(const std::string& s);
const char* codeName(Code c);
const char* opCodeName(OpCode c);

}  // namespace keyra::actions
