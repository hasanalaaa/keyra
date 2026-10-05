## What and why

<!-- One or two sentences. Link the issue: Fixes #123 -->

## How I tested it

- [ ] Host tests: `cmake -S firmware/test/host -B build/host && cmake --build build/host && ctest --test-dir build/host`
- [ ] Web: `npm --prefix web run typecheck && npm --prefix web test` (if `web/` changed)
- [ ] Firmware builds (dev and release profile) and I ran it on a board (if `firmware/` changed). Board:
- [ ] Not tested on hardware, because:

## Checklist

- [ ] The change is limited to what the description says
- [ ] Docs updated (`docs/SPEC.md` for API or behavior changes, `CHANGELOG.md` for user-visible ones)
- [ ] If `web/` changed, `firmware/components/keyra_api/www/` is rebuilt and included
- [ ] No secrets, passphrases or real credentials in code, logs, screenshots or tests
- [ ] Security-sensitive change (crypto, storage format, auth, button/typing path)? I described the threat-model impact above
