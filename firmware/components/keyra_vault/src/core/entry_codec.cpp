#include "entry_codec.hpp"

#include <cstring>

#include "text.hpp"

namespace keyra::vault::codec {
namespace {

constexpr uint8_t kFormatV1 = 1, kFormat = 2;
constexpr size_t kFixed = 1 + 4 + 1 + 3 * 8;

struct Field {
  std::string Entry::* member;
  size_t max;
};
constexpr Field kFields[] = {
    {&Entry::title, kMaxTitle},       {&Entry::url, kMaxUrl},   {&Entry::username, kMaxUsername},
    {&Entry::password, kMaxPassword}, {&Entry::totp, kMaxTotp}, {&Entry::notes, kMaxNotes},
};

void putLe(uint8_t*& p, uint64_t v, int bytes) {
  for (int i = 0; i < bytes; ++i) *p++ = uint8_t(v >> (8 * i));
}

uint64_t getLe(const uint8_t*& p, int bytes) {
  uint64_t v = 0;
  for (int i = 0; i < bytes; ++i) v |= uint64_t(*p++) << (8 * i);
  return v;
}

void putString(uint8_t*& p, const std::string& s) {
  putLe(p, s.size(), 2);
  if (!s.empty()) std::memcpy(p, s.data(), s.size());
  p += s.size();
}

bool getString(const uint8_t*& p, const uint8_t* end, size_t max, std::string& out) {
  if (end - p < 2) return false;
  size_t len = size_t(getLe(p, 2));
  if (len > max || size_t(end - p) < len) return false;
  out.assign(reinterpret_cast<const char*>(p), len);
  p += len;
  return true;
}

}  // namespace

bool valid(const Entry& e) {
  for (const auto& f : kFields) {
    const std::string& s = e.*f.member;
    if (s.size() > f.max || !text::validUtf8(s)) return false;
  }
  if (e.history.size() > kMaxHistory) return false;
  for (const OldPassword& h : e.history)
    if (h.password.size() > kMaxPassword || !text::validUtf8(h.password)) return false;
  return true;
}

size_t encodedSize(const Entry& e) {
  size_t n = kFixed + 1;
  for (const auto& f : kFields) n += 2 + (e.*f.member).size();
  for (const OldPassword& h : e.history) n += 8 + 2 + h.password.size();
  return n;
}

bool encode(const Entry& e, SecureBuf& out) {
  if (!out.alloc(encodedSize(e))) return false;
  uint8_t* p = out.data();
  *p++ = kFormat;
  putLe(p, e.id, 4);
  *p++ = e.favorite ? 1 : 0;
  putLe(p, uint64_t(e.created), 8);
  putLe(p, uint64_t(e.updated), 8);
  putLe(p, uint64_t(e.lastUsed), 8);
  for (const auto& f : kFields) putString(p, e.*f.member);
  *p++ = uint8_t(e.history.size());
  for (const OldPassword& h : e.history) {
    putLe(p, uint64_t(h.changedAt), 8);
    putString(p, h.password);
  }
  return true;
}

bool decode(const uint8_t* p, size_t n, Entry& out) {
  const uint8_t* end = p + n;
  if (n < kFixed) return false;
  const uint8_t format = *p++;
  if (format != kFormatV1 && format != kFormat) return false;
  out.id = uint32_t(getLe(p, 4));
  uint8_t flags = *p++;
  if (flags & ~1u) return false;
  out.favorite = flags & 1;
  out.created = int64_t(getLe(p, 8));
  out.updated = int64_t(getLe(p, 8));
  out.lastUsed = int64_t(getLe(p, 8));
  for (const auto& f : kFields)
    if (!getString(p, end, f.max, out.*f.member)) return false;

  for (OldPassword& h : out.history) wipe(h.password);
  out.history.clear();
  if (format == kFormatV1) return p == end;
  if (p == end) return false;
  const size_t count = *p++;
  if (count > kMaxHistory) return false;
  // Reserved up front: regrowth would move short (inline) passwords and free
  // their old copies unwiped.
  out.history.reserve(count);
  for (size_t i = 0; i < count; ++i) {
    if (end - p < 8) return false;
    out.history.emplace_back();
    out.history.back().changedAt = int64_t(getLe(p, 8));
    if (!getString(p, end, kMaxPassword, out.history.back().password)) return false;
  }
  return p == end;
}

}  // namespace keyra::vault::codec
