#include "host_match.hpp"

#include <algorithm>
#include <array>
#include <vector>

namespace keyra::api::hostmatch {
namespace {

std::vector<std::string_view> labels(std::string_view host) {
  std::vector<std::string_view> out;
  size_t start = 0;
  for (;;) {
    const size_t dot = host.find('.', start);
    out.push_back(host.substr(start, dot == std::string_view::npos ? std::string_view::npos : dot - start));
    if (dot == std::string_view::npos) return out;
    start = dot + 1;
  }
}

bool allDigits(std::string_view s) {
  return !s.empty() && std::all_of(s.begin(), s.end(), [](char c) { return c >= '0' && c <= '9'; });
}

bool isIp(std::string_view host) {
  if (host.find(':') != std::string_view::npos) return true;  // IPv6
  const auto l = labels(host);
  return l.size() == 4 && std::all_of(l.begin(), l.end(), allDigits);
}

// Without a public-suffix list, a host owns its subdomains only when it is
// plainly a site: two or more labels, and not a country's second level
// ("co.uk", "gov.iq", "com.au") that many unrelated owners share.
bool siteLike(std::string_view host) {
  static constexpr std::array<std::string_view, 13> kSecondLevels = {
      "ac", "co", "com", "edu", "gob", "gov", "go", "mil", "ne", "net", "or", "org", "sch"};
  const auto l = labels(host);
  if (l.size() < 2 || std::any_of(l.begin(), l.end(), [](std::string_view s) { return s.empty(); })) return false;
  if (l.size() > 2) return true;
  const std::string_view tld = l[1];
  const bool country =
      tld.size() == 2 && std::all_of(tld.begin(), tld.end(), [](char c) { return c >= 'a' && c <= 'z'; });
  return !(country && std::find(kSecondLevels.begin(), kSecondLevels.end(), l[0]) != kSecondLevels.end());
}

}  // namespace

std::string normalize(std::string_view host) {
  while (!host.empty() && host.front() == ' ') host.remove_prefix(1);
  while (!host.empty() && host.back() == ' ') host.remove_suffix(1);
  if (!host.empty() && host.back() == '.') host.remove_suffix(1);
  std::string out;
  out.reserve(host.size());
  for (char c : host) out.push_back(c >= 'A' && c <= 'Z' ? static_cast<char>(c - 'A' + 'a') : c);
  if (out.compare(0, 4, "www.") == 0) out.erase(0, 4);
  return out;
}

bool matches(std::string_view login, std::string_view page) {
  const std::string a = normalize(login), b = normalize(page);
  if (a.empty() || b.empty()) return false;
  if (a == b) return true;
  if (isIp(a) || isIp(b)) return false;
  const std::string& shorter = a.size() < b.size() ? a : b;
  const std::string& longer = a.size() < b.size() ? b : a;
  if (shorter.size() == longer.size() || !siteLike(shorter)) return false;
  const size_t at = longer.size() - shorter.size();
  return longer[at - 1] == '.' && longer.compare(at, shorter.size(), shorter) == 0;
}

bool validHost(std::string_view host) {
  while (!host.empty() && host.front() == ' ') host.remove_prefix(1);
  while (!host.empty() && host.back() == ' ') host.remove_suffix(1);
  if (host.empty() || host.size() > kMaxHost) return false;
  return std::all_of(host.begin(), host.end(), [](char c) {
    const auto u = static_cast<unsigned char>(c);
    return u > 0x20 && u < 0x7f;
  });
}

}  // namespace keyra::api::hostmatch
