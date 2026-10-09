// Keyra Companion on the device (SPEC §9.4): which logins a page is offered,
// the host binding on typing, and save-as-update through the real vault core
// (the old password lands in the history like any edit).
#include <string>
#include <vector>

#include "companion.hpp"
#include "keyra_test.hpp"
#include "rig.hpp"

using namespace keyra::api;
using companion::HostCheck;

namespace {

keyra::vault::Entry login(uint32_t id, const std::string& title, const std::string& url, const std::string& user,
                          const std::string& password = "s3cret") {
  keyra::vault::Entry e;
  e.id = id;
  e.title = title;
  e.url = url;
  e.username = user;
  e.password = password;
  return e;
}

tokens::Token extension(bool all = true, std::vector<uint32_t> scope = {}) {
  tokens::Token t;
  t.id = 7;
  t.kind = tokens::Kind::Extension;
  t.name = "Chrome";
  t.all = all;
  t.scope = std::move(scope);
  return t;
}

std::vector<uint32_t> ids(const std::vector<companion::Offer>& offers) {
  std::vector<uint32_t> out;
  for (const auto& o : offers) out.push_back(o.id);
  return out;
}

void matching() {
  const std::vector<keyra::vault::Entry> vault = {
      login(1, "GitHub", "https://github.com/login", "hasan", "hunter2"),
      login(2, "Gist", "https://gist.github.com", "other"),
      login(3, "GitLab", "https://gitlab.com", "hasan"),
      login(4, "No URL", "", "hasan"),
      login(5, "Look-alike", "https://github.com.evil.example", "hasan"),
      login(6, "Shared host", "https://alice.github.io", "hasan"),
  };
  auto offers = companion::match(extension(), vault, "github.com", nullptr);
  CHECK(ids(offers) == std::vector<uint32_t>({1, 2}));
  CHECK(offers[0].title == "GitHub" && offers[0].host == "github.com" && !offers[0].sameUser);
  CHECK(offers[1].host == "gist.github.com");
  CHECK(ids(companion::match(extension(), vault, "WWW.GitHub.com.", nullptr)) == std::vector<uint32_t>({1, 2}));
  CHECK(ids(companion::match(extension(), vault, "gist.github.com", nullptr)) == std::vector<uint32_t>({1, 2}));
  CHECK(ids(companion::match(extension(), vault, "mallory.github.io", nullptr)).empty());
  CHECK(ids(companion::match(extension(), vault, "other.example", nullptr)).empty());
  // The owner of evil.example owns its subdomains, even one named like GitHub.
  CHECK(ids(companion::match(extension(), vault, "evil.example", nullptr)) == std::vector<uint32_t>({5}));

  // sameUser: an exact comparison of usernames, only when one was asked about.
  const std::string hasan = "hasan", other = "Hasan", pw = "hunter2";
  offers = companion::match(extension(), vault, "github.com", &hasan);
  CHECK(offers.size() == 2 && offers[0].sameUser && !offers[1].sameUser);
  offers = companion::match(extension(), vault, "github.com", &other);
  CHECK(offers.size() == 2 && !offers[0].sameUser);
  // Never a password oracle: a "username" equal to the stored password says nothing.
  offers = companion::match(extension(), vault, "github.com", &pw);
  CHECK(offers.size() == 2 && !offers[0].sameUser && !offers[1].sameUser);

  // The scope bounds what is offered, like every token listing.
  CHECK(ids(companion::match(extension(false, {2, 3}), vault, "github.com", nullptr)) == std::vector<uint32_t>({2}));
  CHECK(ids(companion::match(extension(false, {4}), vault, "github.com", nullptr)).empty());

  // At most 20, in vault order.
  std::vector<keyra::vault::Entry> many;
  for (uint32_t id = 1; id <= 30; ++id) many.push_back(login(id, "Shop", "https://shop.example.com", "u"));
  offers = companion::match(extension(), many, "example.com", nullptr);
  CHECK(offers.size() == companion::kMaxMatches && offers.front().id == 1 && offers.back().id == 20);
  CHECK(companion::match(extension(), {}, "example.com", nullptr).empty());
}

void hostBinding() {
  CHECK(companion::checkHost("https://github.com/login", "github.com", false) == HostCheck::Same);
  CHECK(companion::checkHost("https://github.com/login", "gist.github.com", false) == HostCheck::Same);
  CHECK(companion::checkHost("https://github.com", "evil.example", false) == HostCheck::Refused);
  CHECK(companion::checkHost("https://github.com", "evil.example", true) == HostCheck::Elsewhere);
  CHECK(companion::checkHost("https://github.com", "github.com", true) == HostCheck::Same);
  CHECK(companion::checkHost("", "github.com", false) == HostCheck::Refused);  // a login without a URL
  CHECK(companion::checkHost("", "github.com", true) == HostCheck::Elsewhere);
  CHECK(companion::checkHost("https://alice.github.io", "mallory.github.io", false) == HostCheck::Refused);
}

void saveAsUpdate() {
  using keyra::vault::Status;
  auto rig = keyra::vault::test::Rig::ready();
  keyra::vault::Entry e = keyra::vault::test::sample("GitHub", "hasan", "https://github.com");
  e.password = "old-password";
  CHECK((*rig)->put(e) == Status::Ok);
  const uint32_t id = e.id;

  // What the extension sent: a new password, no username; title and URL are ignored.
  keyra::vault::Entry sent = login(0, "Ignored title", "https://ignored.example", "", "new-password");
  keyra::vault::Entry stored;
  CHECK((*rig)->get(id, stored) == Status::Ok);
  companion::applyReplace(stored, sent, 1800000000);
  CHECK((*rig)->put(stored) == Status::Ok);

  keyra::vault::Entry got;
  CHECK((*rig)->get(id, got) == Status::Ok);
  CHECK(got.password == "new-password" && got.username == "hasan");
  CHECK(got.title == "GitHub" && got.url == "https://github.com" && got.notes == "notes for GitHub");
  CHECK(got.updated == 1800000000);
  CHECK(got.history.size() == 1 && got.history[0].password == "old-password" &&
        got.history[0].changedAt == 1800000000);

  // With a username, that changes too; the previous password moves down the history.
  sent = login(0, "", "", "hasan@new.example", "newer-password");
  CHECK((*rig)->get(id, stored) == Status::Ok);
  companion::applyReplace(stored, sent, 1800000100);
  CHECK((*rig)->put(stored) == Status::Ok);
  CHECK((*rig)->get(id, got) == Status::Ok);
  CHECK(got.username == "hasan@new.example" && got.password == "newer-password");
  CHECK(got.history.size() == 2 && got.history[0].password == "new-password" &&
        got.history[1].password == "old-password");
}

}  // namespace

int main() {
  matching();
  hostBinding();
  saveAsUpdate();
  return KEYRA_TEST_RESULT();
}
