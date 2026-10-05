#pragma once
// Input rules from SPEC §5 shared by setup, settings and passphrase changes.
#include <cstddef>
#include <string_view>

namespace keyra::api::validate {

constexpr const char* kDefaultWifiPassword = "keyra1234";

// Code points in well-formed UTF-8, or -1 when malformed.
long utf8Length(std::string_view s);
bool passphrase(std::string_view s);    // 10–128 characters
bool wifiPassword(std::string_view s);  // 8–63 printable ASCII, not the factory default
bool ssid(std::string_view s);          // 1–32 bytes UTF-8, no control characters
bool deviceName(std::string_view s);    // 1–32 bytes UTF-8, no control characters

}  // namespace keyra::api::validate
