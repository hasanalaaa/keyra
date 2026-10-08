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

## Wave 3 — hardening and coverage

| Item | Why | Acceptance |
|---|---|---|
| e2e for recovery-key create/unlock, restore merge + replace, factory reset, passphrase change, settings changes, entry delete, trusted-browser removal | These flows have no browser test today | Each flow passes in `npm run e2e` in ar and en-dark |
| Mock drift: passphrase throttle (429 + retryAfterMs), `host.usbOs` only with a session, home-network error kept while connected | The app is tested against the mock | Mock answers match firmware handlers |
| SPEC §5 API table refreshed (settings fields, `failedAttempts`, `/type` fields, result codes) | The table predates v1.1 | Every route and field in `routes.cpp` / handlers is listed |
| ~~UI ranges: LED 0, more typing delays~~ | Kept on purpose (DESIGN App. B): an LED that can be turned off hides "waiting for the button"; the three delays are the tested ones | — |
| Passkeys and the activity log are not in backups: say so | Silent surprise on a new device | Done (Backup and Passkeys screens) |
| Web bundle at 179 KB of 190 KB | Little room left | Next big screen needs trimming first (e.g. lazy QR/Shamir code) |

## Wave 4 — owner decisions first, then build

1. **2FA codes without a press** — decided: kept, documented as an exception (SPEC §12.5a).
2. **Destructive edits without a press** — decided: "delete account" and "remove
   passkey" need a press (in progress); the rest stays session-only and logged.
3. **Passkeys in backups.** Today they are lost with the device. Needs a backup
   format change (encrypted records + salt) — design note first.

## Wave 5 — new features (from the research, still valid)

| Item | Value | Effort | Note |
|---|---|---|---|
| CTAP2 ClientPIN = master passphrase, `hmac-secret`, `credProtect` | Wider site support, SSH `-sk`, LUKS | L | FEATURES-2 D10 |
| Agent Gate: arm-only tokens for AI agents (never return a secret) | Unique; fits the button model | M | FEATURES-2 D7 |
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
