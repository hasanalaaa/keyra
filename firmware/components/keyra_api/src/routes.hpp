#pragma once
// Request classification (SPEC §5): API routes, captive probes, own-host check.
// Pure functions, host-tested.
#include <array>
#include <cstdint>
#include <optional>
#include <string_view>

namespace keyra::api {

enum class Method { Get, Post, Put, Delete, Other };

enum class Route {
  State, Setup, Unlock, Lock,
  ListEntries, CreateEntry, ImportEntries, GetEntry, UpdateEntry, DeleteEntry, EntryTotp,
  Type, TypeCancel, GetSettings, PutSettings, Passphrase, Backup, Restore, FactoryReset,
  WifiScan, WifiHome, ListTrusted, DeleteTrusted,
  GetBle, BlePair, BleForget,
  Generate,
  RevealEntry, GetRecovery, CreateRecovery, DeleteRecovery, UnlockRecovery,
};

struct Match {
  enum class Kind { Found, NotFound, MethodNotAllowed } kind = Kind::NotFound;
  Route route = Route::State;
  uint32_t id = 0;  // entry and trusted-browser routes
  std::array<uint8_t, 6> addr{};  // BleForget: the bond's address
};

// `path` is the URI path without query string, e.g. "/api/entries/42/totp".
Match matchApi(Method m, std::string_view path);
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
