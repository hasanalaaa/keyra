#pragma once
// The operating system of each computer Keyra types into (SPEC §10.5). It
// decides how text reaches the screen regardless of the host's input
// language: Windows takes Alt codes, macOS/iOS switch with Ctrl+Space, the
// rest type plain keys. Pure, host-tested.
#include <cstdint>
#include <string>
#include <string_view>

#include "keyra/ble.hpp"

namespace keyra::api::hostos {

enum class Os : uint8_t { Unknown, Mac, Ios, Windows, Android, Linux };

const char* name(Os);                       // "" for Unknown, else "mac", "ios", …
bool parse(std::string_view s, Os& out);    // accepts "" (Unknown) and every name()

// Per-bond systems, one NVS string: "A4:C1:38:0B:7F:3A=mac;…". Unknown or
// malformed items read as Unknown; set(…, Unknown) removes the bond's item.
Os get(std::string_view table, const ble::Addr& addr);
std::string set(std::string_view table, const ble::Addr& addr, Os os);

// Whether the host turns Ctrl+Space into "switch input language" (macOS, iOS, iPadOS).
inline bool switchesWithCtrlSpace(Os os) { return os == Os::Mac || os == Os::Ios; }

}  // namespace keyra::api::hostos
