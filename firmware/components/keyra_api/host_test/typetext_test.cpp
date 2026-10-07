// "Type any text" (SPEC §9.2): input rules, and proof that the text is wiped
// from RAM after typing, cancel, expiry, replacement or lock. The proof replaces
// the global allocator: every freed block is scanned for the text, so a copy
// that reaches the heap's free list un-wiped fails the test.
#include <cstdlib>
#include <cstring>
#include <new>
#include <string>

#include "actions.hpp"
#include "keyra_test.hpp"
#include "validate.hpp"

using namespace keyra::actions;
namespace validate = keyra::api::validate;

namespace {

// Long enough to live on the heap rather than inside the string object.
constexpr char kMarker[] = "Zq7-remote-keyboard-MARKER-text-0123456789";
int g_dirtyFrees = 0;

bool holdsMarker(const void* p, size_t n) {
  const size_t m = sizeof kMarker - 1;
  const auto* b = static_cast<const char*>(p);
  for (size_t i = 0; i + m <= n; ++i)
    if (std::memcmp(b + i, kMarker, m) == 0) return true;
  return false;
}

}  // namespace

// Header in front of each block records its size, so operator delete can scan it.
void* operator new(size_t n) {
  auto* b = static_cast<unsigned char*>(std::malloc(n + 16));
  if (!b) throw std::bad_alloc();
  std::memcpy(b, &n, sizeof n);
  return b + 16;
}
void operator delete(void* p) noexcept {
  if (!p) return;
  auto* b = static_cast<unsigned char*>(p) - 16;
  size_t n;
  std::memcpy(&n, b, sizeof n);
  if (holdsMarker(p, n)) ++g_dirtyFrees;
  std::free(b);
}
void operator delete(void* p, size_t) noexcept { operator delete(p); }
void* operator new[](size_t n) { return operator new(n); }
void operator delete[](void* p) noexcept { operator delete(p); }
void operator delete[](void* p, size_t) noexcept { operator delete(p); }

namespace {

int64_t g_now = 1000;

std::shared_ptr<FreeText> makeText() {
  auto t = std::make_shared<FreeText>();
  t->text.assign(kMarker);
  t->twice = true;
  return t;
}

TypeRequest textRequest(std::shared_ptr<const FreeText> t) {
  TypeRequest r;
  r.what = What::Text;
  r.text = std::move(t);
  return r;
}

// Arms free text, runs `drop` (which must make the machine let go), and checks
// that the text is gone and was zero when its buffer was freed.
template <class F>
void wipedAfter(const char* what, F drop) {
  g_dirtyFrees = 0;
  std::weak_ptr<const FreeText> watch;
  {
    Machine m([] { return g_now; });
    {
      auto t = makeText();
      watch = t;
      const Pending p = m.arm(textRequest(std::move(t)));
      CHECK(!p.req.text);  // views never carry it
      CHECK(p.req.what == What::Text && p.req.id == 0 && p.req.title.empty());
    }
    CHECK(!watch.expired());  // the slot holds the only reference
    CHECK(m.pending() && !m.pending()->req.text);
    drop(m);
    if (!watch.expired()) std::fprintf(stderr, "still held after %s\n", what);
    CHECK(watch.expired());
  }
  if (g_dirtyFrees) std::fprintf(stderr, "un-wiped text freed after %s\n", what);
  CHECK(g_dirtyFrees == 0);
}

bool typeText(const std::string& s, const char* layout = "us") {
  keyra::hid::Layout l = 0;
  CHECK(keyra::hid::findLayout(layout, l));
  return validate::typeText(s, l);
}

void validation() {
  CHECK(typeText(" "));
  CHECK(typeText("Hello, world! ~`'\"\\{}"));
  CHECK(typeText(std::string(256, 'a')));
  CHECK(!typeText(""));
  CHECK(!typeText(std::string(257, 'a')));
  CHECK(!typeText("tab\there"));
  CHECK(!typeText("line\nbreak"));
  CHECK(!typeText("cr\r"));
  CHECK(!typeText(std::string("nul\0x", 5)));
  CHECK(!typeText("del\x7f"));
  CHECK(!typeText("café"));    // not on the US layout
  CHECK(!typeText("مرحبا"));
  // The target's layout decides (SPEC §10.1).
  CHECK(typeText("مرحبا", "ar"));
  CHECK(!typeText("hello", "ar"));
  CHECK(typeText("café", "fr"));
  std::string arabic256;
  for (int i = 0; i < 256; ++i) arabic256 += "\xD8\xB4";  // 256 characters, 512 bytes
  CHECK(typeText(arabic256, "ar"));
  CHECK(!typeText(arabic256 + "\xD8\xB4", "ar"));
}

void detectorWorks() {
  // A plain std::string freed without wiping is caught, so a pass below means something.
  g_dirtyFrees = 0;
  auto* s = new std::string(kMarker);
  delete s;
  CHECK(g_dirtyFrees == 1);
}

void wipedOnEveryPath() {
  wipedAfter("cancel", [](Machine& m) { CHECK(m.cancel()); });
  wipedAfter("expiry", [](Machine& m) {
    g_now += kExpiryMs;
    CHECK(!m.pending().has_value());
    CHECK(m.last() && m.last()->code == Code::Expired && m.last()->what == What::Text);
  });
  wipedAfter("long press", [](Machine& m) { CHECK(m.onButton(Button::Long, true).effect == Effect::Cancelled); });
  wipedAfter("lock", [](Machine& m) { m.dropSessionItems(); });
  wipedAfter("another action", [](Machine& m) { m.arm({7, "Mail", What::Password, false, {}, nullptr, nullptr, 0}); });
  wipedAfter("a presence op", [](Machine& m) { m.awaitPresence(Op::Wifi, [] { return true; }); });
  wipedAfter("typing", [](Machine& m) {
    {
      Decision d = m.onButton(Button::Short, true);
      CHECK(d.effect == Effect::Run && d.run.what == What::Text);
      CHECK(d.run.text && d.run.text->text == kMarker && d.run.text->twice);
      CHECK(!m.pending().has_value());
      m.typingFinished(d.run, Code::Typed);
    }  // the typing job lets go here
    const auto last = m.last();
    CHECK(last && last->ok && last->what == What::Text && last->title.empty());
  });
}

void names() {
  CHECK(std::string(whatName(What::Text)) == "text");
  CHECK(!parseWhat("text").has_value());  // only via {text}
}

}  // namespace

int main() {
  validation();
  detectorWorks();
  wipedOnEveryPath();
  names();
  return KEYRA_TEST_RESULT();
}
