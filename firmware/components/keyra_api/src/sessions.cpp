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

Sessions::Issued Sessions::create(int64_t nowMs) {
  std::lock_guard<std::mutex> lock(mu_);
  Slot* target = &slots_[0];
  for (Slot& s : slots_) {
    if (!s.used) {
      target = &s;
      break;
    }
    if (s.lastUsed < target->lastUsed) target = &s;
  }
  target->used = true;
  target->token = randomHex();
  target->csrf = randomHex();
  target->lastUsed = nowMs;
  lastActivity_ = nowMs;
  return {target->token, target->csrf};
}

std::optional<std::string> Sessions::csrfFor(std::string_view token, int64_t nowMs) {
  std::lock_guard<std::mutex> lock(mu_);
  if (token.size() != kTokenBytes * 2) return std::nullopt;
  Slot* found = nullptr;
  // Compare against every slot so timing does not reveal which slot matched.
  for (Slot& s : slots_) {
    if (s.used && constantTimeEqual(s.token, token)) found = &s;
  }
  if (!found) return std::nullopt;
  found->lastUsed = nowMs;
  return found->csrf;
}

void Sessions::clear() {
  std::lock_guard<std::mutex> lock(mu_);
  for (Slot& s : slots_) s = Slot{};
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
