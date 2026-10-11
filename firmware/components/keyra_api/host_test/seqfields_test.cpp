// seqrun::Fields holds a sequence's username, password and 2FA code while it
// types (runtime.cpp runSequence). It must wipe them on every way out, an early
// return included. The global allocator is replaced: every freed block is
// scanned for the secret, so a copy freed un-wiped fails the test.
#include <cstdlib>
#include <cstring>
#include <new>
#include <string>

#include "keyra_test.hpp"
#include "sequence_run.hpp"

namespace {

// Long enough to live on the heap rather than inside the string object.
constexpr char kMarker[] = "Zq7-sequence-password-MARKER-0123456789-abcdef";
int g_dirtyFrees = 0;

bool holdsMarker(const void* p, size_t n) {
  const size_t m = sizeof kMarker - 1;
  const auto* b = static_cast<const char*>(p);
  for (size_t i = 0; i + m <= n; ++i)
    if (std::memcmp(b + i, kMarker, m) == 0) return true;
  return false;
}

}  // namespace

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

using keyra::actions::Code;

// The shape of runSequence: fields filled, then a 2FA code that cannot be made.
Code fillThenFail(const std::string& secret, bool codeFails) {
  keyra::api::seqrun::Fields f;
  f.username = secret;
  f.password = secret;
  if (codeFails) return Code::Failed;  // the early return that used to skip the wipe
  f.totp = "123456";
  const keyra::api::seqrun::Fields copy = f;  // copies wipe themselves too
  return copy.password.empty() ? Code::Failed : Code::Typed;
}

void wipedOnEveryReturn() {
  const std::string* secret = new std::string(kMarker);
  for (const bool fails : {true, false}) {
    g_dirtyFrees = 0;
    CHECK(fillThenFail(*secret, fails) == (fails ? Code::Failed : Code::Typed));
    CHECK(g_dirtyFrees == 0);
  }
  // The scan itself works: an un-wiped string is caught.
  g_dirtyFrees = 0;
  delete secret;
  CHECK(g_dirtyFrees == 1);
}

}  // namespace

int main() {
  wipedOnEveryReturn();
  return KEYRA_TEST_RESULT();
}
