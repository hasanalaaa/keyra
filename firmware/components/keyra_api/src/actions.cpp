#include "actions.hpp"

#include "keyra/vault.hpp"

namespace keyra::actions {
namespace {

// Status views of the slot never hold the free text, so it stays in one place.
Pending view(const TypeRequest& req, int64_t expiresInMs) {
  Pending p{req, expiresInMs};
  p.req.text.reset();
  return p;
}

}  // namespace

FreeText::~FreeText() { vault::wipe(text); }

Pending Machine::arm(TypeRequest req) {
  std::lock_guard<std::mutex> lock(mu_);
  const int64_t now = now_();
  clearSlotLocked();
  kind_ = Kind::Type;
  req_ = std::move(req);
  req_.usbSession = req_.target.kind == Target::Kind::Usb && usbMounted_ ? usbSession_ : 0;
  deadline_ = now + kExpiryMs;
  return view(req_, kExpiryMs);
}

bool Machine::cancel() {
  std::lock_guard<std::mutex> lock(mu_);
  const int64_t now = now_();
  expireLocked(now);
  if (kind_ != Kind::Type) return false;
  hasLast_ = true;
  last_ = {false, Code::Cancelled, 0, req_.title, req_.what};
  lastAt_ = now;
  clearSlotLocked();
  return true;
}

bool Machine::cancelPresence(Op op) {
  std::lock_guard<std::mutex> lock(mu_);
  const int64_t now = now_();
  expireLocked(now);
  if (kind_ != Kind::Presence || op_ != op) return false;
  recordOpLocked(op, OpCode::Cancelled, now);
  clearSlotLocked();  // drops the commit: nothing runs on a later press
  return true;
}

int64_t Machine::awaitPresence(Op op, Commit commit) {
  std::lock_guard<std::mutex> lock(mu_);
  clearSlotLocked();
  kind_ = Kind::Presence;
  op_ = op;
  commit_ = std::move(commit);
  deadline_ = now_() + kExpiryMs;
  return kExpiryMs;
}

std::optional<int64_t> Machine::tryAwaitPresence(Op op, Commit commit) {
  std::lock_guard<std::mutex> lock(mu_);
  const int64_t now = now_();
  expireLocked(now);
  if (kind_ != Kind::None || running_) return std::nullopt;
  kind_ = Kind::Presence;
  op_ = op;
  commit_ = std::move(commit);
  deadline_ = now + kExpiryMs;
  return kExpiryMs;
}

void Machine::dropSessionItems() {
  std::lock_guard<std::mutex> lock(mu_);
  const bool sessionOp = kind_ == Kind::Presence && op_ != Op::Setup && op_ != Op::FactoryReset &&
                         op_ != Op::TrustBrowser;
  if (kind_ == Kind::Type) {
    hasLast_ = true;
    last_ = {false, Code::Cancelled, 0, req_.title, req_.what};
    lastAt_ = now_();
  }
  if (sessionOp) recordOpLocked(op_, OpCode::Cancelled, now_());
  if (kind_ == Kind::Type || sessionOp) clearSlotLocked();
}

void Machine::setLinkReady(bool ready) {
  std::lock_guard<std::mutex> lock(mu_);
  linkReady_ = ready;
}

void Machine::setUsbMounted(bool mounted) {
  std::lock_guard<std::mutex> lock(mu_);
  if (mounted == usbMounted_) return;
  usbMounted_ = mounted;
  if (mounted) {
    if (++usbSession_ == 0) usbSession_ = 1;  // 0 means "unbound"
    return;
  }
  if (kind_ == Kind::Type && req_.usbSession != 0) {
    const int64_t now = now_();
    hasLast_ = true;
    last_ = {false, Code::HostChanged, 0, req_.title, req_.what};
    lastAt_ = now;
    clearSlotLocked();
    flashLocked(Indicator::Error, now, kFlashMs);
  }
}

bool HostWatch::pollUsb(int64_t now, bool unlocked, bool usbMounted, bool enabled) {
  if (!unlocked) {
    usbSeen_ = false;
    usbGoneAt_ = -1;
    bleUsed_.reset();
    return false;
  }
  if (usbMounted) {
    usbSeen_ = true;
    usbGoneAt_ = -1;
    return false;
  }
  if (!usbSeen_ || !enabled) return false;
  if (usbGoneAt_ < 0) usbGoneAt_ = now;
  if (now - usbGoneAt_ < kUsbGoneMs) return false;
  usbSeen_ = false;
  usbGoneAt_ = -1;
  return true;
}

bool HostWatch::bleLost(const BtAddr& addr, bool unlocked, bool enabled) {
  const bool hit = unlocked && enabled && bleUsed_ && *bleUsed_ == addr;
  if (hit) bleUsed_.reset();
  return hit;
}

Decision Machine::onButton(Button b, bool unlocked) {
  std::lock_guard<std::mutex> lock(mu_);
  const int64_t now = now_();
  expireLocked(now);
  Decision d;
  if (b == Button::Short) {
    if (kind_ == Kind::Presence) {
      d.effect = Effect::Approve;
      d.op = op_;
      d.commit = std::move(commit_);
      running_ = op_;
      clearSlotLocked();
    } else if (kind_ == Kind::Type && !typing_ && req_.target.kind == Target::Kind::Ble && !linkReady_) {
      // Still connecting: typing now could only fail. Keep the action armed.
      d.effect = Effect::Blink;
      flashLocked(Indicator::Off, now, kBlinkMs);
    } else if (kind_ == Kind::Type && !typing_) {
      d.effect = Effect::Run;
      d.run = std::move(req_);
      typing_ = true;
      clearSlotLocked();
    } else if (!typing_) {
      d.effect = Effect::Blink;
      flashLocked(Indicator::Off, now, kBlinkMs);
    }
    return d;
  }
  if (kind_ == Kind::Type) {
    hasLast_ = true;
    last_ = {false, Code::Cancelled, 0, req_.title, req_.what};
    lastAt_ = now;
  }
  if (kind_ == Kind::Presence) recordOpLocked(op_, OpCode::Cancelled, now);
  if (kind_ != Kind::None) {
    d.effect = Effect::Cancelled;
    clearSlotLocked();
  } else if (typing_ || running_) {
    d.effect = Effect::None;  // typing/committing cannot be interrupted midway
  } else if (unlocked) {
    d.effect = Effect::Lock;
  } else {
    d.effect = Effect::Blink;
    flashLocked(Indicator::Off, now, kBlinkMs);
  }
  return d;
}

void Machine::typingFinished(const TypeRequest& req, Code code) {
  std::lock_guard<std::mutex> lock(mu_);
  const int64_t now = now_();
  typing_ = false;
  if (code == Code::Typed && req.what == What::Sequence && req.seq && req.part + 1 < req.seq->parts) {
    if (kind_ == Kind::None) {  // waits for the {PRESS}: same request, next part
      kind_ = Kind::Type;
      req_ = req;
      ++req_.part;
      deadline_ = now + kExpiryMs;
      return;
    }
    code = Code::Cancelled;  // another item replaced it while this part was typing
  }
  hasLast_ = true;
  last_ = {code == Code::Typed, code, 0, req.title, req.what};
  lastAt_ = now;
  flashLocked(code == Code::Typed ? Indicator::Success : Indicator::Error, now, kFlashMs);
}

void Machine::commitFinished(bool ok) {
  std::lock_guard<std::mutex> lock(mu_);
  const int64_t now = now_();
  if (running_) recordOpLocked(*running_, ok ? OpCode::Done : OpCode::Failed, now);
  running_.reset();
  flashLocked(ok ? Indicator::Success : Indicator::Error, now, kFlashMs);
}

std::optional<OpResult> Machine::opResult() {
  std::lock_guard<std::mutex> lock(mu_);
  const int64_t now = now_();
  expireLocked(now);
  if (!opResult_) return std::nullopt;
  OpResult r = *opResult_;
  r.agoMs = now - opResultAt_;
  return r;
}

void Machine::recordOpLocked(Op op, OpCode code, int64_t at) {
  opResult_ = OpResult{op, code, 0};
  opResultAt_ = at;
}

std::optional<Pending> Machine::pending() {
  std::lock_guard<std::mutex> lock(mu_);
  const int64_t now = now_();
  expireLocked(now);
  if (kind_ != Kind::Type) return std::nullopt;
  return view(req_, deadline_ - now);
}

std::optional<Result> Machine::last() {
  std::lock_guard<std::mutex> lock(mu_);
  const int64_t now = now_();
  expireLocked(now);
  if (!hasLast_) return std::nullopt;
  Result r = last_;
  r.agoMs = now - lastAt_;
  return r;
}

std::optional<Presence> Machine::presence() {
  std::lock_guard<std::mutex> lock(mu_);
  const int64_t now = now_();
  expireLocked(now);
  if (kind_ == Kind::Presence) return Presence{op_, true, deadline_ - now};
  if (running_) return Presence{*running_, false, 0};
  return std::nullopt;
}

Indicator Machine::indicator(bool initialized, bool unlocked, bool blePairing) {
  std::lock_guard<std::mutex> lock(mu_);
  const int64_t now = now_();
  expireLocked(now);
  if (kind_ == Kind::Presence) return Indicator::AwaitPresence;
  if (typing_ || running_) return Indicator::Typing;
  if (kind_ == Kind::Type) return Indicator::Pending;
  if (now < flashUntil_) return flash_;
  if (blePairing) return Indicator::Pairing;
  if (!initialized) return Indicator::Setup;
  return unlocked ? Indicator::Idle : Indicator::Locked;
}

void Machine::expireLocked(int64_t now) {
  if (kind_ == Kind::None || now < deadline_) return;
  if (kind_ == Kind::Type) {
    // A Bluetooth host that never showed up is the more useful explanation.
    const bool noHost = req_.target.kind == Target::Kind::Ble && !linkReady_;
    hasLast_ = true;
    last_ = {false, noHost ? Code::NoHost : Code::Expired, 0, req_.title, req_.what};
    lastAt_ = deadline_;
  } else {
    recordOpLocked(op_, OpCode::Expired, deadline_);
  }
  clearSlotLocked();
}

void Machine::clearSlotLocked() {
  kind_ = Kind::None;
  req_ = {};          // drops the slot's hold on any free text (wiped with the last one)
  commit_ = nullptr;  // destroys captured secrets of a dropped presence op
  deadline_ = 0;
}

void Machine::flashLocked(Indicator ind, int64_t now, int64_t ms) {
  flash_ = ind;
  flashUntil_ = now + ms;
}

const char* whatName(What w) {
  switch (w) {
    case What::Username: return "username";
    case What::Password: return "password";
    case What::Both: return "both";
    case What::Totp: return "totp";
    case What::Test: return "test";
    case What::Text: return "text";
    case What::Sequence: return "sequence";
    case What::Probe: return "probe";
  }
  return "username";
}

std::optional<What> parseWhat(const std::string& s) {
  if (s == "username") return What::Username;
  if (s == "password") return What::Password;
  if (s == "both") return What::Both;
  if (s == "totp") return What::Totp;
  if (s == "sequence") return What::Sequence;
  return std::nullopt;  // "test", "text", "probe" are requested via {test|probe:true} / {text}, never via `what`
}

const char* opName(Op op) {
  switch (op) {
    case Op::Setup: return "setup";
    case Op::Wifi: return "wifi";
    case Op::RestoreReplace: return "restore";
    case Op::FactoryReset: return "factory_reset";
    case Op::HomeWifi: return "home_wifi";
    case Op::TrustBrowser: return "trust_browser";
    case Op::BlePair: return "ble_pair";
    case Op::Reveal: return "reveal";
    case Op::Backup: return "backup";
    case Op::Recovery: return "recovery";
    case Op::Unprotect: return "unprotect";
  }
  return "setup";
}

std::optional<Op> parseOp(const std::string& s) {
  for (Op op : {Op::Setup, Op::Wifi, Op::RestoreReplace, Op::FactoryReset, Op::HomeWifi, Op::TrustBrowser,
                Op::BlePair, Op::Reveal, Op::Backup, Op::Recovery, Op::Unprotect}) {
    if (s == opName(op)) return op;
  }
  return std::nullopt;
}

const char* opCodeName(OpCode c) {
  switch (c) {
    case OpCode::Done: return "done";
    case OpCode::Failed: return "failed";
    case OpCode::Expired: return "expired";
    case OpCode::Cancelled: return "cancelled";
  }
  return "failed";
}

const char* codeName(Code c) {
  switch (c) {
    case Code::Typed: return "typed";
    case Code::Cancelled: return "cancelled";
    case Code::Expired: return "expired";
    case Code::NoUsb: return "no_usb";
    case Code::NoHost: return "no_host";
    case Code::UnsupportedChar: return "unsupported_char";
    case Code::Failed: return "failed";
    case Code::HostChanged: return "host_changed";
  }
  return "failed";
}

}  // namespace keyra::actions
