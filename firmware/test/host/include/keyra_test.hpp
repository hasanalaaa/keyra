#pragma once
// Minimal assertion helpers for plain-C++ host tests. A test binary returns
// KEYRA_TEST_RESULT() from main(); ctest treats non-zero as failure.
#include <cstdio>

namespace keyra_test {
inline int& failures() {
  static int count = 0;
  return count;
}
}  // namespace keyra_test

#define CHECK(cond)                                                              \
  do {                                                                           \
    if (!(cond)) {                                                               \
      std::fprintf(stderr, "%s:%d: CHECK failed: %s\n", __FILE__, __LINE__, #cond); \
      ++keyra_test::failures();                                                  \
    }                                                                            \
  } while (0)

#define CHECK_EQ(a, b) CHECK((a) == (b))

#define KEYRA_TEST_RESULT()                                                        \
  (keyra_test::failures() == 0 ? (std::printf("all checks passed\n"), 0)          \
                               : (std::fprintf(stderr, "%d check(s) failed\n", keyra_test::failures()), 1))
