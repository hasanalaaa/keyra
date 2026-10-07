#!/usr/bin/env bash
# Signs a built Keyra app image for network updates (SPEC §14).
#
#   tools/sign_release.sh firmware/build-release        → build-release/keyra-signed.bin
#
# The key is $KEYRA_SIGNING_KEY or ~/.keyra/keyra-signing-key.pem (RSA-3072,
# Secure Boot V2 scheme). It never belongs in the repository. A Keyra accepts
# updates signed with the key its own firmware was signed with, so the first
# signed image goes on by USB; after that, keep the key safe: losing it means
# updates by USB only, and anyone holding it can make firmware your Keyra trusts.
#
# New key (once):  espsecure generate-signing-key --version 2 --scheme rsa3072 ~/.keyra/keyra-signing-key.pem
set -euo pipefail
dir=${1:?usage: tools/sign_release.sh <build dir>}
key=${KEYRA_SIGNING_KEY:-$HOME/.keyra/keyra-signing-key.pem}
[ -f "$key" ] || { echo "signing key not found: $key" >&2; exit 1; }
[ -f "$dir/keyra.bin" ] || { echo "no $dir/keyra.bin: build first" >&2; exit 1; }
python -m espsecure sign-data --version 2 --keyfile "$key" --output "$dir/keyra-signed.bin" "$dir/keyra.bin"
python -m espsecure verify-signature --version 2 --keyfile "$key" "$dir/keyra-signed.bin"
echo "signed: $dir/keyra-signed.bin"
