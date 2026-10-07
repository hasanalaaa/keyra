#pragma once
// Plain types shared by the net API and its host-tested policy code (no ESP-IDF headers).
#include <cstdint>

namespace keyra::net {

// Always: Keyra's own Wi-Fi stays on. Fallback: it is off while the home network
// is connected and comes back when that network is unavailable (src/link.hpp).
enum class ApMode : uint8_t { Always, Fallback };

// How an HTTP request reached the device, from its socket's local address.
enum class Via : uint8_t { Ap, Home };

// Why the last attempt to join the home network failed (cleared on success or
// a new configuration), so the app can say "wrong password" instead of
// "Connecting…" forever.
enum class HomeError : uint8_t { None, WrongPassword, NotFound, Failed };

}  // namespace keyra::net
