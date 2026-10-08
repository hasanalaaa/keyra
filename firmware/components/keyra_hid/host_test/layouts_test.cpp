// Keyboard layouts (SPEC §10.1). Every character a layout declares must
// round-trip: the key presses Keyra sends for it must give that very character
// according to an independent reverse table (Unicode CLDR, cldr_reverse.inc),
// and every character CLDR says a layout's keys type must be typeable.
#include <cstdio>
#include <cstring>
#include <map>
#include <set>
#include <string>
#include <vector>

#include "check.hpp"
#include "keymap.hpp"
#include "layout_data.hpp"

using namespace keyra::hid;

namespace {

struct Rev {
  const char* layout;
  uint8_t usage;
  int layer;
  uint32_t cp;
  bool dead;
};
const Rev kReverse[] = {
#include "cldr_reverse.inc"
};

// Where the operating system's current data (layouts.txt) and CLDR release 43
// disagree. Each was checked by hand; layouts.txt follows the OS.
struct Known {
  const char* layout;
  uint8_t usage;
  int layer;
  const char* why;
};
const Known kKnown[] = {
#include "cldr_known.inc"
};

bool known(const char* layout, uint8_t usage, int layer) {
  for (const Known& k : kKnown)
    if (std::strcmp(k.layout, layout) == 0 && k.usage == usage && k.layer == layer) return true;
  return false;
}

Layout id(const char* name) {
  Layout l = 0;
  CHECK(findLayout(name, l));
  return l;
}

int layerOf(const data::LayoutDef& d, uint8_t mod) {
  const uint8_t alt = d.platform == data::Platform::Mac ? MOD_LEFT_ALT : MOD_RIGHT_ALT;
  return ((mod & MOD_LEFT_SHIFT) ? 1 : 0) | ((mod & alt) ? 2 : 0);
}

void testRoundTripAgainstCldr() {
  std::set<std::string> checked;
  for (size_t li = 0; li < data::kLayoutCount; ++li) {
    const data::LayoutDef& d = data::kLayouts[li];
    std::multimap<std::pair<int, int>, const Rev*> rev;
    for (const Rev& r : kReverse)
      if (std::strcmp(r.layout, d.id) == 0) rev.insert({{r.usage, r.layer}, &r});
    if (rev.empty()) continue;  // no CLDR data: checked against the OS dump and on hardware
    checked.insert(d.id);

    // Forward: every declared character, through the strokes Keyra sends.
    for (size_t gi = 0; gi < d.glyphCount; ++gi) {
      const uint32_t cp = d.glyphs[gi].cp;
      KeyStroke ks[kMaxStrokes];
      const int n = strokesFor(static_cast<Layout>(li), cp, ks);
      CHECK(n == 1 || n == 2);
      if (n < 1) continue;
      if (n == 2) CHECK(ks[1].keycode == KEY_SPACE && ks[1].modifier == 0);
      const int layer = layerOf(d, ks[0].modifier);
      if (known(d.id, ks[0].keycode, layer)) continue;
      bool ok = false;
      auto range = rev.equal_range({ks[0].keycode, layer});
      for (auto it = range.first; it != range.second; ++it)
        ok |= it->second->cp == cp && it->second->dead == (n == 2);
      if (!ok) {
        std::fprintf(stderr, "%s: U+%04X via key %02X layer %d (%s) disagrees with CLDR\n", d.id, unsigned(cp),
                     ks[0].keycode, layer, n == 2 ? "dead" : "plain");
        ++g_failures;
      }
    }
    // Completeness: whatever CLDR says the keys type, Keyra can type.
    for (const auto& [key, r] : rev) {
      if (known(d.id, r->usage, r->layer)) continue;
      KeyStroke ks[kMaxStrokes];
      if (strokesFor(static_cast<Layout>(li), r->cp, ks) == 0) {
        std::fprintf(stderr, "%s: CLDR key %02X layer %d types U+%04X but the layout cannot\n", d.id, r->usage,
                     r->layer, unsigned(r->cp));
        ++g_failures;
      }
    }
  }
  CHECK_EQ(checked.size(), data::kLayoutCount - 3);  // es-mac, it-mac, ar-pc-mac: no CLDR data
}

void testTableShape() {
  CHECK(std::strcmp(data::kLayouts[0].id, "us") == 0);
  std::set<std::string> ids;
  for (size_t li = 0; li < data::kLayoutCount; ++li) {
    const data::LayoutDef& d = data::kLayouts[li];
    CHECK(ids.insert(d.id).second);
    for (size_t gi = 0; gi < d.glyphCount; ++gi) {
      const data::Glyph& g = d.glyphs[gi];
      CHECK(g.cp >= 0x20 && !(g.cp >= 0x7F && g.cp < 0xA0));
      if (gi > 0) CHECK(d.glyphs[gi - 1].cp < g.cp);  // sorted, unique
      if (g.cp == 0x20) CHECK(g.usage == KEY_SPACE && g.flags == 0);
      CHECK(g.usage != 0x28 && g.usage != 0x2B);  // never Enter or Tab
    }
    // Every layout types a space and every Latin-script layout all of a-z.
    KeyStroke ks[kMaxStrokes];
    CHECK_EQ(strokesFor(static_cast<Layout>(li), ' ', ks), 1);
    if (std::strncmp(d.id, "ar", 2) != 0) {
      for (char c = 'a'; c <= 'z'; ++c) CHECK_EQ(strokesFor(static_cast<Layout>(li), c, ks), 1);
    }
    LayoutInfo info = layoutInfo(static_cast<Layout>(li));
    CHECK(std::strcmp(info.id, d.id) == 0);
  }
  Layout l = 0;
  CHECK(!findLayout("klingon", l));
  CHECK(!findLayout("", l));
}

void testArabicHosts() {
  KeyStroke ks[kMaxStrokes];
  // Windows Arabic (101): no Latin letters at all; digits and most ASCII symbols stay.
  const Layout ar = id("ar");
  CHECK_EQ(strokesFor(ar, 'a', ks), 0);
  CHECK(typeable("2024!@#$%^&*()-_=+[]{}<>,./\\|:\"~", ar));
  CHECK(!typeable("?", ar));  // only the Arabic question mark exists
  CHECK(!typeable(";", ar));
  CHECK(!typeable("'", ar));
  CHECK(!typeable("`", ar));
  CHECK_EQ(strokesFor(ar, '(', ks), 1);  // mirrored: Shift+0 types "("
  CHECK(ks[0].keycode == 0x27 && ks[0].modifier == MOD_LEFT_SHIFT);
  CHECK(typeable("\xD9\x83\xD9\x84\xD9\x85\xD8\xA9 \xD8\xB3\xD8\xB1", ar));  // "كلمة سر"
  // macOS Arabic: Arabic-Indic digits on the number row.
  const Layout arMac = id("ar-mac");
  CHECK_EQ(strokesFor(arMac, 0x0661, ks), 1);  // ١
  CHECK(ks[0].keycode == 0x1E && ks[0].modifier == 0);
}

void testLayoutSafe() {
  const Layout usDe[] = {id("us"), id("de")};
  CHECK(sameOnAll('a', usDe, 2));
  CHECK(!sameOnAll('y', usDe, 2));  // QWERTZ swaps Y and Z
  CHECK(!sameOnAll('-', usDe, 2));
  CHECK(sameOnAll('1', usDe, 2));
  const Layout usAr[] = {id("us"), id("ar")};
  CHECK(sameOnAll('7', usAr, 2));
  CHECK(sameOnAll('!', usAr, 2));
  CHECK(!sameOnAll('a', usAr, 2));
  CHECK(!sameOnAll('(', usAr, 2));  // US Shift+9, Arabic Shift+0
  const Layout usOnly[] = {id("us")};
  for (char c = 0x21; c < 0x7F; ++c) CHECK(sameOnAll(static_cast<unsigned char>(c), usOnly, 1));
  CHECK(!sameOnAll('a', usDe, 0));
  const Layout deOnly[] = {id("de")};
  CHECK(!sameOnAll('^', deOnly, 1));  // a dead key is never layout-proof
}

void testProbe() {
  for (size_t li = 0; li < data::kLayoutCount; ++li) {
    for (size_t i = 0; i < kProbeLen; ++i) {
      CHECK(kProbe[i].keycode != 0x28);
      const uint32_t cp =
          charFor(static_cast<Layout>(li), {kProbe[i].keycode, kProbe[i].shift ? MOD_LEFT_SHIFT : uint8_t{0}});
      if (cp == 0) {
        std::fprintf(stderr, "%s: probe key %zu types nothing or is dead\n", data::kLayouts[li].id, i);
        ++g_failures;
      }
    }
  }
  // Within one platform (plus the layouts every platform shares) each layout
  // leaves a different string, so the phone can tell them apart.
  for (size_t a = 0; a < data::kLayoutCount; ++a) {
    for (size_t b = a + 1; b < data::kLayoutCount; ++b) {
      const auto pa = data::kLayouts[a].platform, pb = data::kLayouts[b].platform;
      if (pa != pb && pa != data::Platform::Any && pb != data::Platform::Any) continue;
      if (probeText(static_cast<Layout>(a)) == probeText(static_cast<Layout>(b))) {
        std::fprintf(stderr, "probe cannot tell %s from %s\n", data::kLayouts[a].id, data::kLayouts[b].id);
        ++g_failures;
      }
    }
  }
  CHECK(probeText(id("us")) == "qwyz ;@#/");
  CHECK(probeText(id("de")) == "qwzy \xC3\xB6\"\xC2\xA7-");
}

}  // namespace

// GET /api/keyboard "chars": exactly what typeable() accepts, so the app checks text the way Keyra does.
void testLayoutChars() {
  for (size_t i = 0; i < layoutCount(); ++i) {
    const auto l = static_cast<Layout>(i);
    const std::string chars = layoutChars(l);
    CHECK(typeable(chars, l));
    size_t n = 0;
    for (const char* p = chars.data(); p < chars.data() + chars.size(); ++n) {
      uint32_t cp = 0;
      CHECK(nextCodePoint(p, chars.data() + chars.size(), cp));
    }
    size_t want = 0;
    for (uint32_t cp = 0x20; cp <= 0xFFFF; ++cp) {
      KeyStroke ks[kMaxStrokes];
      if (strokesFor(l, cp, ks) > 0) ++want;
    }
    CHECK(n == want);
  }
  CHECK(layoutChars(id("us")).size() == 95);  // printable ASCII, nothing else
  CHECK(layoutChars(id("ar")).find('a') == std::string::npos);
}

int main() {
  testTableShape();
  testLayoutChars();
  testRoundTripAgainstCldr();
  testArabicHosts();
  testLayoutSafe();
  testProbe();
  TEST_MAIN_END();
}
