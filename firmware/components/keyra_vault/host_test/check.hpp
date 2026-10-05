// Minimal test harness: TEST(name) registers a case; CHECK records a failure
// and keeps going so one run reports everything.
#pragma once

#include <cstdio>
#include <functional>
#include <vector>

namespace check {
struct Case {
  const char* name;
  std::function<void()> fn;
};
inline std::vector<Case>& cases() {
  static std::vector<Case> c;
  return c;
}
inline int& failures() {
  static int f = 0;
  return f;
}
struct Reg {
  Reg(const char* n, std::function<void()> f) { cases().push_back({n, std::move(f)}); }
};
inline int runAll() {
  for (auto& c : cases()) {
    int before = failures();
    c.fn();
    std::printf("%s %s\n", failures() == before ? "PASS" : "FAIL", c.name);
  }
  std::printf("%zu cases, %d failed checks\n", cases().size(), failures());
  return failures() ? 1 : 0;
}
}  // namespace check

#define TEST_CAT2(a, b) a##b
#define TEST_CAT(a, b) TEST_CAT2(a, b)
#define TEST(name)                                                         \
  static void name();                                                      \
  static check::Reg TEST_CAT(reg_, name)(#name, name);                     \
  static void name()

#define CHECK(cond)                                                              \
  do {                                                                           \
    if (!(cond)) {                                                               \
      std::fprintf(stderr, "%s:%d: CHECK(%s) failed\n", __FILE__, __LINE__, #cond); \
      ++check::failures();                                                       \
    }                                                                            \
  } while (0)

#define TEST_MAIN() \
  int main() { return check::runAll(); }
