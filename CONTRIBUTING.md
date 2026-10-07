# Contributing to Keyra

Thanks for helping. Keyra is a small project with a big promise (it holds your
passwords), so contributions are welcome and held to a careful bar.

By participating you agree to the [Code of Conduct](CODE_OF_CONDUCT.md).
Security problems go through a [private advisory](SECURITY.md#reporting-a-vulnerability), never a public issue.

## Ways to help

- **Try it on a board** and tell us what happened (board, OS, phone, keyboard layout). Hardware reports are the most valuable thing right now.
- **Report bugs** and **suggest features** with the issue templates.
- **Translate** the web app (`web/src/lib/i18n.ts`) or improve the docs.
- **Fix something** from the issue list. Small, focused pull requests get merged fastest.

For anything larger than a bug fix, open an issue first so we can agree on the approach before you invest time.

## Ground rules

1. **Physical confirmation is not negotiable.** Nothing may type, change credentials or erase data without a button press. Changes that weaken this will not be merged.
2. **Keep it simple.** The smallest change that fully solves the problem. No speculative options or abstractions.
3. **Secrets stay out of the repo, logs and screenshots.** Use obviously fake data.
4. **`docs/SPEC.md` is the contract** between firmware and web app. Change it in the same pull request as the code that changes behavior.
5. **One concern per pull request.** Do not mix refactors with fixes.

## Setup

You need:

- [ESP-IDF 6.0.x](https://docs.espressif.com/projects/esp-idf/en/stable/esp32s3/get-started/) (CI uses v6.0.2) for the firmware
- Node.js 22 for the web app
- CMake 3.16+, a C++17 compiler and OpenSSL 3 development headers for the host tests

```sh
git clone https://github.com/hasanalaaa/keyra.git
cd keyra
```

## Run the checks

Run what is relevant to your change; CI runs all of it.

```sh
# Host tests (vault, TOTP, key map, action state machine). No board needed.
cmake -S firmware/test/host -B build/host && cmake --build build/host && ctest --test-dir build/host --output-on-failure

# Web app
npm --prefix web ci
npm --prefix web run typecheck
npm --prefix web test
npm --prefix web run build        # also refreshes firmware/components/keyra_api/www/

# Firmware (needs ESP-IDF exported in your shell)
cd firmware
idf.py build                                                                          # dev profile
idf.py -B build-release -D SDKCONFIG_DEFAULTS="sdkconfig.defaults;sdkconfig.release" build   # release profile
```

To work on the UI without a board, run the mock device: `npm --prefix web run mock`.

## Project conventions

- **Firmware** is C++17. Logic that does not need ESP-IDF lives in a plain-C++ core with host tests (see `keyra_vault/src/core`); ESP-IDF code stays in thin adapters. New behavior needs a host test that fails without it.
- **Web** is Preact + TypeScript, plain CSS, no new runtime dependencies without discussion (the whole app ships inside the firmware in at most 170 KB gzipped). Follow [docs/DESIGN.md](docs/DESIGN.md) for tokens, copy tone, accessibility, and Arabic (RTL) support: use logical CSS properties and add both `en` and `ar` strings.
- **Built web assets are committed** in `firmware/components/keyra_api/www/` so the firmware builds without Node. If you change `web/`, rebuild and commit them.
- **Commits**: imperative and scoped, for example `fix(vault): reject empty passphrase`. Keep history readable.
- **Do not** reformat code you are not changing, bump dependencies as a side effect, or edit generated files by hand.

## Pull request checklist

The [pull request template](.github/PULL_REQUEST_TEMPLATE.md) walks you through it. In short: tests pass, docs and `CHANGELOG.md` updated when behavior changes, and a note about how you verified it (on hardware if firmware changed).

## License

Contributions are licensed under the [MIT License](LICENSE).
