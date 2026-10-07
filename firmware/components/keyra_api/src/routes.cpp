#include "routes.hpp"

#include <cctype>

#include "keyra/ble.hpp"

namespace keyra::api {
namespace {

bool parseId(std::string_view s, uint32_t& out) {
  if (s.empty() || s.size() > 10) return false;
  uint64_t v = 0;
  for (char c : s) {
    if (c < '0' || c > '9') return false;
    v = v * 10 + static_cast<uint64_t>(c - '0');
  }
  if (v == 0 || v > UINT32_MAX) return false;
  out = static_cast<uint32_t>(v);
  return true;
}

Match found(Route r, uint32_t id = 0) { return {Match::Kind::Found, r, id, {}}; }
Match notAllowed() { return {Match::Kind::MethodNotAllowed, Route::State, 0, {}}; }

// One path, one method: the common case.
Match only(Method m, Method want, Route r) { return m == want ? found(r) : notAllowed(); }

bool equalsIgnoreCase(std::string_view a, std::string_view b) {
  if (a.size() != b.size()) return false;
  for (size_t i = 0; i < a.size(); ++i) {
    if (std::tolower(static_cast<unsigned char>(a[i])) != std::tolower(static_cast<unsigned char>(b[i]))) return false;
  }
  return true;
}

}  // namespace

bool percentDecode(std::string_view in, std::string& out) {
  out.clear();
  out.reserve(in.size());
  auto hex = [](char c) -> int {
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;
  };
  for (size_t i = 0; i < in.size(); ++i) {
    if (in[i] != '%') {
      out.push_back(in[i]);
      continue;
    }
    if (i + 2 >= in.size()) return false;
    const int hi = hex(in[i + 1]), lo = hex(in[i + 2]);
    if (hi < 0 || lo < 0) return false;
    const char c = static_cast<char>(hi * 16 + lo);
    // An encoded separator or NUL would let one path pose as another.
    if (c == '\0' || c == '/') return false;
    out.push_back(c);
    i += 2;
  }
  return true;
}

namespace {
Match matchDecoded(Method m, std::string_view path);
}  // namespace

Match matchApi(Method m, std::string_view path) {
  if (path.find('%') == std::string_view::npos) return matchDecoded(m, path);
  std::string decoded;
  if (!percentDecode(path, decoded)) return {};
  return matchDecoded(m, decoded);
}

namespace {
Match matchDecoded(Method m, std::string_view path) {
  constexpr std::string_view kPrefix = "/api/";
  if (path.substr(0, kPrefix.size()) != kPrefix) return {};
  const std::string_view p = path.substr(kPrefix.size());

  if (p == "state") return only(m, Method::Get, Route::State);
  if (p == "setup") return only(m, Method::Post, Route::Setup);
  if (p == "unlock") return only(m, Method::Post, Route::Unlock);
  if (p == "unlock/recovery") return only(m, Method::Post, Route::UnlockRecovery);
  if (p == "recovery") {
    if (m == Method::Get) return found(Route::GetRecovery);
    if (m == Method::Post) return found(Route::CreateRecovery);
    if (m == Method::Delete) return found(Route::DeleteRecovery);
    return notAllowed();
  }
  if (p == "lock") return only(m, Method::Post, Route::Lock);
  if (p == "type") return only(m, Method::Post, Route::Type);
  if (p == "type/cancel") return only(m, Method::Post, Route::TypeCancel);
  if (p == "presence/cancel") return only(m, Method::Post, Route::PresenceCancel);
  if (p == "generate") return only(m, Method::Post, Route::Generate);
  if (p == "keyboard") return only(m, Method::Get, Route::Keyboard);
  if (p == "passphrase") return only(m, Method::Post, Route::Passphrase);
  if (p == "backup") return only(m, Method::Post, Route::Backup);
  if (p == "restore") return only(m, Method::Post, Route::Restore);
  if (p == "factory-reset") return only(m, Method::Post, Route::FactoryReset);
  if (p == "settings") {
    if (m == Method::Get) return found(Route::GetSettings);
    if (m == Method::Put) return found(Route::PutSettings);
    return notAllowed();
  }
  if (p == "entries") {
    if (m == Method::Get) return found(Route::ListEntries);
    if (m == Method::Post) return found(Route::CreateEntry);
    return notAllowed();
  }
  if (p == "entries/import") return only(m, Method::Post, Route::ImportEntries);
  if (p == "health") return only(m, Method::Get, Route::Health);
  if (p == "health/rotate") return only(m, Method::Post, Route::HealthRotate);
  if (p == "activity") return only(m, Method::Get, Route::Activity);
  if (p == "ble") return only(m, Method::Get, Route::GetBle);
  if (p == "ble/pair") return only(m, Method::Post, Route::BlePair);
  constexpr std::string_view kBonds = "ble/bonds/";
  if (p.substr(0, kBonds.size()) == kBonds) {
    Match r = found(m == Method::Put ? Route::BleSetOs : Route::BleForget);
    if (!ble::parseAddr(p.substr(kBonds.size()), r.addr)) return {};
    return m == Method::Delete || m == Method::Put ? r : notAllowed();
  }
  if (p == "fido") return only(m, Method::Get, Route::ListPasskeys);
  constexpr std::string_view kFido = "fido/";
  if (p.substr(0, kFido.size()) == kFido) {
    uint32_t id = 0;
    if (!parseId(p.substr(kFido.size()), id)) return {};
    return m == Method::Delete ? found(Route::DeletePasskey, id) : notAllowed();
  }
  if (p == "wifi/scan") return only(m, Method::Get, Route::WifiScan);
  if (p == "wifi/home") return only(m, Method::Put, Route::WifiHome);
  if (p == "trusted") return only(m, Method::Get, Route::ListTrusted);
  constexpr std::string_view kTrusted = "trusted/";
  if (p.substr(0, kTrusted.size()) == kTrusted) {
    uint32_t id = 0;
    if (!parseId(p.substr(kTrusted.size()), id)) return {};
    return m == Method::Delete ? found(Route::DeleteTrusted, id) : notAllowed();
  }

  constexpr std::string_view kEntries = "entries/";
  if (p.substr(0, kEntries.size()) != kEntries) return {};
  std::string_view rest = p.substr(kEntries.size());
  constexpr std::string_view kTotp = "/totp", kReveal = "/reveal";
  auto suffix = [&](std::string_view sfx) {
    const bool has = rest.size() > sfx.size() && rest.substr(rest.size() - sfx.size()) == sfx;
    if (has) rest.remove_suffix(sfx.size());
    return has;
  };
  const bool totp = suffix(kTotp);
  const bool reveal = !totp && suffix(kReveal);
  uint32_t id = 0;
  if (!parseId(rest, id)) return {};
  if (totp) return m == Method::Get ? found(Route::EntryTotp, id) : notAllowed();
  if (reveal) return m == Method::Post ? found(Route::RevealEntry, id) : notAllowed();
  if (m == Method::Get) return found(Route::GetEntry, id);
  if (m == Method::Put) return found(Route::UpdateEntry, id);
  if (m == Method::Delete) return found(Route::DeleteEntry, id);
  return notAllowed();
}

}  // namespace

bool needsSession(Route r) {
  return r != Route::State && r != Route::Setup && r != Route::Unlock && r != Route::UnlockRecovery &&
         r != Route::FactoryReset && r != Route::PresenceCancel;
}

bool needsCsrf(Method m, Route r) {
  // setup/unlock/factory-reset have no session yet (or a forgotten passphrase);
  // setup and factory-reset are gated by the physical button instead.
  return m != Method::Get && r != Route::Unlock && r != Route::UnlockRecovery && r != Route::Setup &&
         r != Route::FactoryReset && r != Route::PresenceCancel;
}

bool isOwnHost(std::string_view host, std::string_view homeIp) {
  if (host.empty()) return true;  // HTTP/1.0 clients: nothing to redirect on
  if (host.front() != '[') {
    const size_t colon = host.rfind(':');
    if (colon != std::string_view::npos) host = host.substr(0, colon);
  }
  if (!host.empty() && host.back() == '.') host.remove_suffix(1);
  return host == "192.168.4.1" || (!homeIp.empty() && host == homeIp) || equalsIgnoreCase(host, "keyra.local") ||
         equalsIgnoreCase(host, "keyra");
}

bool isAllowedOrigin(std::string_view origin, bool present, std::string_view homeIp) {
  if (!present) return true;  // non-browser clients and same-origin GET-style requests
  constexpr std::string_view kScheme = "http://";
  if (origin.substr(0, kScheme.size()) != kScheme) return false;  // includes "null"
  origin.remove_prefix(kScheme.size());
  return !origin.empty() && origin.find('/') == std::string_view::npos && isOwnHost(origin, homeIp);
}

std::optional<Probe> probeFor(std::string_view path) {
  static constexpr const char* kApple =
      "<HTML><HEAD><TITLE>Success</TITLE></HEAD><BODY>Success</BODY></HTML>";
  if (path == "/hotspot-detect.html" || path == "/library/test/success.html")
    return Probe{"200 OK", "text/html", kApple};
  if (path == "/generate_204" || path == "/gen_204") return Probe{"204 No Content", "text/plain", ""};
  if (path == "/connecttest.txt") return Probe{"200 OK", "text/plain", "Microsoft Connect Test"};
  if (path == "/ncsi.txt") return Probe{"200 OK", "text/plain", "Microsoft NCSI"};
  if (path == "/success.txt") return Probe{"200 OK", "text/plain", "success"};
  return std::nullopt;
}

}  // namespace keyra::api
