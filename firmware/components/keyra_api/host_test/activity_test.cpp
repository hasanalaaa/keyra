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

// SPEC §17: a token listing every few seconds is counted on one line.
void coalesced() {
  std::vector<Event> log;
  append(log, ev(Kind::Unlock));
  Event l = ev(Kind::AgentListed, 7, "Claude");
  append(log, l);
  l.at += 30;
  append(log, l);
  append(log, l);
  CHECK(log.size() == 2 && log.back().n == 3 && log.back().at == 1790000030);
  append(log, ev(Kind::AgentListed, 8, "Other"));  // another token: a new line
  l.at += 30;
  append(log, l);  // still among the newest: counted, and moved to the end
  CHECK(log.size() == 3 && log.back().id == 7 && log.back().n == 4 && log.back().at == 1790000060);
  CHECK(log[1].id == 8);
  append(log, ev(Kind::AgentArmed, 5, "Claude"));  // never coalesced
  append(log, ev(Kind::AgentArmed, 5, "Claude"));
  CHECK(log.size() == 5);
  // A refusal for another reason is its own line.
  Event r = ev(Kind::TagRefused, 3, "Desk");
  append(log, r);
  r.detail = 1;
  append(log, r);
  CHECK(log.size() == 7 && log.back().detail == 1 && log.back().n == 1);
  CHECK(std::string(kindName(Kind::TokenCreated)) == "token_created");
  CHECK(std::string(kindName(Kind::TagCreated)) == "tag_created" && std::string(kindName(Kind::TagTapped)) == "tag_tapped");
  CHECK(std::string(kindName(Kind::TagRevoked)) == "tag_revoked" && std::string(kindName(Kind::TagRefused)) == "tag_refused");
  CHECK(std::string(kindName(Kind::AgentGenerated)) == "agent_generated");
  std::vector<Event> back;
  CHECK(decode(encode(log), back) && back.size() == 7 && back[2].n == 4 && back[3].kind == Kind::AgentArmed);
}

bool has(const std::vector<Event>& log, Kind k) {
  for (const Event& e : log)
    if (e.kind == k) return true;
  return false;
}

// What a stolen session, a token or a tag's URL can repeat without a press
// cannot push the events that matter out of the 200 (the audit's PoC: 200
// rotate on/off, or a token alternating list and generate, emptied the log).
void flood() {
  const auto start = [] {
    std::vector<Event> log;
    append(log, ev(Kind::Unlock));
    append(log, ev(Kind::TokenCreated, 0, "app"));
    append(log, ev(Kind::Revealed, 42, "Bank"));
    return log;
  };
  // Rotate on/off: two lines in all.
  std::vector<Event> log = start();
  for (int i = 0; i < 500; ++i) append(log, ev(i % 2 ? Kind::RotateEnded : Kind::RotateStarted));
  CHECK(log.size() == 5 && has(log, Kind::Revealed) && has(log, Kind::Unlock) && has(log, Kind::TokenCreated));
  CHECK(log.back().n == 250);
  // A token alternating list and generate.
  log = start();
  for (int i = 0; i < 500; ++i) append(log, ev(i % 2 ? Kind::AgentGenerated : Kind::AgentListed, 7, "app"));
  CHECK(log.size() == 5 && has(log, Kind::Revealed));
  // More distinct lines than the window: arming 300 different accounts and
  // refusals from 50 tags. Noise is capped, the rest stays.
  log = start();
  for (uint32_t i = 0; i < 300; ++i) append(log, ev(Kind::AgentArmed, i + 1, "app"));
  for (uint32_t i = 0; i < 50; ++i) append(log, ev(Kind::TagRefused, i + 1, "tag"));
  CHECK(has(log, Kind::Revealed) && has(log, Kind::Unlock) && has(log, Kind::TokenCreated));
  size_t noise = 0;
  for (const Event& e : log) noise += isNoise(e.kind) ? 1 : 0;
  CHECK(noise == kMaxNoise && log.size() == 3 + kMaxNoise);
  CHECK(log.back().kind == Kind::TagRefused && log.back().id == 50);  // the newest noise is kept
  // A full log of real events still drops its oldest real event, noise first.
  log = start();
  append(log, ev(Kind::AgentArmed, 1, "app"));
  for (uint32_t i = 0; i < kMaxEvents; ++i) append(log, ev(Kind::Typed, i + 1));
  CHECK(log.size() == kMaxEvents && !has(log, Kind::Unlock) && !has(log, Kind::AgentArmed));
  CHECK(log.front().kind == Kind::Typed && log.front().id == 1);
  // dropOldest: noise first, then the oldest.
  std::vector<Event> two = {ev(Kind::Unlock), ev(Kind::AgentListed, 1, "a")};
  CHECK(dropOldest(two) && two.size() == 1 && two[0].kind == Kind::Unlock);
  CHECK(dropOldest(two) && two.empty() && !dropOldest(two));
}

// An extension's typing for another site keeps the token's name and the page's
// host in the one title the record has; the host is never the part cut off.
void armedElsewhere() {
  const std::string arrow = " \xE2\x86\x92 ";
  CHECK(elsewhereTitle("Chrome", "evil.example") == "Chrome" + arrow + "evil.example");
  const std::string longName(48, 'n');
  const std::string t = elsewhereTitle(longName, "accounts.example.com");
  CHECK(t.size() == kMaxTitle && t.substr(t.size() - 20) == "accounts.example.com");
  CHECK(t.substr(0, 39) == std::string(39, 'n'));
  // A name cut on a UTF-8 boundary, never mid-character.
  std::string arabic;
  for (int i = 0; i < 24; ++i) arabic += "\xD9\x88";  // 48 bytes of و
  const std::string a = elsewhereTitle(arabic, "x.example.com");  // room 64 - 5 - 13 = 46
  CHECK(a == arabic.substr(0, 46) + arrow + "x.example.com");
  const std::string b = elsewhereTitle(arabic, "xy.example.com");  // room 45 → 44
  CHECK(b == arabic.substr(0, 44) + arrow + "xy.example.com");
  // A host too long for both: the host alone, clipped like any title.
  CHECK(elsewhereTitle("Chrome", std::string(70, 'h')) == std::string(kMaxTitle, 'h'));
  CHECK(elsewhereTitle("Chrome", std::string(59, 'h')) == std::string(59, 'h'));
  CHECK(elsewhereTitle("Chrome", std::string(58, 'h')) == "C" + arrow + std::string(58, 'h'));
  CHECK(kArmedElsewhere > 3);  // never collides with a "what" detail
}

}  // namespace

int main() {
  coalesced();
  flood();
  armedElsewhere();
  roundTrip();
  ringAndClip();
  damaged();
  return KEYRA_TEST_RESULT();
}
