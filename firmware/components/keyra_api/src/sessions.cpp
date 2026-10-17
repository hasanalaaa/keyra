#include "sessions.hpp"

namespace keyra::api {

bool constantTimeEqual(std::string_view a, std::string_view b) {
  if (a.size() != b.size()) return false;
  volatile uint8_t diff = 0;
  for (size_t i = 0; i < a.size(); ++i) diff |= static_cast<uint8_t>(a[i] ^ b[i]);
  return diff == 0;
}

std::string Sessions::randomHex() {
  uint8_t raw[kTokenBytes];
  rng_(raw, sizeof raw);
  static constexpr char kHex[] = "0123456789abcdef";
  std::string out(kTokenBytes * 2, '0');
  for (size_t i = 0; i < kTokenBytes; ++i) {
    out[2 * i] = kHex[raw[i] >> 4];
    out[2 * i + 1] = kHex[raw[i] & 0x0F];
    raw[i] = 0;
  }
  return out;
}

Sessions::Issued Sessions::create(int64_t nowMs, uint32_t trustId, uint32_t generation) {
  std::lock_guard<std::mutex> lock(mu_);
  Slot* target = &slots_[0];
  for (Slot& s : slots_) {
    if (!s.used) {
      target = &s;
      break;
    }
    if (s.lastUsed < target->lastUsed) target = &s;
  }
  std::string evicted = target->used ? target->token : std::string();
  target->used = true;
  target->token = randomHex();
  target->csrf = randomHex();
  target->lastUsed = nowMs;
  target->trustId = trustId;
  target->generation = generation;
  target->approval.reset();
  target->approvalUntil = 0;
  target->revealed = false;
  lastActivity_ = nowMs;
  return {target->token, target->csrf, std::move(evicted)};
}

Sessions::Slot* Sessions::findLocked(std::string_view token) {
  if (token.size() != kTokenBytes * 2) return nullptr;
  Slot* found = nullptr;
  // Compare against every slot so timing does not reveal which slot matched.
  for (Slot& s : slots_) {
    if (s.used && constantTimeEqual(s.token, token)) found = &s;
  }
  return found;
}

bool Sessions::approve(std::string_view token, int64_t nowMs, Approval a) {
  std::lock_guard<std::mutex> lock(mu_);
  Slot* s = findLocked(token);
  if (!s) return false;
  s->approval = std::move(a);
  s->approvalUntil = nowMs + kApprovalMs;
  return true;
}

Sessions::Redeem Sessions::redeem(std::string_view token, int64_t nowMs, const Approval& want) {
  std::lock_guard<std::mutex> lock(mu_);
  Slot* s = findLocked(token);
  if (!s || !s->approval) return Redeem::None;
  if (nowMs >= s->approvalUntil) {
    s->approval.reset();
    return Redeem::None;
  }
  const Approval& a = *s->approval;
  if (a.purpose != want.purpose || a.resource != want.resource || !constantTimeEqual(a.request, want.request))
    return Redeem::None;
  const bool same = a.revision == want.revision;
  s->approval.reset();
  return same ? Redeem::Ok : Redeem::Changed;
}

void Sessions::noteRevealed(std::string_view token, uint32_t entry, uint32_t revision) {
  std::lock_guard<std::mutex> lock(mu_);
  if (Slot* s = findLocked(token)) {
    s->revealed = true;
    s->revealedEntry = entry;
    s->revealedRevision = revision;
  }
}

bool Sessions::knows(std::string_view token, uint32_t entry, uint32_t revision) {
  std::lock_guard<std::mutex> lock(mu_);
  const Slot* s = findLocked(token);
  return s && s->revealed && s->revealedEntry == entry && s->revealedRevision == revision;
}

std::optional<std::string> Sessions::csrfFor(std::string_view token, int64_t nowMs, uint32_t generation) {
  std::lock_guard<std::mutex> lock(mu_);
  Slot* found = findLocked(token);
  if (!found || found->generation != generation) return std::nullopt;
  found->lastUsed = nowMs;
  return found->csrf;
}

void Sessions::clear() {
  std::lock_guard<std::mutex> lock(mu_);
  for (Slot& s : slots_) s = Slot{};
}

std::vector<std::string> Sessions::endTrusted(uint32_t trustId) {
  std::lock_guard<std::mutex> lock(mu_);
  std::vector<std::string> ended;
  for (Slot& s : slots_) {
    if (trustId != 0 && s.used && s.trustId == trustId) {
      ended.push_back(s.token);
      s = Slot{};
    }
  }
  return ended;
}

bool Sessions::stillValid(std::string_view token, uint32_t generation, uint32_t currentGeneration, bool unlocked) {
  std::lock_guard<std::mutex> lock(mu_);
  const Slot* s = findLocked(token);
  return unlocked && generation == currentGeneration && s && s->generation == generation;
}

size_t Sessions::size() {
  std::lock_guard<std::mutex> lock(mu_);
  size_t n = 0;
  for (const Slot& s : slots_) n += s.used ? 1 : 0;
  return n;
}

void Sessions::activity(int64_t nowMs) {
  std::lock_guard<std::mutex> lock(mu_);
  lastActivity_ = nowMs;
}

bool Sessions::idleFor(int64_t nowMs, int64_t limitMs) {
  std::lock_guard<std::mutex> lock(mu_);
  return nowMs - lastActivity_ >= limitMs;
}

}  // namespace keyra::api
