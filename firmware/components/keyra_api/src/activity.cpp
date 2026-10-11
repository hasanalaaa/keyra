#include "activity.hpp"

#include <algorithm>
#include <cstddef>

namespace keyra::api::activity {
namespace {

// Record: u8 kind | i64 at | u32 id | u32 n | u8 detail | u8 titleLen | title
// (little-endian), after a one-byte format version.
constexpr uint8_t kFormat = 1;
constexpr size_t kFixed = 1 + 8 + 4 + 4 + 1 + 1;

void put(std::vector<uint8_t>& out, uint64_t v, int bytes) {
  for (int i = 0; i < bytes; ++i) out.push_back(static_cast<uint8_t>(v >> (8 * i)));
}

uint64_t get(const uint8_t* p, int bytes) {
  uint64_t v = 0;
  for (int i = 0; i < bytes; ++i) v |= uint64_t(p[i]) << (8 * i);
  return v;
}

// At most `max` bytes, cut on a UTF-8 boundary.
std::string clip(std::string s, size_t max = kMaxTitle) {
  if (s.size() <= max) return s;
  size_t n = max;
  while (n > 0 && (static_cast<unsigned char>(s[n]) & 0xC0) == 0x80) --n;
  s.resize(n);
  return s;
}

}  // namespace

std::vector<uint8_t> encode(const std::vector<Event>& events) {
  std::vector<uint8_t> out;
  if (events.empty()) return out;
  out.push_back(kFormat);
  for (const Event& e : events) {
    const std::string t = clip(e.title);
    out.push_back(static_cast<uint8_t>(e.kind));
    put(out, static_cast<uint64_t>(e.at), 8);
    put(out, e.id, 4);
    put(out, e.n, 4);
    out.push_back(e.detail);
    out.push_back(static_cast<uint8_t>(t.size()));
    out.insert(out.end(), t.begin(), t.end());
  }
  return out;
}

bool decode(const std::vector<uint8_t>& bytes, std::vector<Event>& out) {
  out.clear();
  if (bytes.empty()) return true;
  if (bytes[0] != kFormat) return false;
  size_t i = 1;
  while (i < bytes.size()) {
    if (bytes.size() - i < kFixed) return false;
    const uint8_t* p = bytes.data() + i;
    Event e;
    e.kind = static_cast<Kind>(p[0]);
    e.at = static_cast<int64_t>(get(p + 1, 8));
    e.id = static_cast<uint32_t>(get(p + 9, 4));
    e.n = static_cast<uint32_t>(get(p + 13, 4));
    e.detail = p[17];
    const size_t len = p[18];
    if (len > kMaxTitle || bytes.size() - i - kFixed < len) return false;
    e.title.assign(reinterpret_cast<const char*>(p + kFixed), len);
    out.push_back(std::move(e));
    i += kFixed + len;
  }
  return out.size() <= kMaxEvents;
}

bool isNoise(Kind k) {
  switch (k) {
    case Kind::RotateStarted: case Kind::RotateEnded:
    case Kind::AgentListed: case Kind::AgentArmed: case Kind::AgentGenerated:
    case Kind::TagTapped: case Kind::TagRefused:
      return true;
    default:
      return false;
  }
}

bool coalesces(Kind k) {
  return k == Kind::RotateStarted || k == Kind::RotateEnded || k == Kind::AgentListed ||
         k == Kind::AgentGenerated || k == Kind::TagRefused;
}

bool dropOldest(std::vector<Event>& events) {
  if (events.empty()) return false;
  auto it = std::find_if(events.begin(), events.end(), [](const Event& e) { return isNoise(e.kind); });
  events.erase(it != events.end() ? it : events.begin());
  return true;
}

void append(std::vector<Event>& events, Event e) {
  e.title = clip(std::move(e.title));
  if (coalesces(e.kind)) {
    e.n = e.n == 0 ? 1 : e.n;
    const size_t from = events.size() > kCoalesceWindow ? events.size() - kCoalesceWindow : 0;
    for (size_t i = events.size(); i-- > from;) {
      const Event& old = events[i];
      if (old.kind == e.kind && old.id == e.id && old.detail == e.detail && old.title == e.title) {
        const uint32_t n = old.n > UINT32_MAX - e.n ? UINT32_MAX : old.n + e.n;
        if (e.at == 0) e.at = old.at;
        e.n = n;
        events.erase(events.begin() + static_cast<std::ptrdiff_t>(i));  // re-added below, newest
        break;
      }
    }
  }
  const bool noise = isNoise(e.kind);
  events.push_back(std::move(e));
  if (noise) {
    size_t count = 0;
    for (const Event& x : events) count += isNoise(x.kind) ? 1 : 0;
    if (count > kMaxNoise) dropOldest(events);  // the oldest noise: there is more than one
  }
  while (events.size() > kMaxEvents) dropOldest(events);
}

std::string elsewhereTitle(std::string_view tokenName, std::string_view pageHost) {
  constexpr std::string_view kArrow = " \xE2\x86\x92 ";  // " → "
  std::string host = clip(std::string(pageHost));
  if (host.size() + kArrow.size() >= kMaxTitle) return host;  // no room for a name: the host matters more
  std::string out = clip(std::string(tokenName), kMaxTitle - kArrow.size() - host.size());
  out += kArrow;
  out += host;
  return out;
}

const char* kindName(Kind k) {
  switch (k) {
    case Kind::Unlock: return "unlock";
    case Kind::FailedUnlocks: return "failed_unlocks";
    case Kind::Lock: return "lock";
    case Kind::Typed: return "typed";
    case Kind::Revealed: return "revealed";
    case Kind::Backup: return "backup";
    case Kind::Restore: return "restore";
    case Kind::Passphrase: return "passphrase";
    case Kind::RecoveryCreated: return "recovery_created";
    case Kind::RecoveryRemoved: return "recovery_removed";
    case Kind::BleForgot: return "ble_forgot";
    case Kind::TrustedRemoved: return "trusted_removed";
    case Kind::EntryDeleted: return "entry_deleted";
    case Kind::TextTyped: return "text_typed";
    case Kind::BlePairing: return "ble_pairing";
    case Kind::RotateStarted: return "rotate_started";
    case Kind::RotateEnded: return "rotate_ended";
    case Kind::EntryBurned: return "entry_burned";
    case Kind::TokenCreated: return "token_created";
    case Kind::TokenRevoked: return "token_revoked";
    case Kind::AgentListed: return "agent_listed";
    case Kind::AgentArmed: return "agent_armed";
    case Kind::AgentSaved: return "agent_saved";
    case Kind::AgentGenerated: return "agent_generated";
    case Kind::TagCreated: return "tag_created";
    case Kind::TagRevoked: return "tag_revoked";
    case Kind::TagTapped: return "tag_tapped";
    case Kind::TagRefused: return "tag_refused";
  }
  return "unknown";
}

}  // namespace keyra::api::activity
