#!/usr/bin/env bash
# Regenerates firmware/test/fixtures/v0.2.0-3ce814a from the real 3ce814a
# sources (see the README there). Needs git, cmake, a C++17 compiler and OpenSSL 3.
#   firmware/test/upgrade/regen.sh
set -euo pipefail

OLD_REV=3ce814a
here="$(cd "$(dirname "$0")" && pwd)"
repo="$(git -C "$here" rev-parse --show-toplevel)"
fixtures="$repo/firmware/test/fixtures/v0.2.0-$OLD_REV"
work="$(mktemp -d)"
cleanup() {
  git -C "$repo" worktree remove --force "$work/old" >/dev/null 2>&1 || true
  rm -rf "$work"
}
trap cleanup EXIT

git -C "$repo" worktree add --detach "$work/old" "$OLD_REV" >/dev/null

# 1. 3ce814a writes a Keyra's worth of data.
cmake -S "$here/old" -B "$work/build-old" -DKEYRA_TREE="$work/old" >/dev/null
cmake --build "$work/build-old" -j >/dev/null
"$work/build-old/gen_old" v020 "$work/v020"

# 2. This tree (0.3.0) opens it and writes its own data, then 3ce814a boots it
#    again (a rolled-back update) and must use all of it.
cmake -S "$repo/firmware/test/host" -B "$work/build-host" >/dev/null
cmake --build "$work/build-host" -j --target keyra_upgrade_test >/dev/null
"$work/build-host/upgrade/keyra_upgrade_test" write030 "$work/v020/device" "$work/v030"
"$work/build-old/gen_old" rollback "$work/v030" "$work/after-rollback"

# 3. Replace the fixtures (README.md stays) and check them with this tree.
rm -rf "$fixtures/device" "$fixtures/after-rollback"
mkdir -p "$fixtures"
cp -R "$work/v020/." "$fixtures/"
cp -R "$work/after-rollback" "$fixtures/after-rollback"
"$work/build-host/upgrade/keyra_upgrade_test"
echo "fixtures written to $fixtures"
