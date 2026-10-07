// Password health (SPEC §13): the strength estimate matches the web meter's
// bands, reuse groups equal passwords only, and "old" needs a known clock.
#include <string>
#include <vector>

#include "health.hpp"
#include "keyra_test.hpp"

using namespace keyra::api::health;

namespace {

constexpr int64_t kDay = 24 * 60 * 60;
constexpr int64_t kNow = 1790000000;  // 2026-09

void levels() {
  CHECK(level("") == 0);
  CHECK(level("password") == 1);
  CHECK(level("PassWord") == 1);  // the common list ignores case
  CHECK(level("abc") == 1);
  CHECK(level("hasan2000") == 2);    // 9 × log2(36) ≈ 46.5
  CHECK(level("Hasan2019!") == 3);   // 10 × log2(95) ≈ 65.7
  CHECK(level("Hasan2000!") == 2);   // ... less 8 bits for "000"
  CHECK(level("k7#Qv9!pL2@xW4$z") == 4);
  // Runs and sequences cost 8 bits each, as on the phone.
  CHECK(bits("aaaa") < bits("abzq"));
  CHECK(bits("abcd") < bits("abzq"));
  // Arabic letters widen the pool; other scripts count like symbols.
  CHECK(bits("\xD9\x85\xD8\xB1\xD8\xAD\xD8\xA8\xD8\xA7") > 0);  // مرحبا
  CHECK(level("\xD9\x85\xD8\xB1\xD8\xAD\xD8\xA8\xD8\xA7") == 1);
}

void reuseAndAge() {
  const std::string a = "Hasan2019!", b = "k7#Qv9!pL2@xW4$z", c = "hasan2000";
  const std::vector<Item> items = {
      {7, a, kNow - 10 * kDay},
      {3, b, kNow - 400 * kDay},
      {9, a, 0},
      {4, "", kNow - 900 * kDay},  // notes only: not checked at all
      {5, c, kNow - 366 * kDay},
      {2, a, kNow - kDay},
  };
  const Report r = check(items, kNow);
  CHECK(r.checked == 5);
  CHECK(r.reused.size() == 1);
  CHECK((r.reused[0] == std::vector<uint32_t>{2, 7, 9}));
  CHECK(r.weak.size() == 1);
  CHECK(r.weak[0].first == 5 && r.weak[0].second == 2);
  CHECK(r.old.size() == 2);
  CHECK(r.old[0].first == 3 && r.old[1].first == 5);
  // Exactly a year is not old yet; unknown dates never are.
  CHECK(check({{1, b, kNow - kOldAfterSec}}, kNow).old.empty());
  // No clock: nothing is old, the rest still works.
  const Report noClock = check(items, 0);
  CHECK(noClock.old.empty() && noClock.reused.size() == 1);
  // Passwords that differ only in case are different passwords.
  CHECK(check({{1, "Abcdefgh1!xy", 0}, {2, "abcdefgh1!xy", 0}}, kNow).reused.empty());
  CHECK(check({}, kNow).checked == 0);
}

}  // namespace

int main() {
  levels();
  reuseAndAge();
  return KEYRA_TEST_RESULT();
}
