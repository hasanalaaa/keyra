#pragma once
// Minimal host-test harness: no framework dependency, ctest reads the exit code.
#include <cstdio>

inline int g_failures = 0;

#define CHECK(cond)                                                          \
  do {                                                                       \
    if (!(cond)) {                                                           \
      std::fprintf(stderr, "%s:%d: CHECK failed: %s\n", __FILE__, __LINE__, #cond); \
      ++g_failures;                                                          \
    }                                                                        \
  } while (0)

#define CHECK_EQ(a, b)                                                       \
  do {                                                                       \
    const auto va_ = (a);                                                    \
    const auto vb_ = (b);                                                    \
    if (!(va_ == vb_)) {                                                     \
      std::fprintf(stderr, "%s:%d: CHECK_EQ failed: %s (%lld) != %s (%lld)\n", __FILE__, __LINE__, #a, \
                   static_cast<long long>(va_), #b, static_cast<long long>(vb_)); \
      ++g_failures;                                                          \
    }                                                                        \
  } while (0)

#define TEST_MAIN_END()                                                      \
  do {                                                                       \
    if (g_failures) std::fprintf(stderr, "%d failure(s)\n", g_failures);     \
    else std::printf("all passed\n");                                       \
    return g_failures ? 1 : 0;                                               \
  } while (0)
