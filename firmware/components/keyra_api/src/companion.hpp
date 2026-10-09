#pragma once
// Keyra Companion's decisions on the device (SPEC §9.4): which logins a page
// is offered, whether an extension may type a login into a page, and what an
// update-on-save changes. Pure, host-tested; HTTP is in handlers_agent.cpp.
#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

#include "keyra/vault.hpp"
#include "tokens.hpp"

namespace keyra::api::companion {

constexpr size_t kMaxMatches = 20;

// What POST /api/agent/match shows of a login: never its username or a secret.
struct Offer {
  uint32_t id = 0;
  std::string title;
  std::string host;       // tokens::urlHost() of the entry's URL
  bool sameUser = false;  // meaningful only when a username was asked about
};

// Entries in the token's scope whose host matches `page` (hostmatch::matches),
// in vault order, at most kMaxMatches. With `username`, sameUser is an exact
// comparison with the entry's username. Passwords are never looked at.
std::vector<Offer> match(const tokens::Token& t, const std::vector<vault::Entry>& entries, std::string_view page,
                         const std::string* username);

enum class HostCheck {
  Same,       // the login is for this page
  Elsewhere,  // another site's login, and the user chose it ("anyHost")
  Refused,    // another site's login: 409 host_mismatch
};
HostCheck checkHost(std::string_view loginUrl, std::string_view page, bool anyHost);

// POST /api/agent/save with `replace`: the press sets the stored entry's
// username (when `sent` has one) and password; the title, URL and the rest
// stay. vault::put() then moves the old password into the history (§9.3).
void applyReplace(vault::Entry& stored, const vault::Entry& sent, int64_t now);

}  // namespace keyra::api::companion
