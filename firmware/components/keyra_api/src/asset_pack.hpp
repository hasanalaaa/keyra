#pragma once
// The web app's hashed files (/assets/<name>.js|.css|.woff2), packed into one
// embedded blob by web/scripts/postbuild.mjs (SPEC §5 Static). Pure, host-tested.
//
// Layout, little-endian: "KYA1", then per file: u16 path length, path,
// u16 type length, Content-Type ending in one NUL (counted in the length),
// u8 flags (bit 0: body is gzip), u32 body size, body.
#include <cstddef>
#include <cstdint>
#include <optional>
#include <string_view>

namespace keyra::api::assets {

struct File {
  const char* type;  // NUL-terminated, inside the pack
  const uint8_t* body;
  size_t size;
  bool gzip;
};

// The file served at `path`, or nullopt when the pack has none (or is
// malformed: every length is checked against the pack's end).
std::optional<File> find(const uint8_t* pack, size_t packLen, std::string_view path);

}  // namespace keyra::api::assets
