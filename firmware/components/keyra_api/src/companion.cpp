#include "companion.hpp"

#include "host_match.hpp"

namespace keyra::api::companion {

std::vector<Offer> match(const tokens::Token& t, const std::vector<vault::Entry>& entries, std::string_view page,
                         const std::string* username) {
  std::vector<Offer> out;
  for (const vault::Entry& e : entries) {
    if (out.size() >= kMaxMatches) break;
    if (!tokens::inScope(t, e.id)) continue;
    std::string host = tokens::urlHost(e.url);
    if (!hostmatch::matches(host, page)) continue;
    out.push_back({e.id, e.title, std::move(host), username != nullptr && e.username == *username});
  }
  return out;
}

HostCheck checkHost(std::string_view loginUrl, std::string_view page, bool anyHost) {
  if (hostmatch::matches(tokens::urlHost(loginUrl), page)) return HostCheck::Same;
  return anyHost ? HostCheck::Elsewhere : HostCheck::Refused;
}

void applyReplace(vault::Entry& stored, const vault::Entry& sent, int64_t now) {
  if (!sent.username.empty()) stored.username = sent.username;
  stored.password = sent.password;
  stored.updated = now;
}

}  // namespace keyra::api::companion
