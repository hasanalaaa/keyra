#include "host_os.hpp"

namespace keyra::api::hostos {
namespace {

constexpr Os kAll[] = {Os::Mac, Os::Ios, Os::Windows, Os::Android, Os::Linux};

// Calls fn(addrText, osText) for each "addr=os" item.
template <typename Fn>
void each(std::string_view table, Fn fn) {
  while (!table.empty()) {
    const size_t end = table.find(';');
    const std::string_view item = table.substr(0, end);
    table = end == std::string_view::npos ? std::string_view{} : table.substr(end + 1);
    const size_t eq = item.find('=');
    if (eq != std::string_view::npos) fn(item.substr(0, eq), item.substr(eq + 1));
  }
}

}  // namespace

const char* name(Os os) {
  switch (os) {
    case Os::Unknown: return "";
    case Os::Mac: return "mac";
    case Os::Ios: return "ios";
    case Os::Windows: return "windows";
    case Os::Android: return "android";
    case Os::Linux: return "linux";
  }
  return "";
}

bool parse(std::string_view s, Os& out) {
  if (s.empty()) {
    out = Os::Unknown;
    return true;
  }
  for (Os os : kAll) {
    if (s == name(os)) {
      out = os;
      return true;
    }
  }
  return false;
}

Os get(std::string_view table, const ble::Addr& addr) {
  Os found = Os::Unknown;
  each(table, [&](std::string_view a, std::string_view o) {
    ble::Addr parsed;
    Os os;
    if (ble::parseAddr(a, parsed) && parsed == addr && parse(o, os)) found = os;
  });
  return found;
}

std::string set(std::string_view table, const ble::Addr& addr, Os os) {
  std::string out;
  each(table, [&](std::string_view a, std::string_view o) {
    ble::Addr parsed;
    Os other;
    // Drops this bond's old item and anything unreadable.
    if (!ble::parseAddr(a, parsed) || parsed == addr || !parse(o, other) || other == Os::Unknown) return;
    out.append(a).append("=").append(o).append(";");
  });
  if (os != Os::Unknown) out.append(ble::formatAddr(addr)).append("=").append(name(os)).append(";");
  if (!out.empty()) out.pop_back();
  return out;
}

}  // namespace keyra::api::hostos
