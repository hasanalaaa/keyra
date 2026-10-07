#pragma once
// Request classification (SPEC §5): API routes, captive probes, own-host check.
// Pure functions, host-tested.
#include <array>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>

namespace keyra::api {

enum class Method { Get, Post, Put, Delete, Other };

enum class Route {
  State, Setup, Unlock, Lock,
  ListEntries, CreateEntry, ImportEntries, Health, Activity, GetEntry, UpdateEntry, DeleteEntry, EntryTotp,
  Type, TypeCancel, PresenceCancel, GetSettings, PutSettings, Passphrase, Backup, Restore, FactoryReset,
  WifiScan, WifiHome, ListTrusted, DeleteTrusted,
  GetBle, BlePair, BleForget, BleSetOs,
  Generate,
  Keyboard,
  ListPasskeys, DeletePasskey,
  RevealEntry, GetRecovery, CreateRecovery, DeleteRecovery, UnlockRecovery,
};

struct Match {
  enum class Kind { Found, NotFound, MethodNotAllowed } kind = Kind::NotFound;
  Route route = Route::State;
  uint32_t id = 0;  // entry, trusted-browser and passkey routes
  std::array<uint8_t, 6> addr{};  // BleForget/BleSetOs: the bond's address
};

// `path` is the URI path without query string, e.g. "/api/entries/42/totp".
// Percent-escapes are decoded first (browsers may send "A4%3AC1…" for a bond
// address); a malformed escape or an encoded NUL or '/' is NotFound.
Match matchApi(Method m, std::string_view path);
// "%3A" → ':'. False on a malformed escape or a decoded NUL or '/'.
bool percentDecode(std::string_view in, std::string& out);
bool needsSession(Route r);
bool needsCsrf(Method m, Route r);

// Host header names that address the device itself (AP IP, mDNS name, or the
// current home-network IP when joined; any port). Anything else is refused so a
// DNS-rebinding page can't reach the API under a foreign name.
bool isOwnHost(std::string_view host, std::string_view homeIp = {});
// Origin header of a state-changing request: absent, or this device over http.
// Blocks cross-site form posts to the CSRF-exempt routes (setup/unlock/reset).
bool isAllowedOrigin(std::string_view origin, bool present, std::string_view homeIp = {});

struct Probe {
  const char* status;
  const char* contentType;
  const char* body;
};
// OS connectivity checks answered as "online" so no captive sheet appears.
std::optional<Probe> probeFor(std::string_view path);

}  // namespace keyra::api
