#include "actions.hpp"

namespace keyra::actions {

Pending Machine::arm(TypeRequest req) {
  std::lock_guard<std::mutex> lock(mu_);
  const int64_t now = now_();
  clearSlotLocked();
  kind_ = Kind::Type;
  req_ = std::move(req);
  deadline_ = now + kExpiryMs;
  return {req_, kExpiryMs};
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
  const bool sessionOp = kind_ == Kind::Presence && (op_ == Op::Wifi || op_ == Op::RestoreReplace);
  if (kind_ == Kind::Type) {
    hasLast_ = true;
    last_ = {false, Code::Cancelled, 0, req_.title, req_.what};
    lastAt_ = now_();
  }
  if (kind_ == Kind::Type || sessionOp) clearSlotLocked();
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
    } else if (kind_ == Kind::Type && !typing_) {
      d.effect = Effect::Run;
      d.run = req_;
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
  hasLast_ = true;
  last_ = {code == Code::Typed, code, 0, req.title, req.what};
  lastAt_ = now;
  flashLocked(code == Code::Typed ? Indicator::Success : Indicator::Error, now, kFlashMs);
}

void Machine::commitFinished(bool ok) {
  std::lock_guard<std::mutex> lock(mu_);
  running_.reset();
  flashLocked(ok ? Indicator::Success : Indicator::Error, now_(), kFlashMs);
}

std::optional<Pending> Machine::pending() {
  std::lock_guard<std::mutex> lock(mu_);
  const int64_t now = now_();
  expireLocked(now);
  if (kind_ != Kind::Type) return std::nullopt;
  return Pending{req_, deadline_ - now};
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

Indicator Machine::indicator(bool initialized, bool unlocked) {
  std::lock_guard<std::mutex> lock(mu_);
  const int64_t now = now_();
  expireLocked(now);
  if (kind_ == Kind::Presence) return Indicator::AwaitPresence;
  if (typing_ || running_) return Indicator::Typing;
  if (kind_ == Kind::Type) return Indicator::Pending;
  if (now < flashUntil_) return flash_;
  if (!initialized) return Indicator::Setup;
  return unlocked ? Indicator::Idle : Indicator::Locked;
}

void Machine::expireLocked(int64_t now) {
  if (kind_ == Kind::None || now < deadline_) return;
  if (kind_ == Kind::Type) {
    hasLast_ = true;
    last_ = {false, Code::Expired, 0, req_.title, req_.what};
    lastAt_ = deadline_;
  }
  clearSlotLocked();
}

void Machine::clearSlotLocked() {
  kind_ = Kind::None;
  req_ = {};
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
  }
  return "username";
}

std::optional<What> parseWhat(const std::string& s) {
  if (s == "username") return What::Username;
  if (s == "password") return What::Password;
  if (s == "both") return What::Both;
  if (s == "totp") return What::Totp;
  return std::nullopt;  // "test" is requested via {test:true}, never via `what`
}

const char* opName(Op op) {
  switch (op) {
    case Op::Setup: return "setup";
    case Op::Wifi: return "wifi";
    case Op::RestoreReplace: return "restore";
    case Op::FactoryReset: return "factory_reset";
  }
  return "setup";
}

const char* codeName(Code c) {
  switch (c) {
    case Code::Typed: return "typed";
    case Code::Cancelled: return "cancelled";
    case Code::Expired: return "expired";
    case Code::NoUsb: return "no_usb";
    case Code::UnsupportedChar: return "unsupported_char";
    case Code::Failed: return "failed";
  }
  return "failed";
}

}  // namespace keyra::actions
