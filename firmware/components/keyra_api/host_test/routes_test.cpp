#include <array>
#include <string>

#include "clock.hpp"
#include "keyra_test.hpp"
#include "routes.hpp"
#include "validate.hpp"

using namespace keyra::api;
using K = Match::Kind;

namespace {

bool is(Match m, Route r, uint32_t id = 0) { return m.kind == K::Found && m.route == r && m.id == id; }

void apiRoutes() {
  CHECK(is(matchApi(Method::Get, "/api/state"), Route::State));
  CHECK(is(matchApi(Method::Post, "/api/setup"), Route::Setup));
  CHECK(is(matchApi(Method::Post, "/api/unlock"), Route::Unlock));
  CHECK(is(matchApi(Method::Post, "/api/lock"), Route::Lock));
  CHECK(is(matchApi(Method::Get, "/api/entries"), Route::ListEntries));
  CHECK(is(matchApi(Method::Post, "/api/entries"), Route::CreateEntry));
  CHECK(is(matchApi(Method::Post, "/api/entries/import"), Route::ImportEntries));
  CHECK(is(matchApi(Method::Get, "/api/entries/42"), Route::GetEntry, 42));
  CHECK(is(matchApi(Method::Put, "/api/entries/4294967295"), Route::UpdateEntry, 4294967295u));
  CHECK(is(matchApi(Method::Delete, "/api/entries/1"), Route::DeleteEntry, 1));
  CHECK(is(matchApi(Method::Get, "/api/entries/9/totp"), Route::EntryTotp, 9));
  CHECK(is(matchApi(Method::Post, "/api/type"), Route::Type));
  CHECK(is(matchApi(Method::Post, "/api/generate"), Route::Generate));
  CHECK(matchApi(Method::Get, "/api/generate").kind == Match::Kind::MethodNotAllowed);
  CHECK(needsSession(Route::Generate, true) && needsCsrf(Method::Post, Route::Generate, true));
  CHECK(is(matchApi(Method::Get, "/api/keyboard"), Route::Keyboard));
  CHECK(matchApi(Method::Put, "/api/keyboard").kind == Match::Kind::MethodNotAllowed);
  CHECK(needsSession(Route::Keyboard, true));
  CHECK(is(matchApi(Method::Post, "/api/type/cancel"), Route::TypeCancel));
  CHECK(is(matchApi(Method::Get, "/api/settings"), Route::GetSettings));
  CHECK(is(matchApi(Method::Put, "/api/settings"), Route::PutSettings));
  CHECK(is(matchApi(Method::Post, "/api/passphrase"), Route::Passphrase));
  CHECK(is(matchApi(Method::Post, "/api/backup"), Route::Backup));
  CHECK(is(matchApi(Method::Post, "/api/restore"), Route::Restore));
  CHECK(is(matchApi(Method::Post, "/api/factory-reset"), Route::FactoryReset));
  CHECK(is(matchApi(Method::Get, "/api/wifi/scan"), Route::WifiScan));
  CHECK(is(matchApi(Method::Put, "/api/wifi/home"), Route::WifiHome));
  CHECK(is(matchApi(Method::Get, "/api/trusted"), Route::ListTrusted));
  CHECK(is(matchApi(Method::Delete, "/api/trusted/77"), Route::DeleteTrusted, 77));
  CHECK(matchApi(Method::Post, "/api/wifi/scan").kind == K::MethodNotAllowed);
  CHECK(matchApi(Method::Get, "/api/wifi/home").kind == K::MethodNotAllowed);
  CHECK(matchApi(Method::Get, "/api/trusted/77").kind == K::MethodNotAllowed);
  CHECK(matchApi(Method::Delete, "/api/trusted/0").kind == K::NotFound);
  CHECK(matchApi(Method::Delete, "/api/trusted/x").kind == K::NotFound);
  CHECK(is(matchApi(Method::Get, "/api/fido"), Route::ListPasskeys));
  CHECK(is(matchApi(Method::Delete, "/api/fido/4294967295"), Route::DeletePasskey, 4294967295u));
  CHECK(matchApi(Method::Post, "/api/fido").kind == K::MethodNotAllowed);
  CHECK(matchApi(Method::Get, "/api/fido/5").kind == K::MethodNotAllowed);
  CHECK(matchApi(Method::Delete, "/api/fido/0").kind == K::NotFound);
  CHECK(matchApi(Method::Delete, "/api/fido/4294967296").kind == K::NotFound);
  CHECK(needsSession(Route::ListPasskeys, true) && needsSession(Route::DeletePasskey, true));
  CHECK(needsCsrf(Method::Delete, Route::DeletePasskey, true));

  // X-Keyra-Time sets the clock only with a live session (setup and unlock
  // adopt it in their handlers, once the vault is empty or the passphrase right).
  CHECK(adoptsClientClock(Route::ListEntries, true) && adoptsClientClock(Route::State, true));
  CHECK(!adoptsClientClock(Route::State, false) && !adoptsClientClock(Route::Unlock, false));
  CHECK(!adoptsClientClock(Route::UnlockRecovery, false) && !adoptsClientClock(Route::FactoryReset, false));
  CHECK(!adoptsClientClock(Route::Setup, false));
  CHECK(!adoptsClientClock(Route::AgentType, false) && !adoptsClientClock(Route::AgentEntries, true));
  CHECK(!adoptsClientClock(Route::TagTap, false) && !adoptsClientClock(Route::TagStatus, true));

  CHECK(is(matchApi(Method::Get, "/api/health"), Route::Health));
  CHECK(matchApi(Method::Post, "/api/health").kind == K::MethodNotAllowed);
  CHECK(needsSession(Route::Health, true));
  CHECK(is(matchApi(Method::Post, "/api/health/rotate"), Route::HealthRotate));
  CHECK(is(matchApi(Method::Post, "/api/health/reveal"), Route::HealthReveal));
  CHECK(matchApi(Method::Get, "/api/health/reveal").kind == K::MethodNotAllowed);
  CHECK(needsSession(Route::HealthReveal, true) && needsCsrf(Method::Post, Route::HealthReveal, true));
  CHECK(needsCsrf(Method::Post, Route::HealthRotate, true));
  CHECK(is(matchApi(Method::Get, "/api/activity"), Route::Activity));
  CHECK(matchApi(Method::Delete, "/api/activity").kind == K::MethodNotAllowed);  // the log cannot be wiped
  CHECK(needsSession(Route::Activity, true));
  CHECK(is(matchApi(Method::Post, "/api/update"), Route::Update));
  CHECK(matchApi(Method::Get, "/api/update").kind == K::MethodNotAllowed);
  CHECK(needsSession(Route::Update, true) && needsCsrf(Method::Post, Route::Update, true));
  CHECK(is(matchApi(Method::Post, "/api/update/check"), Route::UpdateCheck));
  CHECK(is(matchApi(Method::Post, "/api/update/download"), Route::UpdateDownload));
  CHECK(is(matchApi(Method::Post, "/api/update/apply"), Route::UpdateApply));
  CHECK(matchApi(Method::Get, "/api/update/apply").kind == K::MethodNotAllowed);
  CHECK(needsCsrf(Method::Post, Route::UpdateApply, true) && needsSession(Route::UpdateDownload, true));

  CHECK(is(matchApi(Method::Get, "/api/ble"), Route::GetBle));
  CHECK(is(matchApi(Method::Post, "/api/ble/pair"), Route::BlePair));
  const Match fg = matchApi(Method::Delete, "/api/ble/bonds/a4:c1:38:0B:7F:3A");
  CHECK(is(fg, Route::BleForget));
  CHECK(fg.addr == (std::array<uint8_t, 6>{0xA4, 0xC1, 0x38, 0x0B, 0x7F, 0x3A}));
  const Match os = matchApi(Method::Put, "/api/ble/bonds/A4:C1:38:0B:7F:3A");
  CHECK(is(os, Route::BleSetOs));
  CHECK(os.addr == fg.addr);
  CHECK(matchApi(Method::Put, "/api/ble/bonds/nope").kind == K::NotFound);
  // Browsers may percent-encode the colons (encodeURIComponent): same route.
  const Match enc = matchApi(Method::Put, "/api/ble/bonds/A4%3AC1%3A38%3A0B%3A7F%3A3A");
  CHECK(is(enc, Route::BleSetOs) && enc.addr == fg.addr);
  CHECK(is(matchApi(Method::Delete, "/api/ble/bonds/a4%3ac1%3a38%3a0b%3a7f%3a3a"), Route::BleForget));
  CHECK(is(matchApi(Method::Get, "/api/%73tate"), Route::State));
  // Malformed escapes, an encoded '/' or NUL never match anything.
  CHECK(matchApi(Method::Get, "/api/state%").kind == K::NotFound);
  CHECK(matchApi(Method::Get, "/api/state%4").kind == K::NotFound);
  CHECK(matchApi(Method::Get, "/api/state%zz").kind == K::NotFound);
  CHECK(matchApi(Method::Delete, "/api/ble%2Fbonds%2FA4:C1:38:0B:7F:3A").kind == K::NotFound);
  CHECK(matchApi(Method::Get, "/api/state%00").kind == K::NotFound);
  std::string out;
  CHECK(percentDecode("a%20b%3A", out) && out == "a b:");
  CHECK(!percentDecode("%2f", out));
  CHECK(matchApi(Method::Get, "/api/ble/bonds/A4:C1:38:0B:7F:3A").kind == K::MethodNotAllowed);
  CHECK(matchApi(Method::Delete, "/api/ble/bonds/A4:C1:38:0B:7F").kind == K::NotFound);
  CHECK(matchApi(Method::Delete, "/api/ble/bonds/").kind == K::NotFound);
  CHECK(matchApi(Method::Post, "/api/ble").kind == K::MethodNotAllowed);
  CHECK(matchApi(Method::Get, "/api/ble/pair").kind == K::MethodNotAllowed);

  // SPEC §12: reveal grace and the recovery key.
  CHECK(is(matchApi(Method::Post, "/api/entries/42/reveal"), Route::RevealEntry, 42));
  CHECK(matchApi(Method::Get, "/api/entries/42/reveal").kind == K::MethodNotAllowed);
  CHECK(matchApi(Method::Post, "/api/entries/0/reveal").kind == K::NotFound);
  CHECK(matchApi(Method::Post, "/api/entries/1/totp/reveal").kind == K::NotFound);
  CHECK(is(matchApi(Method::Get, "/api/recovery"), Route::GetRecovery));
  CHECK(is(matchApi(Method::Post, "/api/recovery"), Route::CreateRecovery));
  CHECK(is(matchApi(Method::Delete, "/api/recovery"), Route::DeleteRecovery));
  CHECK(matchApi(Method::Put, "/api/recovery").kind == K::MethodNotAllowed);
  CHECK(is(matchApi(Method::Post, "/api/unlock/recovery"), Route::UnlockRecovery));
  CHECK(!needsSession(Route::UnlockRecovery, true) && !needsCsrf(Method::Post, Route::UnlockRecovery, true));
  CHECK(needsSession(Route::RevealEntry, true) && needsCsrf(Method::Post, Route::RevealEntry, true));
  CHECK(needsSession(Route::GetRecovery, true) && needsCsrf(Method::Post, Route::CreateRecovery, true));
  CHECK(needsCsrf(Method::Delete, Route::DeleteRecovery, true));

  CHECK(matchApi(Method::Get, "/api/lock").kind == K::MethodNotAllowed);
  CHECK(matchApi(Method::Get, "/api/entries/import").kind == K::MethodNotAllowed);
  CHECK(matchApi(Method::Post, "/api/entries/3").kind == K::MethodNotAllowed);
  CHECK(matchApi(Method::Delete, "/api/entries/3/totp").kind == K::MethodNotAllowed);
  CHECK(matchApi(Method::Get, "/api/entries/0").kind == K::NotFound);
  CHECK(matchApi(Method::Get, "/api/entries/4294967296").kind == K::NotFound);
  CHECK(matchApi(Method::Get, "/api/entries/12a").kind == K::NotFound);
  CHECK(matchApi(Method::Get, "/api/entries/").kind == K::NotFound);
  CHECK(matchApi(Method::Get, "/api/entries//totp").kind == K::NotFound);
  CHECK(matchApi(Method::Get, "/api/state/").kind == K::NotFound);
  CHECK(matchApi(Method::Get, "/api").kind == K::NotFound);
  CHECK(matchApi(Method::Get, "/index.html").kind == K::NotFound);
}

void policy() {
  CHECK(!needsSession(Route::State, true));
  CHECK(!needsSession(Route::Setup, true));
  CHECK(!needsSession(Route::Unlock, true));
  // F11: a forgotten passphrase (locked) needs no session; an open vault does, with CSRF.
  CHECK(!needsSession(Route::FactoryReset, false) && !needsCsrf(Method::Post, Route::FactoryReset, false));
  CHECK(needsSession(Route::FactoryReset, true) && needsCsrf(Method::Post, Route::FactoryReset, true));
  CHECK(!needsSession(Route::Unlock, false) && needsSession(Route::Lock, false));
  CHECK(needsSession(Route::Lock, true));
  CHECK(needsSession(Route::GetEntry, true));
  CHECK(needsSession(Route::TypeCancel, true));
  CHECK(!needsCsrf(Method::Get, Route::ListEntries, true));
  CHECK(!needsCsrf(Method::Post, Route::Unlock, true));
  CHECK(!needsCsrf(Method::Post, Route::Setup, true));
  CHECK(needsCsrf(Method::Post, Route::Lock, true));
  CHECK(needsCsrf(Method::Delete, Route::DeleteEntry, true));
  CHECK(needsCsrf(Method::Put, Route::PutSettings, true));
  CHECK(needsSession(Route::GetBle, true) && needsSession(Route::BlePair, true) && needsSession(Route::BleForget, true));
  CHECK(needsCsrf(Method::Post, Route::BlePair, true));
  CHECK(needsCsrf(Method::Delete, Route::BleForget, true));
  CHECK(needsSession(Route::BleSetOs, true) && needsCsrf(Method::Put, Route::BleSetOs, true));
  CHECK(needsSession(Route::WifiScan, true) && needsSession(Route::WifiHome, true));
  CHECK(needsSession(Route::ListTrusted, true) && needsSession(Route::DeleteTrusted, true));
  CHECK(needsCsrf(Method::Put, Route::WifiHome, true) && needsCsrf(Method::Delete, Route::DeleteTrusted, true));
  CHECK(!needsCsrf(Method::Get, Route::WifiScan, true));

  // F8: what the web app sends on its own never keeps the vault open; the user's requests do.
  CHECK(!countsAsActivity(Method::Get, Route::ListEntries));
  CHECK(!countsAsActivity(Method::Get, Route::EntryTotp));
  CHECK(!countsAsActivity(Method::Get, Route::GetBle));
  CHECK(countsAsActivity(Method::Get, Route::GetEntry) && countsAsActivity(Method::Post, Route::RevealEntry));
  CHECK(countsAsActivity(Method::Get, Route::GetSettings) && countsAsActivity(Method::Put, Route::PutSettings));
  CHECK(countsAsActivity(Method::Post, Route::Type) && countsAsActivity(Method::Put, Route::UpdateEntry));
  CHECK(countsAsActivity(Method::Post, Route::BlePair) && countsAsActivity(Method::Post, Route::CreateEntry));
}

// SPEC §17: tokens are managed by a session; /api/agent/… takes a bearer token only.
void tokenRoutes() {
  CHECK(is(matchApi(Method::Get, "/api/tokens"), Route::ListTokens));
  CHECK(is(matchApi(Method::Post, "/api/tokens"), Route::CreateToken));
  CHECK(is(matchApi(Method::Delete, "/api/tokens/77"), Route::DeleteToken, 77));
  CHECK(matchApi(Method::Get, "/api/tokens/77").kind == K::MethodNotAllowed);
  CHECK(matchApi(Method::Delete, "/api/tokens/0").kind == K::NotFound);
  CHECK(matchApi(Method::Delete, "/api/tokens/x").kind == K::NotFound);
  CHECK(needsSession(Route::ListTokens, true) && needsSession(Route::CreateToken, true) && needsSession(Route::DeleteToken, true));
  CHECK(needsCsrf(Method::Post, Route::CreateToken, true) && needsCsrf(Method::Delete, Route::DeleteToken, true));
  CHECK(!isAgent(Route::CreateToken) && !isAgent(Route::Type));

  CHECK(is(matchApi(Method::Get, "/api/agent/entries"), Route::AgentEntries));
  CHECK(is(matchApi(Method::Post, "/api/agent/type"), Route::AgentType));
  CHECK(is(matchApi(Method::Get, "/api/agent/status"), Route::AgentStatus));
  CHECK(is(matchApi(Method::Post, "/api/agent/cancel"), Route::AgentCancel));
  CHECK(is(matchApi(Method::Post, "/api/agent/save"), Route::AgentSave));
  CHECK(is(matchApi(Method::Post, "/api/agent/generate"), Route::AgentGenerate));
  CHECK(is(matchApi(Method::Post, "/api/agent/match"), Route::AgentMatch));
  CHECK(matchApi(Method::Get, "/api/agent/match").kind == K::MethodNotAllowed);
  CHECK(is(matchApi(Method::Get, "/api/agent/targets"), Route::AgentTargets));
  CHECK(is(matchApi(Method::Post, "/api/agent/ble/pair"), Route::AgentBlePair));
  CHECK(matchApi(Method::Post, "/api/agent/targets").kind == K::MethodNotAllowed);
  CHECK(matchApi(Method::Get, "/api/agent/ble/pair").kind == K::MethodNotAllowed);
  CHECK(matchApi(Method::Get, "/api/agent/type").kind == K::MethodNotAllowed);
  CHECK(matchApi(Method::Get, "/api/agent/entries/1").kind == K::NotFound);
  for (Route r : {Route::AgentEntries, Route::AgentType, Route::AgentStatus, Route::AgentCancel, Route::AgentSave,
                  Route::AgentGenerate, Route::AgentMatch, Route::AgentTargets, Route::AgentBlePair}) {
    CHECK(isAgent(r));
    CHECK(!needsSession(r, true));
    CHECK(!needsCsrf(Method::Post, r, true));
  }
}

// SPEC §18: tags are managed by a session; the tap page's calls need none.
void tagRoutes() {
  CHECK(is(matchApi(Method::Get, "/api/tags"), Route::ListTags));
  CHECK(is(matchApi(Method::Post, "/api/tags"), Route::CreateTag));
  CHECK(is(matchApi(Method::Delete, "/api/tags/9"), Route::DeleteTag, 9));
  CHECK(matchApi(Method::Put, "/api/tags").kind == K::MethodNotAllowed);
  CHECK(matchApi(Method::Get, "/api/tags/9").kind == K::MethodNotAllowed);
  CHECK(matchApi(Method::Delete, "/api/tags/0").kind == K::NotFound);
  CHECK(needsSession(Route::ListTags, true) && needsSession(Route::CreateTag, true) && needsSession(Route::DeleteTag, true));
  CHECK(needsCsrf(Method::Post, Route::CreateTag, true) && needsCsrf(Method::Delete, Route::DeleteTag, true));
  CHECK(is(matchApi(Method::Post, "/api/tag/tap"), Route::TagTap));
  CHECK(is(matchApi(Method::Post, "/api/tag/status"), Route::TagStatus));
  CHECK(matchApi(Method::Get, "/api/tag/tap").kind == K::MethodNotAllowed);
  for (Route r : {Route::TagTap, Route::TagStatus}) {
    CHECK(isTagTap(r) && !isAgent(r));
    CHECK(!needsSession(r, true) && !needsCsrf(Method::Post, r, true));
  }
  CHECK(!isTagTap(Route::CreateTag) && !isTagTap(Route::Type));
  CHECK(isTapPage("/t/123/abc") && isTapPage("/t/123"));
  CHECK(!isTapPage("/t/") && !isTapPage("/t") && !isTapPage("/") && !isTapPage("/tags") && !isTapPage("/api/t/1"));
}

void hosts() {
  CHECK(isOwnHost(""));
  CHECK(isOwnHost("192.168.4.1"));
  CHECK(isOwnHost("192.168.4.1:80"));
  CHECK(isOwnHost("keyra.local"));
  CHECK(isOwnHost("Keyra.Local."));
  CHECK(isOwnHost("keyra"));
  CHECK(!isOwnHost("captive.apple.com"));
  CHECK(!isOwnHost("keyra.local.evil.com"));
  CHECK(!isOwnHost("192.168.4.10"));
  // Joined to the home network: its current IP addresses the device too.
  CHECK(isOwnHost("192.168.1.42", "192.168.1.42"));
  CHECK(isOwnHost("192.168.1.42:80", "192.168.1.42"));
  CHECK(!isOwnHost("192.168.1.42"));
  CHECK(!isOwnHost("192.168.1.43", "192.168.1.42"));
  CHECK(!isOwnHost("192.168.1.4", "192.168.1.42"));
}

void origins() {
  CHECK(isAllowedOrigin("", false));
  CHECK(isAllowedOrigin("http://keyra.local", true));
  CHECK(isAllowedOrigin("http://192.168.4.1", true));
  CHECK(isAllowedOrigin("http://192.168.4.1:80", true));
  CHECK(!isAllowedOrigin("", true));
  CHECK(!isAllowedOrigin("null", true));
  CHECK(!isAllowedOrigin("https://evil.example", true));
  CHECK(!isAllowedOrigin("http://evil.example", true));
  CHECK(!isAllowedOrigin("http://keyra.local.evil.example", true));
  CHECK(!isAllowedOrigin("http://", true));
  CHECK(isAllowedOrigin("http://192.168.1.42", true, "192.168.1.42"));
  CHECK(!isAllowedOrigin("http://192.168.1.42", true));
  CHECK(!isAllowedOrigin("http://192.168.1.99", true, "192.168.1.42"));
}

// SPEC §9.4: a browser extension may call /api/agent/… (bearer token) but never
// a session route, where the cookie would ride along.
void extensionOrigins() {
  const char* chrome = "chrome-extension://abcdefghijklmnopabcdefghijklmnop";
  const char* firefox = "moz-extension://2f1e9a4c-6b1d-4c3e-9f0a-1b2c3d4e5f60";
  const char* safari = "safari-web-extension://2F1E9A4C-6B1D-4C3E-9F0A-1B2C3D4E5F60";
  for (const char* o : {chrome, firefox, safari}) {
    CHECK(isExtensionOrigin(o));
    CHECK(!isAllowedOrigin(o, true));  // the base rule is unchanged
    for (Route r : {Route::AgentType, Route::AgentSave, Route::AgentGenerate, Route::AgentCancel, Route::AgentMatch})
      CHECK(isAllowedOriginFor(r, o, true));
    for (Route r : {Route::Type, Route::CreateEntry, Route::UpdateEntry, Route::Unlock, Route::Setup,
                    Route::FactoryReset, Route::CreateToken, Route::DeleteToken, Route::TagTap, Route::PresenceCancel})
      CHECK(!isAllowedOriginFor(r, o, true));
  }
  // Keyra's own origins and no Origin at all still work everywhere.
  CHECK(isAllowedOriginFor(Route::AgentType, "http://keyra.local", true));
  CHECK(isAllowedOriginFor(Route::Type, "http://keyra.local", true));
  CHECK(isAllowedOriginFor(Route::AgentType, "", false) && isAllowedOriginFor(Route::Type, "", false));
  CHECK(isAllowedOriginFor(Route::AgentType, "http://192.168.1.42", true, "192.168.1.42"));
  // Web pages and look-alikes stay refused, agent routes included.
  for (const char* o : {"https://evil.example", "null", "", "chrome-extension://", "chrome-extension://id/x",
                        "Chrome-Extension://abc", "http://chrome-extension://abc", "chrome-extension:abc",
                        "extension://abc", "moz-extension://", "safari-web-extension://"}) {
    CHECK(!isExtensionOrigin(o));
    CHECK(!isAllowedOriginFor(Route::AgentType, o, true));
  }
}

void probes() {
  auto apple = probeFor("/hotspot-detect.html");
  CHECK(apple && std::string(apple->body).find("Success") != std::string::npos);
  CHECK(probeFor("/library/test/success.html").has_value());
  CHECK(std::string(probeFor("/generate_204")->status) == "204 No Content");
  CHECK(std::string(probeFor("/gen_204")->status) == "204 No Content");
  CHECK(std::string(probeFor("/connecttest.txt")->body) == "Microsoft Connect Test");
  CHECK(std::string(probeFor("/ncsi.txt")->body) == "Microsoft NCSI");
  CHECK(std::string(probeFor("/success.txt")->body) == "success");
  CHECK(!probeFor("/").has_value());
}

void clockAdoption() {
  const int64_t t = 1790000000000LL;  // 2026
  const int64_t m = 5000;             // monotonic ms at the first request
  {
    clock::Trust c;  // trusted moments (setup, unlock) and a clock never set: as before
    CHECK(c.adopt(0, m, t, false, false));                // nothing better known: anyone sets it
    CHECK(!c.adopt(t, m, t + clock::kMaxDriftMs, false, true));
    CHECK(c.adopt(t, m, t + clock::kMaxDriftMs + 1, false, true));
    CHECK(c.adopt(t, m, t - clock::kMaxDriftMs - 1, false, true));
    CHECK(c.adopt(t, m, t + 86400000, false, true));      // an unlock may move it any distance
    CHECK(!c.adopt(0, m, 1000, false, true));             // client clock obviously wrong
    CHECK(!c.adopt(0, m, clock::kValidBeforeMs, false, true));
  }
  CHECK(clock::parse("1790000000000") == t);
  CHECK(!clock::parse("").has_value());
  CHECK(!clock::parse("-5").has_value());
  CHECK(!clock::parse("17900000000001234").has_value());
  CHECK(!clock::parse("12.5").has_value());
  {
    // Once SNTP has set the clock, the phone's clock is only a fallback.
    clock::Trust c;
    CHECK(!c.adopt(t, m, t + 60000, true, true));
    CHECK(c.adopt(0, m, t, true, false));  // synced flag without a valid clock: still adopt
  }
  {
    // A session (F2): within kMaxSessionSkewMs of the trusted time, never further,
    // and measured from the trusted time, so steps cannot walk the clock.
    clock::Trust c;
    CHECK(c.adopt(0, m, t, false, true));  // the unlock set it to t at monotonic m
    const int64_t later = m + 60000;       // a minute on, the trusted time is t + 60 s
    CHECK(c.adopt(t + 60000, later, t + 60000 + 4 * 60000, false, false));   // 4 min off: corrected
    CHECK(!c.adopt(t + 300000, later, t + 60000 + 8 * 60000, false, false)); // 8 min off the trusted time
    CHECK(!c.adopt(t + 300000, later, t + 60000 + clock::kMaxSessionSkewMs + 1, false, false));
    CHECK(c.adopt(t + 300000, later, t + 60000, false, false));              // back to the trusted time
    CHECK(!c.adopt(t + 60000, later, t + 60000 - clock::kMaxSessionSkewMs - 1, false, false));
    // Two /totp calls 60 s apart in header time an hour ahead: neither moves the clock.
    CHECK(!c.adopt(t + 60000, later, t + 3600000, false, false));
    CHECK(!c.adopt(t + 60000, later, t + 3660000, false, false));
    // Walking forward 4 minutes per request stops at the bound.
    int64_t device = t + 60000;
    int steps = 0;
    for (int i = 1; i <= 100; ++i) {
      const int64_t want = device + 4 * 60000;
      if (c.adopt(device, later, want, false, false)) {
        device = want;
        ++steps;
      }
    }
    CHECK(steps == 1 && device - (t + 60000) <= clock::kMaxSessionSkewMs);
    // A new unlock is trusted again and becomes the reference.
    CHECK(c.adopt(device, later, t + 7200000, false, true));
    CHECK(c.adopt(t + 7200000, later, t + 7200000 + 60000, false, false));
  }
  {
    // A clock that was valid before any request (SNTP, or kept across a reboot) is the reference.
    clock::Trust c;
    CHECK(!c.adopt(t, m, t + 10 * 60000, false, false));
    CHECK(c.adopt(t, m, t + 60000, false, false));
  }
}

void inputRules() {
  CHECK(validate::utf8Length("abc") == 3);
  CHECK(validate::utf8Length("\xD8\xA8\xD8\xA7") == 2);  // Arabic "با"
  CHECK(validate::utf8Length("\xC0\xAF") == -1);         // overlong
  CHECK(validate::utf8Length("\xED\xA0\x80") == -1);     // surrogate
  CHECK(validate::utf8Length("\xE2\x82") == -1);         // truncated
  CHECK(!validate::passphrase("short"));
  CHECK(validate::passphrase("ten chars!"));
  CHECK(validate::passphrase(std::string(128, 'x')));
  CHECK(!validate::passphrase(std::string(129, 'x')));
  // 10 Arabic letters = 20 bytes but 10 characters.
  std::string arabic;
  for (int i = 0; i < 10; ++i) arabic += "\xD8\xA8";
  CHECK(validate::passphrase(arabic));
  CHECK(!validate::wifiPassword("keyra1234"));
  CHECK(!validate::wifiPassword("1234567"));
  CHECK(validate::wifiPassword("12345678"));
  CHECK(validate::wifiPassword(std::string(63, 'a')));
  CHECK(!validate::wifiPassword(std::string(64, 'a')));
  CHECK(!validate::wifiPassword("caf\xC3\xA9 au lait"));
  CHECK(!validate::wifiPassword("tab\there!"));
  CHECK(validate::homePassword("keyra1234"));  // the home router may use anything WPA allows
  CHECK(!validate::homePassword("1234567"));
  CHECK(!validate::homePassword(std::string(64, 'a')));
  CHECK(validate::ssid("Keyra-1A2B"));
  CHECK(!validate::ssid(""));
  CHECK(!validate::ssid(std::string(33, 'a')));
  CHECK(!validate::deviceName("bad\nname"));
  CHECK(validate::deviceName("\xD9\x85\xD9\x81\xD8\xAA\xD8\xA7\xD8\xAD"));
}

}  // namespace

int main() {
  apiRoutes();
  tokenRoutes();
  tagRoutes();
  policy();
  hosts();
  origins();
  extensionOrigins();
  probes();
  clockAdoption();
  inputRules();
  return KEYRA_TEST_RESULT();
}
