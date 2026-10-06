// Trusted browsers (SPEC §8.2): the store's limits, persistence and naming.
#include "keyra_test.hpp"
#include "trust.hpp"

using namespace keyra::api::trust;

namespace {

Digest digest(uint8_t seed) {
  Digest d{};
  for (size_t i = 0; i < d.size(); ++i) d[i] = static_cast<uint8_t>(seed + i);
  return d;
}

Browser browser(uint32_t id, int64_t seen) { return {id, digest(static_cast<uint8_t>(id)), "Safari on iPhone", 100, seen}; }

void gate() {
  CHECK(!needsApproval(false, false));  // Keyra's own Wi-Fi never asks
  CHECK(!needsApproval(false, true));
  CHECK(needsApproval(true, false));
  CHECK(!needsApproval(true, true));
}

void findAddRemove() {
  Store s;
  CHECK(!s.find(digest(1)).has_value());
  CHECK_EQ(s.add(browser(1, 0)), 0u);
  CHECK_EQ(s.add(browser(2, 0)), 0u);
  CHECK(s.find(digest(2)) == size_t{1});
  CHECK(!s.find(digest(3)).has_value());
  CHECK(s.remove(1));
  CHECK(!s.remove(1));
  CHECK(!s.find(digest(1)).has_value());
  CHECK(s.find(digest(2)) == size_t{0});
}

void ninthEvictsLeastRecentlySeen() {
  Store s;
  for (uint32_t id = 1; id <= kMaxBrowsers; ++id) s.add(browser(id, 1000 + id));
  s.touch(*s.find(digest(1)), 5000);  // browser 1 was just used; 2 is now the stalest
  CHECK_EQ(s.add(browser(42, 0)), 2u);
  CHECK_EQ(s.all().size(), kMaxBrowsers);
  CHECK(!s.contains(2) && s.contains(1) && s.contains(42));
}

void touchKeepsLastSeenWhenClockUnknown() {
  Store s;
  s.add(browser(5, 300));
  s.touch(0, 0);
  CHECK_EQ(s.all()[0].lastSeen, 300);
  s.touch(0, 900);
  CHECK_EQ(s.all()[0].lastSeen, 900);
}

void roundTripsAndRejectsJunk() {
  Store s;
  s.add({7, digest(7), "Chrome on Android", 1790000000, 1790000500});
  s.add({9, digest(9), "", 0, 0});
  const auto blob = s.serialize();
  const auto back = Store::parse(blob.data(), blob.size());
  CHECK(back.has_value());
  CHECK_EQ(back->all().size(), 2u);
  CHECK(back->all()[0].name == "Chrome on Android" && back->all()[0].lastSeen == 1790000500);
  CHECK(back->all()[1].id == 9 && back->find(digest(9)) == size_t{1});

  auto truncated = blob;
  truncated.pop_back();
  CHECK(!Store::parse(truncated.data(), truncated.size()).has_value());
  auto trailing = blob;
  trailing.push_back(0);
  CHECK(!Store::parse(trailing.data(), trailing.size()).has_value());
  auto badVersion = blob;
  badVersion[0] = 2;
  CHECK(!Store::parse(badVersion.data(), badVersion.size()).has_value());
  const uint8_t tooMany[] = {1, 9};
  CHECK(!Store::parse(tooMany, sizeof tooMany).has_value());
  const uint8_t empty[] = {1, 0};
  CHECK(Store::parse(empty, sizeof empty)->all().empty());
}

void names() {
  CHECK(browserName("Mozilla/5.0 (iPhone; CPU iPhone OS 18_0 like Mac OS X) AppleWebKit/605.1.15 (KHTML, like Gecko) "
                    "Version/18.0 Mobile/15E148 Safari/604.1") == "Safari on iPhone");
  CHECK(browserName("Mozilla/5.0 (Linux; Android 15; Pixel 9) AppleWebKit/537.36 (KHTML, like Gecko) "
                    "Chrome/140.0.0.0 Mobile Safari/537.36") == "Chrome on Android");
  CHECK(browserName("Mozilla/5.0 (Windows NT 10.0; Win64; x64) AppleWebKit/537.36 (KHTML, like Gecko) "
                    "Chrome/140.0.0.0 Safari/537.36 Edg/140.0.0.0") == "Edge on Windows");
  CHECK(browserName("Mozilla/5.0 (Macintosh; Intel Mac OS X 10.15; rv:131.0) Gecko/20100101 Firefox/131.0") ==
        "Firefox on Mac");
  CHECK(browserName("Mozilla/5.0 (iPhone; CPU iPhone OS 18_0 like Mac OS X) CriOS/140.0 Mobile Safari/604.1") ==
        "Chrome on iPhone");
  CHECK(browserName("curl/8.7.1") == "curl/8.7.1");
  CHECK(browserName("") == "Browser");
  const std::string junk = browserName(std::string("\x01<script>\xD8\xA8") + std::string(80, 'x'));
  CHECK(junk.size() <= kMaxName && junk.find('\x01') == std::string::npos);
}

}  // namespace

int main() {
  gate();
  findAddRemove();
  ninthEvictsLeastRecentlySeen();
  touchKeepsLastSeenWhenClockUnknown();
  roundTripsAndRejectsJunk();
  names();
  return KEYRA_TEST_RESULT();
}
