// Activity log encoding (SPEC §15): round trip, the 200-event ring, title
// clipping on UTF-8 boundaries, and refusal of damaged data.
#include <string>
#include <vector>

#include "activity.hpp"
#include "keyra_test.hpp"

using namespace keyra::api::activity;

namespace {

Event ev(Kind k, uint32_t id = 0, std::string title = "") {
  Event e;
  e.kind = k;
  e.at = 1790000000;
  e.id = id;
  e.title = std::move(title);
  return e;
}

void roundTrip() {
  std::vector<Event> log;
  append(log, ev(Kind::Unlock));
  Event f = ev(Kind::FailedUnlocks);
  f.n = 3;
  append(log, f);
  Event t = ev(Kind::Typed, 0xDEADBEEF, "GitHub");
  t.detail = 1;
  append(log, t);
  append(log, ev(Kind::Revealed, 7, "\xD8\xA8\xD9\x86\xD9\x83"));  // بنك
  std::vector<Event> back;
  CHECK(decode(encode(log), back));
  CHECK(back.size() == 4);
  CHECK(back[1].kind == Kind::FailedUnlocks && back[1].n == 3);
  CHECK(back[2].id == 0xDEADBEEF && back[2].title == "GitHub" && back[2].detail == 1 && back[2].at == 1790000000);
  CHECK(back[3].title == "\xD8\xA8\xD9\x86\xD9\x83");
  CHECK(encode({}).empty());
  CHECK(decode({}, back) && back.empty());
  CHECK(std::string(kindName(Kind::Typed)) == "typed");
  CHECK(std::string(kindName(static_cast<Kind>(99))) == "unknown");
}

void ringAndClip() {
  std::vector<Event> log;
  for (uint32_t i = 1; i <= kMaxEvents + 25; ++i) append(log, ev(Kind::Typed, i));
  CHECK(log.size() == kMaxEvents);
  CHECK(log.front().id == 26 && log.back().id == kMaxEvents + 25);  // oldest dropped
  // A long Arabic title is cut without splitting a letter.
  std::string ar;
  for (int i = 0; i < 40; ++i) ar += "\xD9\x85";  // م × 40 = 80 bytes
  append(log, ev(Kind::Typed, 1, ar));
  CHECK(log.back().title.size() == kMaxTitle);
  std::vector<Event> back;
  CHECK(decode(encode(log), back) && back.back().title == log.back().title);
}

void damaged() {
  std::vector<Event> log;
  append(log, ev(Kind::Typed, 5, "GitHub"));
  std::vector<uint8_t> b = encode(log);
  std::vector<Event> out;
  std::vector<uint8_t> cut(b.begin(), b.end() - 2);
  CHECK(!decode(cut, out));
  std::vector<uint8_t> badVersion = b;
  badVersion[0] = 9;
  CHECK(!decode(badVersion, out));
  std::vector<uint8_t> longTitle = b;
  longTitle[1 + 18] = 200;  // titleLen beyond the limit
  CHECK(!decode(longTitle, out));
}

}  // namespace

int main() {
  roundTrip();
  ringAndClip();
  damaged();
  return KEYRA_TEST_RESULT();
}
