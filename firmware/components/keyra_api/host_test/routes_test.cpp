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
  CHECK(!needsSession(Route::State));
  CHECK(!needsSession(Route::Setup));
  CHECK(!needsSession(Route::Unlock));
  CHECK(!needsSession(Route::FactoryReset));
  CHECK(needsSession(Route::Lock));
  CHECK(needsSession(Route::GetEntry));
  CHECK(needsSession(Route::TypeCancel));
  CHECK(!needsCsrf(Method::Get, Route::ListEntries));
  CHECK(!needsCsrf(Method::Post, Route::Unlock));
  CHECK(!needsCsrf(Method::Post, Route::Setup));
  CHECK(!needsCsrf(Method::Post, Route::FactoryReset));
  CHECK(needsCsrf(Method::Post, Route::Lock));
  CHECK(needsCsrf(Method::Delete, Route::DeleteEntry));
  CHECK(needsCsrf(Method::Put, Route::PutSettings));
  CHECK(needsSession(Route::WifiScan) && needsSession(Route::WifiHome));
  CHECK(needsSession(Route::ListTrusted) && needsSession(Route::DeleteTrusted));
  CHECK(needsCsrf(Method::Put, Route::WifiHome) && needsCsrf(Method::Delete, Route::DeleteTrusted));
  CHECK(!needsCsrf(Method::Get, Route::WifiScan));
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
  CHECK(clock::shouldAdopt(0, t));
  CHECK(!clock::shouldAdopt(t, t + clock::kMaxDriftMs));
  CHECK(clock::shouldAdopt(t, t + clock::kMaxDriftMs + 1));
  CHECK(clock::shouldAdopt(t, t - clock::kMaxDriftMs - 1));
  CHECK(!clock::shouldAdopt(0, 1000));  // client clock obviously wrong
  CHECK(!clock::shouldAdopt(0, clock::kValidBeforeMs));
  CHECK(clock::parse("1790000000000") == t);
  CHECK(!clock::parse("").has_value());
  CHECK(!clock::parse("-5").has_value());
  CHECK(!clock::parse("17900000000001234").has_value());
  CHECK(!clock::parse("12.5").has_value());
  // Once SNTP has set the clock, the phone's clock is only a fallback.
  CHECK(!clock::shouldAdopt(t, t + 60000, true));
  CHECK(clock::shouldAdopt(0, t, true));  // synced flag without a valid clock: still adopt
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
  policy();
  hosts();
  origins();
  probes();
  clockAdoption();
  inputRules();
  return KEYRA_TEST_RESULT();
}
