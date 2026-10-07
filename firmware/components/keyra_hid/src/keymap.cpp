#include "keymap.hpp"

#include <algorithm>
#include <cstring>

#include "keyra/hid.hpp"
#include "layout_data.hpp"

namespace keyra::hid {
namespace {

using data::kLayouts;

const data::LayoutDef* def(Layout l) { return l < data::kLayoutCount ? &kLayouts[l] : nullptr; }

uint8_t altBit(const data::LayoutDef& d) {
  return d.platform == data::Platform::Mac ? MOD_LEFT_ALT : MOD_RIGHT_ALT;
}

const data::Glyph* glyph(const data::LayoutDef& d, uint32_t cp) {
  if (cp > 0xFFFF) return nullptr;
  const data::Glyph* end = d.glyphs + d.glyphCount;
  const data::Glyph* g =
      std::lower_bound(d.glyphs, end, cp, [](const data::Glyph& a, uint32_t v) { return a.cp < v; });
  return g != end && g->cp == cp ? g : nullptr;
}

const char* platformName(data::Platform p) {
  switch (p) {
    case data::Platform::Windows: return "windows";
    case data::Platform::Mac: return "mac";
    case data::Platform::Any: break;
  }
  return "any";
}

void appendUtf8(std::string& s, uint32_t cp) {
  if (cp < 0x80) {
    s += static_cast<char>(cp);
  } else if (cp < 0x800) {
    s += static_cast<char>(0xC0 | (cp >> 6));
    s += static_cast<char>(0x80 | (cp & 0x3F));
  } else {
    s += static_cast<char>(0xE0 | (cp >> 12));
    s += static_cast<char>(0x80 | ((cp >> 6) & 0x3F));
    s += static_cast<char>(0x80 | (cp & 0x3F));
  }
}

}  // namespace

const ProbeKey kProbe[] = {{0x14, false}, {0x1A, false}, {0x1C, false}, {0x1D, false}, {0x2C, false},
                           {0x33, false}, {0x1F, true},  {0x20, true},  {0x38, false}};
const size_t kProbeLen = sizeof kProbe / sizeof kProbe[0];

int strokesFor(Layout layout, uint32_t cp, KeyStroke out[kMaxStrokes]) {
  const data::LayoutDef* d = def(layout);
  if (d == nullptr || cp < 0x20 || (cp >= 0x7F && cp < 0xA0)) return 0;
  const data::Glyph* g = glyph(*d, cp);
  if (g == nullptr) return 0;
  uint8_t mod = 0;
  if (g->flags & data::kShift) mod |= MOD_LEFT_SHIFT;
  if (g->flags & data::kAlt) mod |= altBit(*d);
  out[0] = {g->usage, mod};
  if (!(g->flags & data::kDead)) return 1;
  out[1] = {KEY_SPACE, 0};
  return 2;
}

bool nextCodePoint(const char*& p, const char* end, uint32_t& cp) {
  if (p >= end) return false;
  const auto c = static_cast<uint8_t>(*p);
  size_t len;
  if (c < 0x80) {
    len = 1;
    cp = c;
  } else if ((c & 0xE0) == 0xC0) {
    len = 2;
    cp = c & 0x1F;
  } else if ((c & 0xF0) == 0xE0) {
    len = 3;
    cp = c & 0x0F;
  } else if ((c & 0xF8) == 0xF0) {
    len = 4;
    cp = c & 0x07;
  } else {
    return false;
  }
  if (static_cast<size_t>(end - p) < len) return false;
  for (size_t k = 1; k < len; ++k) {
    const auto cc = static_cast<uint8_t>(p[k]);
    if ((cc & 0xC0) != 0x80) return false;
    cp = (cp << 6) | (cc & 0x3F);
  }
  static constexpr uint32_t kMinForLen[] = {0, 0, 0x80, 0x800, 0x10000};
  if (cp < kMinForLen[len] || cp > 0x10FFFF || (cp >= 0xD800 && cp <= 0xDFFF)) return false;
  p += len;
  return true;
}

uint32_t charFor(Layout layout, KeyStroke s) {
  const data::LayoutDef* d = def(layout);
  if (d == nullptr) return 0;
  const uint8_t alt = altBit(*d);
  if (s.modifier & ~(MOD_LEFT_SHIFT | alt)) return 0;
  const int layer = ((s.modifier & MOD_LEFT_SHIFT) ? 1 : 0) | ((s.modifier & alt) ? 2 : 0);
  for (size_t i = 0; i < d->keyCount; ++i) {
    const data::KeyCell& k = d->keys[i];
    if (k.usage != s.keycode) continue;
    return (k.deadMask & (1u << layer)) ? 0 : k.cp[layer];
  }
  return 0;
}

size_t layoutCount() { return data::kLayoutCount; }

LayoutInfo layoutInfo(Layout l) {
  const data::LayoutDef* d = def(l);
  if (d == nullptr) return {"", "", "any"};
  return {d->id, d->name, platformName(d->platform)};
}

bool findLayout(std::string_view id, Layout& out) {
  for (size_t i = 0; i < data::kLayoutCount; ++i) {
    if (id == kLayouts[i].id) {
      out = static_cast<Layout>(i);
      return true;
    }
  }
  return false;
}

bool typeable(std::string_view text, Layout layout) {
  KeyStroke ks[kMaxStrokes];
  const char* end = text.data() + text.size();
  for (const char* p = text.data(); p < end;) {
    uint32_t cp = 0;
    if (!nextCodePoint(p, end, cp) || strokesFor(layout, cp, ks) == 0) return false;
  }
  return true;
}

bool typeable(const char* text, Layout layout) { return text != nullptr && typeable(std::string_view(text), layout); }

bool sameOnAll(uint32_t cp, const Layout* layouts, size_t n) {
  if (n == 0) return false;
  KeyStroke first[kMaxStrokes];
  if (strokesFor(layouts[0], cp, first) != 1) return false;
  for (size_t i = 1; i < n; ++i) {
    KeyStroke ks[kMaxStrokes];
    if (strokesFor(layouts[i], cp, ks) != 1 || ks[0].keycode != first[0].keycode ||
        ks[0].modifier != first[0].modifier)
      return false;
  }
  return true;
}

std::string probeText(Layout layout) {
  std::string s;
  for (size_t i = 0; i < kProbeLen; ++i) {
    const uint32_t cp = charFor(layout, {kProbe[i].keycode, kProbe[i].shift ? MOD_LEFT_SHIFT : uint8_t{0}});
    if (cp != 0) appendUtf8(s, cp);
  }
  return s;
}

}  // namespace keyra::hid
