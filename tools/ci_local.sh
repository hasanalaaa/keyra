#!/usr/bin/env bash
# Runs the same jobs as .github/workflows/ci.yml on this machine, from a clean
# state, so nothing is pushed that CI would reject. Usage: tools/ci_local.sh
set -euo pipefail
cd "$(dirname "$0")/.."
root=$PWD
out=$(mktemp -d)
trap 'rm -rf "$out"' EXIT

step() { printf '\n== %s\n' "$1"; }

step "Host tests"
cmake -S firmware/test/host -B "$out/host" -DCMAKE_BUILD_TYPE=Debug >/dev/null
cmake --build "$out/host" --parallel >/dev/null
ctest --test-dir "$out/host" --output-on-failure

step "Web app (clean install, typecheck, unit tests, build)"
npm --prefix web ci --no-audit --no-fund >/dev/null
npm --prefix web run typecheck
npm --prefix web test
npm --prefix web run build
# CI rebuilds the web assets; a diff here means the committed www/ is stale.
git diff --exit-code -- firmware/components/keyra_api/www \
  || { echo "firmware/components/keyra_api/www is out of date: commit the rebuilt assets"; exit 1; }

step "Android app (unit tests, builds, lint)"
(cd android && ANDROID_HOME="${ANDROID_HOME:-$HOME/Library/Android/sdk}" JAVA_HOME="${JAVA_HOME:-/Applications/Android Studio.app/Contents/jbr/Contents/Home}" \
  ./gradlew --console=plain --rerun-tasks testDebugUnitTest assembleDebug assembleRelease lint \
  | grep -E "BUILD|FAIL")

step "Firmware (dev, release)"
# shellcheck disable=SC1091
source "${IDF_PATH:-$HOME/esp/esp-idf-v6.0.2}/export.sh" >/dev/null
(cd firmware && idf.py -B "$out/fw-dev" build >/dev/null && echo "dev: ok")
(cd firmware && idf.py -B "$out/fw-release" \
  -D SDKCONFIG_DEFAULTS="sdkconfig.defaults;sdkconfig.release" build >/dev/null && echo "release: ok")

step "Docs links"
python3 - "$root" <<'EOF'
import pathlib, re, sys
root = pathlib.Path(sys.argv[1])
bad = []
for md in [*root.glob("*.md"), *root.glob("docs/*.md")]:
    for link in re.findall(r'(?:\]\(|src=")([^)"#\s]+)', md.read_text()):
        if link.startswith(("http://", "https://", "mailto:")):
            continue
        if not (md.parent / link).exists():
            bad.append(f"{md.relative_to(root)} -> {link}")
print("\n".join(bad) or "all relative links resolve")
sys.exit(1 if bad else 0)
EOF

printf '\nAll CI jobs passed locally.\n'
