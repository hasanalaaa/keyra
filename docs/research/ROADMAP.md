# Keyra roadmap after v0.2.0

Written 2026-10-08 from a code audit (four read-only reviews: passkeys/FIDO, button
presses and reveal grace, restore and firmware update, API ↔ UI coverage) plus the
earlier research in [FEATURES.md](FEATURES.md) and [FEATURES-2.md](FEATURES-2.md).
Every finding below was checked against the code before it was fixed or listed.

Fixed constraints: every action that types or reveals a secret stays behind the
button; no cloud or account; no eFuse burning unless the owner opts in; the browser
extension (SPEC §9.4) stays deferred; Arabic-first UI within 190 KB gzip; persisted
formats and the REST API are contracts (changes need a migration).

## Wave 1 — security fixes (done)

| Item | Commit |
|---|---|
| Passkeys: no signature after the vault locks (keys unwrapped only after the touch, GetNextAssertion keeps IDs only, bound to its CTAPHID channel, forgotten on lock; volatile wipes) | `bda832d` |
| A press approves only what its own session armed (no swapping by another session); press grace per op; backup and recovery-key presses single use; cancel token returned by the arm | `effd8d7` |
| Restore "replace" atomic across power cuts and full storage (stage, commit marker, roll forward/back at init); merge keeps newer local entries and moves replaced passwords into history; replace checked before the press | `a824dcd` |
| Update: install held until the restart; the press installs only the image the phone showed; https-only redirects; rate-limit and network errors told apart; 15 s probation before a new image is kept | `b8ccc90` |

## Wave 2 — features the firmware has but the app does not show (done)

- Keyboard layout per output (GET /api/keyboard, `layoutUsb`/`layoutBle`), Layout
  Doctor probe, layout-safe generator option (SPEC §10.1–10.3).
- Auto-type sequences: editor on the account, "Type sequence" with multi-part
  progress, `bothSequence` editor (SPEC §10.4).
- Mock parity for all of the above. Also: `hasSequence` on entries, Arabic layout
  names, U2F refuses to sign once the vault has locked.

## Wave 3 — hardening and coverage (done)

- e2e for recovery-key create/unlock, restore merge + replace, factory reset,
  passphrase change (with the 429 throttle), settings, entry and passkey delete,
  trusted-browser removal — each in ar/light and en/dark (`dfa5d3f`, `1f9d459`).
- Mock drift fixed: passphrase throttle, `host.usbOs` only with a session, text
  checked against the output's layout, restore "replace" checked before the press.
- Type text checks the layout's own characters (`chars` in GET /api/keyboard)
  instead of US-ASCII; the erase message shows after a factory reset (`afebe53`).
- Kept on purpose (DESIGN App. B): LED minimum 10%, the three typing delays.
- Web bundle at 179.6 KB of 190 KB: the next big screen needs trimming first.

## Wave 4 — owner decisions first, then build

1. **2FA codes without a press** — decided: kept, documented as an exception (SPEC §12.5a).
2. **Destructive edits without a press** — decided and done (`1f9d459`): "delete
   account" and "remove passkey" need a press; the rest stays session-only and logged.
3. **Passkeys in backups** — decided ([PASSKEY-BACKUP.md](PASSKEY-BACKUP.md)):
   backup v3 carries the wrap keys, records and counter; in progress.

## Wave 5 — new features (from the research, still valid)

| Item | Value | Effort | Note |
|---|---|---|---|
| CTAP2 ClientPIN = master passphrase, `hmac-secret`, `credProtect` | Wider site support, SSH `-sk`, LUKS | L | FEATURES-2 D10 |
| Agent Gate: arm-only tokens for AI agents (never return a secret) — **built** (SPEC §17, [TOKENS.md](TOKENS.md), `tools/keyra-mcp`) | Unique; fits the button model | M | FEATURES-2 D7 |
| Android app: autofill save/fill with button-gated typing | Biggest daily-use gain on phones | L | FEATURES-2 D6 |
| NTAG 424 tap-to-arm tags | Delight; one entry per tag | M | Needs arm-only tokens |

## Real-hardware checklist (code cannot prove these)

- Pair a second Bluetooth device while the Mac is connected ("always" mode).
- Windows: Alt-code typing with an Arabic layout active.
- iPhone: input-language switch (Ctrl+Space) during typing.
- Update: install a newer release from Settings; pull power during the first 15 s
  of the new image and confirm the old one comes back.
- Restore "replace" with the cable pulled midway: old or new vault, never mixed.
- Passkey: lock the vault while a site waits for the touch — the site must fail.
