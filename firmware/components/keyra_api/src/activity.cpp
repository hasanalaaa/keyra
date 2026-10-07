#include "activity.hpp"

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

std::string clip(std::string s) {
  if (s.size() <= kMaxTitle) return s;
  size_t n = kMaxTitle;
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

void append(std::vector<Event>& events, Event e) {
  e.title = clip(std::move(e.title));
  events.push_back(std::move(e));
  if (events.size() > kMaxEvents) events.erase(events.begin(), events.end() - kMaxEvents);
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
  }
  return "unknown";
}

}  // namespace keyra::api::activity
