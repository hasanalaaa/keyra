#pragma once
// Pure DNS reply builder for the captive resolver; no sockets, host-testable.
#include <array>
#include <cstddef>
#include <cstdint>

namespace keyra::net::dns {

constexpr size_t kMaxPacket = 512;  // classic UDP DNS limit; we never send EDNS

// Writes the reply to `query` into `out` and returns its length, or 0 when the
// packet must be dropped (malformed, or itself a response). Every A/IN question
// is answered with `ip`; any other type gets NOERROR with no answers, so phones
// do not retry AAAA/HTTPS lookups through a timeout.
size_t reply(const uint8_t* query, size_t len, const std::array<uint8_t, 4>& ip, uint8_t* out,
             size_t cap);

}  // namespace keyra::net::dns
