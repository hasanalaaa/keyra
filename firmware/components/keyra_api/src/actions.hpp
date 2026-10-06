#pragma once
// Pending-action state machine (SPEC §5 "Button semantics"). Plain C++ with an
// injected clock so it runs in host tests; the ESP glue lives in action_runner.
//
// One slot holds at most one item: a type action or a presence-gated op.
// Arming either replaces whatever was in the slot. Items expire after 60 s.
#include <cstdint>
#include <functional>
#include <mutex>
#include <optional>
#include <string>

namespace keyra::actions {

constexpr int64_t kExpiryMs = 60000;
constexpr int64_t kFlashMs = 1500;  // Success/Error LED after a finished action
constexpr int64_t kBlinkMs = 150;   // "nothing to do" acknowledgement

enum class What { Username, Password, Both, Totp, Test };
// HomeWifi: join/change/leave the home network (session). TrustBrowser: approve a
// browser that unlocks through the home network (no session yet, SPEC §8.2).
// BlePair: open the Bluetooth pairing window (session, SPEC §8.1).
enum class Op { Setup, Wifi, RestoreReplace, FactoryReset, HomeWifi, TrustBrowser, BlePair };
// NoUsb: output is USB-only and no computer is plugged in. NoHost: nothing
// connected on the selected output (auto or Bluetooth).
enum class Code { Typed, Cancelled, Expired, NoUsb, NoHost, UnsupportedChar, Failed };
enum class Button { Short, Long };
enum class Indicator { Setup, Locked, Idle, Pending, Typing, AwaitPresence, Success, Error, Off, Pairing };

struct TypeRequest {
  uint32_t id = 0;  // 0 for the test string
  std::string title;
  What what = What::Username;
  bool submit = false;
};

struct Pending {
  TypeRequest req;
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

  Decision onButton(Button b, bool unlocked);
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
  std::optional<Op> running_;
  bool hasLast_ = false;
  Result last_;
  int64_t lastAt_ = 0;
  std::optional<OpResult> opResult_;
  int64_t opResultAt_ = 0;
  Indicator flash_ = Indicator::Off;
  int64_t flashUntil_ = 0;
};

const char* whatName(What w);
std::optional<What> parseWhat(const std::string& s);
const char* opName(Op op);
const char* codeName(Code c);
const char* opCodeName(OpCode c);

}  // namespace keyra::actions
