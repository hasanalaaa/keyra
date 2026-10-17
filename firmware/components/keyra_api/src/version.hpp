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

// The floor a vault moved to the 16 MB tail sets (SPEC §6.1): `next` must not
// be older than the firmware that moved it. An empty floor (not moved) allows
// anything; a floor that does not parse is ignored, since refusing every
// update would leave a device that is updated only over the air stuck; the
// running version, which mayInstall() also checks, is never below it.
bool mayInstallOver(std::string_view next, std::string_view floor);

// A GitHub release tag ("v0.2.0" or "0.2.0") as a version, or empty when the
// tag is not one.
std::string fromTag(std::string_view tag);

// The signed app image a release carries for a board (CONFIG_KEYRA_BOARD_ID;
// tools/build_boards.sh uses the same rule): the default board's keeps the
// name earlier firmware on that board looks for.
inline constexpr std::string_view kDefaultBoard = "devkitc1";
std::string assetName(std::string_view boardId);

}  // namespace keyra::api::version
