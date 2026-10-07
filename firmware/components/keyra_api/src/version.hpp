#pragma once
// Firmware version rules for updates (SPEC §14). Pure, host-tested.
#include <array>
#include <optional>
#include <string>
#include <string_view>

namespace keyra::api::version {

// "MAJOR.MINOR.PATCH", optionally followed by "-anything" (ignored). Each part
// is 0..9999 without a sign or leading "+"; anything else is nullopt.
std::optional<std::array<int, 3>> parse(std::string_view v);

// An update may install `next` over `running` when both parse and `next` is
// not older (the same version may be reinstalled). Downgrades are refused so a
// stolen session cannot bring back an older, signed but vulnerable firmware.
bool mayInstall(std::string_view next, std::string_view running);

// A GitHub release tag ("v0.2.0" or "0.2.0") as a version, or empty when the
// tag is not one.
std::string fromTag(std::string_view tag);

// The signed app image every release carries (tools/release.sh uploads it).
inline constexpr std::string_view kAssetName = "keyra-firmware.bin";

}  // namespace keyra::api::version
