#include "entry_codec.hpp"

#include <cstring>

#include "text.hpp"

namespace keyra::vault::codec {
namespace {

constexpr uint8_t kFormat = 1;
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

}  // namespace

bool valid(const Entry& e) {
  for (const auto& f : kFields) {
    const std::string& s = e.*f.member;
    if (s.size() > f.max || !text::validUtf8(s)) return false;
  }
  return true;
}

size_t encodedSize(const Entry& e) {
  size_t n = kFixed;
  for (const auto& f : kFields) n += 2 + (e.*f.member).size();
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
  for (const auto& f : kFields) {
    const std::string& s = e.*f.member;
    putLe(p, s.size(), 2);
    if (!s.empty()) std::memcpy(p, s.data(), s.size());
    p += s.size();
  }
  return true;
}

bool decode(const uint8_t* p, size_t n, Entry& out) {
  const uint8_t* end = p + n;
  if (n < kFixed || *p++ != kFormat) return false;
  out.id = uint32_t(getLe(p, 4));
  uint8_t flags = *p++;
  if (flags & ~1u) return false;
  out.favorite = flags & 1;
  out.created = int64_t(getLe(p, 8));
  out.updated = int64_t(getLe(p, 8));
  out.lastUsed = int64_t(getLe(p, 8));
  for (const auto& f : kFields) {
    if (end - p < 2) return false;
    size_t len = size_t(getLe(p, 2));
    if (len > f.max || size_t(end - p) < len) return false;
    (out.*f.member).assign(reinterpret_cast<const char*>(p), len);
    p += len;
  }
  return p == end;
}

}  // namespace keyra::vault::codec
