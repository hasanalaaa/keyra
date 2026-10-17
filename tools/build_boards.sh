#!/usr/bin/env bash
# Builds the release profile for each board in firmware/boards/boards.json
# (sdkconfig.defaults + sdkconfig.release + boards/<id>.defaults), clean, into
# firmware/build-<id>, and signs it with tools/sign_release.sh
# (build-<id>/keyra-signed.bin). Releases publish it as keyra-firmware.bin for
# the default board and keyra-firmware-<id>.bin for the others (SPEC §14).
#
#   tools/build_boards.sh                     every board, signed
#   tools/build_boards.sh tinys3 atoms3u      only those
#   tools/build_boards.sh --unsigned --out D  CI: no key needed, build dirs under D
#   tools/build_boards.sh --ids [--others]    print the board ids (--others: all but the default)
#
# Needs ESP-IDF in the environment (. ~/esp/esp-idf-v6.0.2/export.sh).
set -euo pipefail
root=$(cd "$(dirname "$0")/.." && pwd)
boards="$root/firmware/boards/boards.json"

# boards.json is checked on every run: a wrong id would publish an asset no device looks for.
check() {
  python3 - "$boards" "$@" <<'EOF'
import json, os, re, sys
path, mode = sys.argv[1], sys.argv[2:]
boards = json.load(open(path))
keys = {"id", "name", "vendor", "url", "flash_mb", "psram", "button", "led", "usb", "tested", "default", "notes"}
ids = [b["id"] for b in boards]
for b in boards:
    if set(b) != keys:
        sys.exit(f"{path}: {b.get('id')}: fields must be exactly {sorted(keys)}")
    if not re.fullmatch(r"[a-z0-9]+(-[a-z0-9]+)*", b["id"]):
        sys.exit(f"{path}: bad id {b['id']!r}")
    if not os.path.isfile(os.path.join(os.path.dirname(path), b["id"] + ".defaults")):
        sys.exit(f"{path}: no {b['id']}.defaults")
    if b["flash_mb"] < 8:
        sys.exit(f"{path}: {b['id']}: Keyra needs 8 MB of flash")
if len(set(ids)) != len(ids):
    sys.exit(f"{path}: duplicate ids")
# keyra::api::version::kDefaultBoard: the board that keeps keyra-firmware.bin.
if [b["id"] for b in boards if b["default"]] != ["devkitc1"]:
    sys.exit(f"{path}: exactly one default board, devkitc1")
if mode == ["--ids", "--others"]:
    print(" ".join(b["id"] for b in boards if not b["default"]))
elif mode == ["--ids"]:
    print(" ".join(ids))
EOF
}

sign=1
out="$root/firmware"
case "${1:-}" in
  --ids) check "$@"; exit 0 ;;
esac
while [ $# -gt 0 ]; do
  case $1 in
    --unsigned) sign=0; shift ;;
    --out) mkdir -p "$2"; out=$(cd "$2" && pwd); shift 2 ;;
    -*) echo "unknown option $1" >&2; exit 2 ;;
    *) break ;;
  esac
done
check
all=$(check --ids)
if [ $# -gt 0 ]; then ids="$*"; else ids=$all; fi
version=$(sed -n 's/^set(PROJECT_VER "\(.*\)")$/\1/p' "$root/firmware/CMakeLists.txt")

for id in $ids; do
  case " $all " in *" $id "*) ;; *) echo "unknown board $id (see $boards)" >&2; exit 1 ;; esac
  dir="$out/build-$id"
  rm -rf "$dir"
  mkdir -p "$dir"
  if ! (cd "$root/firmware" && idf.py -B "$dir" -D SDKCONFIG="$dir/sdkconfig" \
      -D SDKCONFIG_DEFAULTS="sdkconfig.defaults;sdkconfig.release;boards/$id.defaults" build >"$dir/build.log" 2>&1); then
    tail -n 40 "$dir/build.log" >&2
    echo "$id: build failed (log: $dir/build.log)" >&2
    exit 1
  fi
  # The profile must name its own board, or the image would look for another board's updates.
  grep -qx "CONFIG_KEYRA_BOARD_ID=\"$id\"" "$dir/sdkconfig" \
    || { echo "$id: boards/$id.defaults does not set CONFIG_KEYRA_BOARD_ID=\"$id\"" >&2; exit 1; }
  built=$(python3 -c "import json,sys;print(json.load(open(sys.argv[1]))['project_version'])" "$dir/project_description.json")
  [ "$built" = "$version" ] || { echo "$id: built $built, expected $version" >&2; exit 1; }
  if [ "$sign" = 1 ]; then "$root/tools/sign_release.sh" "$dir" >/dev/null; fi
  echo "$id: ok"
done
