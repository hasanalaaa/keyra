// Keymap: every printable ASCII char maps to the US-ANSI key a human would
// press; everything else is rejected. The expectation is derived from the
// physical keyboard rows, independently of the table in keymap.cpp.
#include <cstdint>
#include <cstring>
#include <map>
#include <string>

#include "check.hpp"
#include "keymap.hpp"
#include "keyra/hid.hpp"

using keyra::hid::KeyStroke;
using keyra::hid::keystrokeFor;
using keyra::hid::typeable;

namespace {

struct Row {
  const char* plain;
  const char* shifted;
  uint8_t codes[13];
};

std::map<char, KeyStroke> expected() {
  std::map<char, KeyStroke> m;
  // Non-letter keys, left to right on each US-ANSI row.
  const Row rows[] = {
      {"`1234567890-=", "~!@#$%^&*()_+", {0x35, 0x1E, 0x1F, 0x20, 0x21, 0x22, 0x23, 0x24, 0x25, 0x26, 0x27, 0x2D, 0x2E}},
      {"[]\\", "{}|", {0x2F, 0x30, 0x31}},
      {";'", ":\"", {0x33, 0x34}},
      {",./", "<>?", {0x36, 0x37, 0x38}},
  };
  for (const Row& r : rows) {
    for (size_t i = 0; i < std::strlen(r.plain); ++i) {
      m[r.plain[i]] = {r.codes[i], false};
      m[r.shifted[i]] = {r.codes[i], true};
    }
  }
  for (int i = 0; i < 26; ++i) {
    m[static_cast<char>('a' + i)] = {static_cast<uint8_t>(0x04 + i), false};
    m[static_cast<char>('A' + i)] = {static_cast<uint8_t>(0x04 + i), true};
  }
  m[' '] = {0x2C, false};
  return m;
}

}  // namespace

int main() {
  const auto exp = expected();
  CHECK_EQ(exp.size(), size_t{95});  // the reference itself covers 0x20..0x7E

  // Every printable char.
  for (int c = 0x20; c <= 0x7E; ++c) {
    KeyStroke ks{};
    const bool ok = keystrokeFor(static_cast<char>(c), ks);
    CHECK(ok);
    const auto it = exp.find(static_cast<char>(c));
    CHECK(it != exp.end());
    if (ok && it != exp.end()) {
      if (ks.keycode != it->second.keycode || ks.shift != it->second.shift) {
        std::fprintf(stderr, "char 0x%02X '%c': got %02X/%d want %02X/%d\n", c, c, ks.keycode, ks.shift,
                     it->second.keycode, it->second.shift);
        ++g_failures;
      }
    }
  }

  // Everything else: control chars (incl. \t \n \r), DEL, all high bytes.
  for (int c = 0; c < 256; ++c) {
    if (c >= 0x20 && c <= 0x7E) continue;
    KeyStroke ks{};
    if (keystrokeFor(static_cast<char>(c), ks)) {
      std::fprintf(stderr, "byte 0x%02X unexpectedly mapped\n", c);
      ++g_failures;
    }
  }

  std::string all;
  for (int c = 0x20; c <= 0x7E; ++c) all.push_back(static_cast<char>(c));
  CHECK(typeable(all.c_str()));
  CHECK(typeable(""));
  CHECK(typeable("Tr0ub4dor&3 correct-horse!"));
  CHECK(!typeable(nullptr));
  CHECK(!typeable("\n"));
  CHECK(!typeable("\t"));
  CHECK(!typeable("pass\n"));
  CHECK(!typeable("user\tpass"));
  CHECK(!typeable("\r"));
  CHECK(!typeable("\x7F"));
  CHECK(!typeable("كلمة السر"));        // Arabic (UTF-8)
  CHECK(!typeable("pass\xD9\x83word"));  // one Arabic letter inside ASCII
  CHECK(!typeable("caf\xC3\xA9"));       // é
  CHECK(!typeable("\xE2\x82\xAC"));      // €
  CHECK(!typeable("\xF0\x9F\x94\x91"));  // 🔑

  TEST_MAIN_END();
}
