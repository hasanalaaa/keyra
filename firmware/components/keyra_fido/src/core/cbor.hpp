#pragma once
// The CBOR subset CTAP2 uses (RFC 8949): unsigned/negative integers, byte and
// text strings, arrays, maps, true/false/null. Definite lengths only; the
// decoder rejects indefinite lengths, tags, floats and nesting deeper than
// kMaxDepth. The encoder writes the shortest heads (CTAP2 canonical form);
// callers emit map keys in canonical order themselves.
#include <cstddef>
#include <cstdint>
#include <string>
#include <utility>
#include <vector>

namespace keyra::fido::cbor {

class Writer {
 public:
  std::vector<uint8_t> out;

  void uint(uint64_t v) { head(0, v); }
  void integer(int64_t v) {
    if (v >= 0) head(0, static_cast<uint64_t>(v));
    else head(1, static_cast<uint64_t>(-1 - v));
  }
  void bytes(const uint8_t* p, size_t n) {
    head(2, n);
    out.insert(out.end(), p, p + n);
  }
  void bytes(const std::vector<uint8_t>& v) { bytes(v.data(), v.size()); }
  void text(const std::string& s) {
    head(3, s.size());
    out.insert(out.end(), s.begin(), s.end());
  }
  void array(size_t n) { head(4, n); }
  void map(size_t n) { head(5, n); }
  void boolean(bool b) { out.push_back(b ? 0xF5 : 0xF4); }

 private:
  void head(uint8_t major, uint64_t v);
};

struct Value {
  enum class Type { Uint, Neg, Bytes, Text, Array, Map, Bool, Null };
  Type type = Type::Null;
  uint64_t u = 0;            // Uint: the value; Neg: -1 - value
  bool b = false;
  std::vector<uint8_t> str;  // Bytes and Text (Text is checked to be UTF-8)
  std::vector<Value> items;  // Array
  std::vector<std::pair<Value, Value>> entries;  // Map, in encoded order

  bool isInt() const { return type == Type::Uint || type == Type::Neg; }
  // Uint/Neg as int64 when it fits; false otherwise.
  bool asInt(int64_t& out) const;
  std::string text() const { return std::string(str.begin(), str.end()); }
  // Map lookup by integer or text key; nullptr when absent (or not a map).
  const Value* find(int64_t key) const;
  const Value* find(const char* key) const;
};

constexpr int kMaxDepth = 8;

// Decodes exactly one item that fills [p, p+n). False on malformed input,
// trailing bytes, unsupported types, invalid UTF-8 or duplicate map keys.
bool decode(const uint8_t* p, size_t n, Value& out);

}  // namespace keyra::fido::cbor
