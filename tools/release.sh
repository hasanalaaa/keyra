#!/usr/bin/env bash
# Builds, signs and publishes a Keyra release on GitHub (SPEC §14).
#
#   tools/release.sh            → release v<PROJECT_VER> from firmware/CMakeLists.txt
#
# Needs: ESP-IDF in the environment (. ~/esp/esp-idf-v6.0.2/export.sh), `gh`
# logged in, a clean tree pushed to origin, a "## [<version>]" section in
# CHANGELOG.md (its text becomes the release notes; the top of it is what
# Keyra shows in Settings → Firmware update), and the signing key (see
# tools/sign_release.sh). Every Keyra signed with that key can then install it
# from Settings → Firmware update.
#
# Assets: keyra-firmware.bin (the signed app: what updates install) and, for a
# first install by USB, bootloader.bin, partition-table.bin, ota_data_initial.bin
# and flash-offsets.txt.
set -euo pipefail
root=$(cd "$(dirname "$0")/.." && pwd)
cd "$root"
version=$(sed -n 's/^set(PROJECT_VER "\(.*\)")$/\1/p' firmware/CMakeLists.txt)
tag="v$version"
[ -n "$version" ] || { echo "no PROJECT_VER in firmware/CMakeLists.txt" >&2; exit 1; }
[ -z "$(git status --porcelain --untracked-files=no)" ] || { echo "commit your changes first" >&2; exit 1; }
git fetch -q origin
[ "$(git rev-parse HEAD)" = "$(git rev-parse '@{u}')" ] || { echo "push first: HEAD is not on origin" >&2; exit 1; }
if gh release view "$tag" >/dev/null 2>&1; then echo "$tag is already released" >&2; exit 1; fi

notes=$(mktemp)
dist=$(mktemp -d)
trap 'rm -f "$notes"; rm -rf "$dist"' EXIT
awk -v v="$version" '
  $0 ~ "^## \\[" v "\\]" { on = 1; next }
  on && /^## \[/ { exit }
  on { print }
' CHANGELOG.md > "$notes"
[ -s "$notes" ] || { echo "CHANGELOG.md has no \"## [$version]\" section" >&2; exit 1; }

# A clean build directory, inside the gitignored firmware/build* tree.
out=firmware/build-release
rm -rf "$out"
(cd firmware && idf.py -B "../$out" -D SDKCONFIG="../$out/sdkconfig" \
  -D SDKCONFIG_DEFAULTS="sdkconfig.defaults;sdkconfig.release" build >/dev/null)
built=$(python3 -c "import json;print(json.load(open('$out/project_description.json'))['project_version'])")
[ "$built" = "$version" ] || { echo "built $built, expected $version" >&2; exit 1; }
tools/sign_release.sh "$out"

cp "$out/keyra-signed.bin" "$dist/keyra-firmware.bin"
cp "$out/bootloader/bootloader.bin" "$out/partition_table/partition-table.bin" "$out/ota_data_initial.bin" "$dist/"
cat > "$dist/flash-offsets.txt" <<OFFSETS
First install by USB (esptool --chip esp32s3 write_flash):
0x0      bootloader.bin
0x8000   partition-table.bin
0xf000   ota_data_initial.bin
0x20000  keyra-firmware.bin
After that, update from Settings → Firmware update.
OFFSETS
(cd "$dist" && shasum -a 256 ./*.bin > SHA256SUMS)

gh release create "$tag" --title "Keyra $version" --notes-file "$notes" \
  "$dist/keyra-firmware.bin" "$dist/bootloader.bin" "$dist/partition-table.bin" \
  "$dist/ota_data_initial.bin" "$dist/flash-offsets.txt" "$dist/SHA256SUMS"
echo "released $tag"
