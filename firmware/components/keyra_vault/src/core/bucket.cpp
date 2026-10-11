#include "bucket.hpp"

#include <algorithm>
#include <cstdio>
#include <cstring>

namespace keyra::vault::bucket {
namespace {

constexpr size_t kHeader = 1 + 4, kRecordHeader = 4 + 4;

uint32_t getLe32(const uint8_t* p) {
  return uint32_t(p[0]) | uint32_t(p[1]) << 8 | uint32_t(p[2]) << 16 | uint32_t(p[3]) << 24;
}
void putLe32(std::vector<uint8_t>& out, uint32_t v) {
  for (int i = 0; i < 4; ++i) out.push_back(uint8_t(v >> (8 * i)));
}
std::string name(uint32_t k, const char* ext) {
  char b[16];
  std::snprintf(b, sizeof b, "%s/%02x%s", kDir, static_cast<unsigned>(k), ext);
  return b;
}

}  // namespace

std::string path(uint32_t k) { return name(k, ".bin"); }
std::string stagedPath(uint32_t k) { return name(k, ".new"); }

bool parseName(const std::string& n, uint32_t& k, const char* ext) {
  if (n.size() != 2 + std::strlen(ext) || n.compare(2, std::string::npos, ext) != 0) return false;
  k = 0;
  for (int i = 0; i < 2; ++i) {
    const char c = n[i];
    const int v = c >= '0' && c <= '9' ? c - '0' : c >= 'a' && c <= 'f' ? c - 'a' + 10 : -1;
    if (v < 0) return false;
    k = k << 4 | uint32_t(v);
  }
  return k < kCount;
}

bool parse(const std::vector<uint8_t>& file, uint32_t k, Records& out) {
  out.clear();
  if (file.size() < kHeader || file[0] != kVersion) return false;
  const uint32_t count = getLe32(&file[1]);
  // Every record takes at least its header and one byte: bounds `count` before reserving.
  if (count > (file.size() - kHeader) / (kRecordHeader + 1)) return false;
  out.reserve(count);
  size_t at = kHeader;
  uint32_t last = 0;
  for (uint32_t i = 0; i < count; ++i) {
    if (file.size() - at < kRecordHeader) return false;
    const uint32_t id = getLe32(&file[at]), len = getLe32(&file[at + 4]);
    at += kRecordHeader;
    if (id == 0 || of(id) != k || (i > 0 && id <= last) || len == 0 || len > file.size() - at) return false;
    out.push_back(Record{id, std::vector<uint8_t>(file.begin() + long(at), file.begin() + long(at + len))});
    at += len;
    last = id;
  }
  return at == file.size();
}

std::vector<uint8_t> encode(Records& records) {
  std::sort(records.begin(), records.end(), [](const Record& a, const Record& b) { return a.id < b.id; });
  size_t total = kHeader;
  for (const Record& r : records) total += kRecordHeader + r.sealed.size();
  std::vector<uint8_t> out;
  out.reserve(total);
  out.push_back(kVersion);
  putLe32(out, uint32_t(records.size()));
  for (const Record& r : records) {
    putLe32(out, r.id);
    putLe32(out, uint32_t(r.sealed.size()));
    out.insert(out.end(), r.sealed.begin(), r.sealed.end());
  }
  return out;
}

}  // namespace keyra::vault::bucket
