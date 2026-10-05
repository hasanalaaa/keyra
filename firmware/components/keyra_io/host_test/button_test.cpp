// Button classifier timing edges: debounce, exactly 600 / 1500 ms, Long once.
#include <initializer_list>
#include <utility>
#include <vector>

#include "button_classifier.hpp"
#include "check.hpp"

using keyra::io::ButtonClassifier;
using keyra::io::Press;

namespace {

struct Sim {
  ButtonClassifier c;
  uint32_t t;
  std::vector<std::pair<Press, uint32_t>> events;
  explicit Sim(uint32_t start = 1000, bool initiallyPressed = false) : t(start) { feed(initiallyPressed); }
  void feed(bool level) {
    const Press p = c.update(level, t);
    if (p != Press::None) events.push_back({p, t});
  }
  // Hold a level for ms, sampling every step ms (1 ms resolution for exact edges).
  void hold(bool level, uint32_t ms, uint32_t step = 1) {
    for (uint32_t e = 0; e < ms; e += step) {
      feed(level);
      t += step;
    }
  }
  int count(Press p) const {
    int n = 0;
    for (auto& e : events) n += e.first == p;
    return n;
  }
};

// Press for `heldMs` (edge to edge), then release long enough to settle.
Sim press(uint32_t heldMs, uint32_t step = 1) {
  Sim s;
  s.hold(false, 100, step);
  s.hold(true, heldMs, step);
  s.hold(false, 200, step);
  return s;
}

void testShortPress() {
  Sim s = press(100);
  CHECK_EQ(s.count(Press::Short), 1);
  CHECK_EQ(s.count(Press::Long), 0);
  // Reported once the release is debounced: 30 ms after the release edge.
  CHECK_EQ(s.events.size(), size_t{1});
  if (!s.events.empty()) CHECK_EQ(s.events[0].second, 1000u + 100 + 100 + ButtonClassifier::kDebounceMs);
}

void testShortBoundary() {
  CHECK_EQ(press(599).count(Press::Short), 1);
  CHECK_EQ(press(600).count(Press::Short), 0);  // exactly 600 ms is not Short
  CHECK_EQ(press(600).count(Press::Long), 0);
}

void testDeadZoneBetweenShortAndLong() {
  for (uint32_t ms : {600u, 900u, 1499u}) {
    Sim s = press(ms);
    CHECK_EQ(s.events.size(), size_t{0});
  }
}

void testLongBoundary() {
  Sim s;
  s.hold(false, 100);
  const uint32_t edge = s.t;
  s.hold(true, 1500);  // samples at edge .. edge+1499
  CHECK_EQ(s.count(Press::Long), 0);
  s.hold(true, 1);  // sample at edge+1500
  CHECK_EQ(s.count(Press::Long), 1);
  if (!s.events.empty()) CHECK_EQ(s.events[0].second, edge + 1500);
}

void testLongFiresOnceAndNoShortAfter() {
  Sim s = press(5000);
  CHECK_EQ(s.count(Press::Long), 1);
  CHECK_EQ(s.count(Press::Short), 0);
}

void testBounceIsOnePress() {
  Sim s;
  s.hold(false, 100);
  // Contact chatter on press: alternating every 3 ms for 20 ms.
  for (int i = 0; i < 7; ++i) s.hold(i % 2 == 0, 3);
  s.hold(true, 150);
  // Chatter on release too.
  for (int i = 0; i < 7; ++i) s.hold(i % 2 == 1, 3);
  s.hold(false, 200);
  CHECK_EQ(s.count(Press::Short), 1);
  CHECK_EQ(s.events.size(), size_t{1});
}

void testGlitchShorterThanDebounceIgnored() {
  Sim s;
  s.hold(false, 100);
  s.hold(true, 29);  // 29 ms spike: below the 30 ms debounce
  s.hold(false, 500);
  CHECK_EQ(s.events.size(), size_t{0});
  // And a 29 ms dropout during a long hold does not split it.
  Sim l;
  l.hold(false, 100);
  l.hold(true, 800);
  l.hold(false, 29);
  l.hold(true, 800);
  CHECK_EQ(l.count(Press::Long), 1);
  CHECK_EQ(l.count(Press::Short), 0);
}

void testTenMsPolling() {
  // The real task samples every 10 ms.
  CHECK_EQ(press(200, 10).count(Press::Short), 1);
  CHECK_EQ(press(2000, 10).count(Press::Long), 1);
  CHECK_EQ(press(1000, 10).events.size(), size_t{0});
}

void testHeldAtBootIgnored() {
  Sim s(1000, /*initiallyPressed=*/true);
  s.hold(true, 3000);
  s.hold(false, 200);
  CHECK_EQ(s.events.size(), size_t{0});
  // The next real press works.
  s.hold(true, 100);
  s.hold(false, 200);
  CHECK_EQ(s.count(Press::Short), 1);
}

void testClockWrap() {
  Sim s(0xFFFFFF00u);  // wraps during the press
  s.hold(false, 100);
  s.hold(true, 2000);
  s.hold(false, 100);
  s.hold(true, 100);
  s.hold(false, 100);
  CHECK_EQ(s.count(Press::Long), 1);
  CHECK_EQ(s.count(Press::Short), 1);
}

}  // namespace

int main() {
  testShortPress();
  testShortBoundary();
  testDeadZoneBetweenShortAndLong();
  testLongBoundary();
  testLongFiresOnceAndNoShortAfter();
  testBounceIsOnePress();
  testGlitchShorterThanDebounceIgnored();
  testTenMsPolling();
  testHeldAtBootIgnored();
  testClockWrap();
  TEST_MAIN_END();
}
