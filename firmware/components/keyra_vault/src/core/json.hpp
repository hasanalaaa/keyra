// Just enough JSON for backups, kept in the core so it is host-testable.
// Reader: RFC 8259 objects/arrays/strings/true/false/null and *integer* numbers
// (backups never contain fractions; anything else is rejected). Depth ≤ 8.
// Strings are SecureString because decrypted backups carry passwords.
#pragma once

#include <cstdint>
#include <vector>

#include "secure_buf.hpp"

namespace keyra::vault::json {

struct Value {
  enum class Type { Null, Bool, Int, String, Array, Object } type = Type::Null;
  bool b = false;
  int64_t i = 0;
  SecureString s;
  // Zeroing allocators so growth never leaves short (inline) secrets behind.
  std::vector<SecureString, ZeroingAllocator<SecureString>> keys;  // Object: keys[k] ↔ items[k]
  std::vector<Value, ZeroingAllocator<Value>> items;  // Array elements or Object values

  const Value* find(const char* key) const;  // Object member or nullptr
};

bool parse(const char* p, size_t n, Value& out);

void writeString(SecureString& out, const char* p, size_t n);
inline void writeString(SecureString& out, const std::string& s) {
  writeString(out, s.data(), s.size());
}
void writeInt(SecureString& out, int64_t v);

}  // namespace keyra::vault::json
