#include "json.hpp"

#include <cstring>

#include "text.hpp"

namespace keyra::vault::json {
namespace {

constexpr int kMaxDepth = 8;

struct Parser {
  const char* p;
  const char* end;

  void ws() {
    while (p < end && (*p == ' ' || *p == '\t' || *p == '\n' || *p == '\r')) ++p;
  }
  bool lit(const char* word) {
    size_t n = std::strlen(word);
    if (size_t(end - p) < n || std::memcmp(p, word, n) != 0) return false;
    p += n;
    return true;
  }
  bool hex4(uint32_t& v) {
    if (end - p < 4) return false;
    v = 0;
    for (int k = 0; k < 4; ++k) {
      char c = *p++;
      v <<= 4;
      if (c >= '0' && c <= '9') v |= uint32_t(c - '0');
      else if (c >= 'a' && c <= 'f') v |= uint32_t(c - 'a' + 10);
      else if (c >= 'A' && c <= 'F') v |= uint32_t(c - 'A' + 10);
      else return false;
    }
    return true;
  }
  bool string(SecureString& out) {
    out.clear();
    if (p >= end || *p++ != '"') return false;
    while (p < end) {
      unsigned char c = static_cast<unsigned char>(*p++);
      if (c == '"') return true;
      if (c < 0x20) return false;
      if (c != '\\') {
        out += char(c);
        continue;
      }
      if (p >= end) return false;
      char e = *p++;
      switch (e) {
        case '"': out += '"'; break;
        case '\\': out += '\\'; break;
        case '/': out += '/'; break;
        case 'b': out += '\b'; break;
        case 'f': out += '\f'; break;
        case 'n': out += '\n'; break;
        case 'r': out += '\r'; break;
        case 't': out += '\t'; break;
        case 'u': {
          uint32_t cp;
          if (!hex4(cp)) return false;
          if (cp >= 0xD800 && cp <= 0xDBFF) {
            uint32_t lo;
            if (!lit("\\u") || !hex4(lo) || lo < 0xDC00 || lo > 0xDFFF) return false;
            cp = 0x10000 + ((cp - 0xD800) << 10) + (lo - 0xDC00);
          } else if (cp >= 0xDC00 && cp <= 0xDFFF) {
            return false;
          }
          std::string tmp;  // ≤ 4 bytes: stays in the inline buffer, never on the heap
          text::appendUtf8(tmp, cp);
          out.append(tmp.data(), tmp.size());
          break;
        }
        default: return false;
      }
    }
    return false;
  }
  bool integer(int64_t& v) {
    bool neg = false;
    if (p < end && *p == '-') neg = true, ++p;
    if (p >= end || *p < '0' || *p > '9') return false;
    if (*p == '0' && end - p > 1 && p[1] >= '0' && p[1] <= '9') return false;  // leading zero
    uint64_t acc = 0;
    const uint64_t limit = neg ? uint64_t(INT64_MAX) + 1 : uint64_t(INT64_MAX);
    while (p < end && *p >= '0' && *p <= '9') {
      uint64_t d = uint64_t(*p++ - '0');
      if (acc > (limit - d) / 10) return false;
      acc = acc * 10 + d;
    }
    if (p < end && (*p == '.' || *p == 'e' || *p == 'E')) return false;
    v = neg ? int64_t(0 - acc) : int64_t(acc);
    return true;
  }
  bool value(Value& v, int depth) {
    if (depth > kMaxDepth) return false;
    ws();
    if (p >= end) return false;
    switch (*p) {
      case '{': {
        ++p;
        v.type = Value::Type::Object;
        ws();
        if (p < end && *p == '}') return ++p, true;
        for (;;) {
          ws();
          v.keys.emplace_back();
          if (!string(v.keys.back())) return false;
          ws();
          if (p >= end || *p++ != ':') return false;
          v.items.emplace_back();
          if (!value(v.items.back(), depth + 1)) return false;
          ws();
          if (p >= end) return false;
          if (*p == ',') { ++p; continue; }
          if (*p == '}') return ++p, true;
          return false;
        }
      }
      case '[': {
        ++p;
        v.type = Value::Type::Array;
        ws();
        if (p < end && *p == ']') return ++p, true;
        for (;;) {
          v.items.emplace_back();
          if (!value(v.items.back(), depth + 1)) return false;
          ws();
          if (p >= end) return false;
          if (*p == ',') { ++p; continue; }
          if (*p == ']') return ++p, true;
          return false;
        }
      }
      case '"': v.type = Value::Type::String; return string(v.s);
      case 't': v.type = Value::Type::Bool, v.b = true; return lit("true");
      case 'f': v.type = Value::Type::Bool, v.b = false; return lit("false");
      case 'n': v.type = Value::Type::Null; return lit("null");
      default: v.type = Value::Type::Int; return integer(v.i);
    }
  }
};

}  // namespace

const Value* Value::find(const char* key) const {
  if (type != Type::Object) return nullptr;
  for (size_t k = 0; k < keys.size(); ++k)
    if (keys[k] == key) return &items[k];
  return nullptr;
}

bool parse(const char* p, size_t n, Value& out) {
  out = Value{};
  Parser ps{p, p + n};
  if (!ps.value(out, 0)) return false;
  ps.ws();
  return ps.p == ps.end;
}

void writeString(SecureString& out, const char* p, size_t n) {
  static const char kHex[] = "0123456789abcdef";
  out += '"';
  for (size_t i = 0; i < n; ++i) {
    unsigned char c = static_cast<unsigned char>(p[i]);
    if (c == '"' || c == '\\') {
      out += '\\';
      out += char(c);
    } else if (c < 0x20) {
      out += "\\u00";
      out += kHex[c >> 4];
      out += kHex[c & 15];
    } else {
      out += char(c);  // UTF-8 passes through unchanged
    }
  }
  out += '"';
}

void writeInt(SecureString& out, int64_t v) {
  char buf[24];
  char* q = buf + sizeof buf;
  uint64_t u = v < 0 ? 0 - uint64_t(v) : uint64_t(v);
  do *--q = char('0' + u % 10); while (u /= 10);
  if (v < 0) *--q = '-';
  out.append(q, size_t(buf + sizeof buf - q));
}

}  // namespace keyra::vault::json
