#include "cbor.hpp"

#include <cstring>

namespace keyra::fido::cbor {

void Writer::head(uint8_t major, uint64_t v) {
  const uint8_t m = static_cast<uint8_t>(major << 5);
  if (v < 24) {
    out.push_back(static_cast<uint8_t>(m | v));
  } else if (v <= 0xFF) {
    out.push_back(m | 24);
    out.push_back(static_cast<uint8_t>(v));
  } else if (v <= 0xFFFF) {
    out.push_back(m | 25);
    for (int s = 8; s >= 0; s -= 8) out.push_back(static_cast<uint8_t>(v >> s));
  } else if (v <= 0xFFFFFFFFull) {
    out.push_back(m | 26);
    for (int s = 24; s >= 0; s -= 8) out.push_back(static_cast<uint8_t>(v >> s));
  } else {
    out.push_back(m | 27);
    for (int s = 56; s >= 0; s -= 8) out.push_back(static_cast<uint8_t>(v >> s));
  }
}

bool Value::asInt(int64_t& out) const {
  if (type == Type::Uint && u <= static_cast<uint64_t>(INT64_MAX)) {
    out = static_cast<int64_t>(u);
    return true;
  }
  if (type == Type::Neg && u <= static_cast<uint64_t>(INT64_MAX)) {
    out = -1 - static_cast<int64_t>(u);
    return true;
  }
  return false;
}

const Value* Value::find(int64_t key) const {
  if (type != Type::Map) return nullptr;
  for (const auto& e : entries) {
    int64_t k;
    if (e.first.asInt(k) && k == key) return &e.second;
  }
  return nullptr;
}

const Value* Value::find(const char* key) const {
  if (type != Type::Map) return nullptr;
  const size_t n = std::strlen(key);
  for (const auto& e : entries) {
    if (e.first.type == Type::Text && e.first.str.size() == n && std::memcmp(e.first.str.data(), key, n) == 0)
      return &e.second;
  }
  return nullptr;
}

namespace {

bool validUtf8(const std::vector<uint8_t>& s) {
  size_t i = 0;
  while (i < s.size()) {
    const uint8_t c = s[i];
    size_t len;
    uint32_t cp;
    if (c < 0x80) {
      ++i;
      continue;
    } else if ((c & 0xE0) == 0xC0) {
      len = 2, cp = c & 0x1F;
    } else if ((c & 0xF0) == 0xE0) {
      len = 3, cp = c & 0x0F;
    } else if ((c & 0xF8) == 0xF0) {
      len = 4, cp = c & 0x07;
    } else {
      return false;
    }
    if (i + len > s.size()) return false;
    for (size_t k = 1; k < len; ++k) {
      if ((s[i + k] & 0xC0) != 0x80) return false;
      cp = (cp << 6) | (s[i + k] & 0x3F);
    }
    const uint32_t minimum = len == 2 ? 0x80 : len == 3 ? 0x800 : 0x10000;
    if (cp < minimum || cp > 0x10FFFF || (cp >= 0xD800 && cp <= 0xDFFF)) return false;
    i += len;
  }
  return true;
}

bool sameKey(const Value& a, const Value& b) {
  return a.type == b.type && a.u == b.u && a.str == b.str && (a.isInt() || a.type == Value::Type::Text);
}

class Reader {
 public:
  Reader(const uint8_t* p, size_t n) : p_(p), n_(n) {}
  size_t pos() const { return i_; }

  bool item(Value& v, int depth) {
    if (depth > kMaxDepth || i_ >= n_) return false;
    const uint8_t ib = p_[i_++];
    const uint8_t major = ib >> 5, info = ib & 0x1F;
    if (major == 7) {
      switch (info) {
        case 20: v.type = Value::Type::Bool, v.b = false; return true;
        case 21: v.type = Value::Type::Bool, v.b = true; return true;
        case 22: v.type = Value::Type::Null; return true;
        default: return false;  // undefined, simple values, floats, break
      }
    }
    uint64_t arg;
    if (!argument(info, arg)) return false;
    switch (major) {
      case 0: v.type = Value::Type::Uint, v.u = arg; return true;
      case 1: v.type = Value::Type::Neg, v.u = arg; return true;
      case 2:
      case 3:
        if (arg > n_ - i_) return false;
        v.type = major == 2 ? Value::Type::Bytes : Value::Type::Text;
        v.str.assign(p_ + i_, p_ + i_ + arg);
        i_ += static_cast<size_t>(arg);
        return major == 2 || validUtf8(v.str);
      case 4:
        if (arg > n_ - i_) return false;  // every item takes at least one byte
        v.type = Value::Type::Array;
        v.items.resize(static_cast<size_t>(arg));
        for (auto& x : v.items)
          if (!item(x, depth + 1)) return false;
        return true;
      case 5:
        if (arg > (n_ - i_) / 2) return false;
        v.type = Value::Type::Map;
        v.entries.resize(static_cast<size_t>(arg));
        for (size_t k = 0; k < v.entries.size(); ++k) {
          auto& e = v.entries[k];
          if (!item(e.first, depth + 1) || !item(e.second, depth + 1)) return false;
          for (size_t j = 0; j < k; ++j)
            if (sameKey(v.entries[j].first, e.first)) return false;
        }
        return true;
      default: return false;  // 6 = tags
    }
  }

 private:
  bool argument(uint8_t info, uint64_t& out) {
    if (info < 24) {
      out = info;
      return true;
    }
    if (info > 27) return false;  // 28-30 reserved, 31 indefinite length
    const size_t len = size_t{1} << (info - 24);
    if (len > n_ - i_) return false;
    out = 0;
    for (size_t k = 0; k < len; ++k) out = (out << 8) | p_[i_++];
    return true;
  }

  const uint8_t* p_;
  size_t n_;
  size_t i_ = 0;
};

}  // namespace

bool decode(const uint8_t* p, size_t n, Value& out) {
  out = Value{};
  Reader r(p, n);
  return r.item(out, 0) && r.pos() == n;
}

}  // namespace keyra::fido::cbor
