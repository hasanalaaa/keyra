#include "scan_list.hpp"

#include <algorithm>

namespace keyra::net::scanlist {
namespace {

bool printable(const std::string& s) {
  return !s.empty() && std::none_of(s.begin(), s.end(), [](char c) {
    const auto u = static_cast<unsigned char>(c);
    return u < 0x20 || u == 0x7F;
  });
}

}  // namespace

std::vector<Item> tidy(const std::vector<Record>& raw, size_t max) {
  std::vector<Item> out;
  for (const Record& r : raw) {
    if (!printable(r.ssid) || r.security == Security::Other) continue;
    const auto same = std::find_if(out.begin(), out.end(), [&](const Item& i) { return i.ssid == r.ssid; });
    const Item item{r.ssid, r.rssi, r.security == Security::Psk, r.channel};
    if (same == out.end()) {
      out.push_back(item);
    } else if (r.rssi > same->rssi) {
      *same = item;
    }
  }
  std::stable_sort(out.begin(), out.end(), [](const Item& a, const Item& b) { return a.rssi > b.rssi; });
  if (out.size() > max) out.resize(max);
  return out;
}

}  // namespace keyra::net::scanlist
