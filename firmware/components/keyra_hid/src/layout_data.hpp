#pragma once
// Shape of the generated layout tables (layouts/layouts.txt → layouts_gen.cpp,
// produced at build time by layouts/gen_layouts.py).
#include <cstddef>
#include <cstdint>

namespace keyra::hid::data {

enum class Platform : uint8_t { Any, Windows, Mac };

constexpr uint8_t kShift = 1, kAlt = 2, kDead = 4;

// How to type one character: the key, Shift and/or AltGr/Option, and whether
// the key is a dead key that types the character only when followed by Space.
// Sorted by code point; each character appears once (the simplest way to type it).
struct Glyph {
  uint16_t cp;
  uint8_t usage;
  uint8_t flags;
};

// What one key types in each layer (base, Shift, Alt, Shift+Alt); 0 = nothing.
// deadMask bit n: that layer is a dead key (cp = what it types before Space).
struct KeyCell {
  uint8_t usage;
  uint16_t cp[4];
  uint8_t deadMask;
};

struct LayoutDef {
  const char* id;
  const char* name;
  Platform platform;
  const Glyph* glyphs;
  size_t glyphCount;
  const KeyCell* keys;
  size_t keyCount;
};

extern const LayoutDef kLayouts[];
extern const size_t kLayoutCount;

}  // namespace keyra::hid::data
