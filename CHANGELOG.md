# Changelog

All notable changes to Keyra are documented here. The format follows
[Keep a Changelog](https://keepachangelog.com/en/1.1.0/) and the project uses
[Semantic Versioning](https://semver.org/).

## [Unreleased]

### Added

- **Security key PIN, hmac-secret and credProtect** (firmware + web,
  [FIDO.md](docs/FIDO.md#clientpin)): CTAP2 `authenticatorClientPIN` with PIN/UV
  auth protocols 2 and 1 (getPINRetries, getKeyAgreement, setPIN, changePIN,
  getPINToken). The PIN is set from the computer, is separate from the master
  passphrase, and is kept in the vault as `fidopin.bin` (DEK-encrypted hash and
  retries; 8 tries, 3 wrong per power-up, blocked at 0 until authenticatorReset).
  With a PIN set only the PIN gives user verification; without one nothing
  changes. `hmac-secret` derives its per-credential secrets from the wrapping key
  (nothing new stored; restored backups give the same secrets), `credProtect`
  levels 1-3 are kept in the credential ID's flags byte (older IDs = level 1).
  getInfo lists the extensions, `clientPin`, `pinUvAuthProtocols [2,1]`,
  `maxCredentialCountInList` and `maxCredentialIdLength`; still `FIDO_2_0`.
  `GET /api/fido` adds `pinSet` and `pinRetries`, and Settings → Passkeys shows
  them with how to set the PIN. The PIN is not part of backups.
- **Access tokens and the Agent Gate** (SPEC §17, [TOKENS.md](docs/research/TOKENS.md)):
  Settings → **Apps and agents** creates bearer tokens for AI agents and apps
  (name, kind, the accounts it may use; a button press to create, shown once
  with copy and QR, revoked without a press). With a token, `/api/agent/…`
  lists titles and hosts in scope and arms typing through the same pending
  machine and button press as the web app — it can never read a password,
  username or 2FA secret. App tokens may also save a new account (after a
  press) and generate a password. Keyra keeps only SHA-256 of each token, in a
  new vault record sealed with the data key (`tokens.bin`); tokens work only
  while unlocked, are rate limited (10 requests per 10 s), and every use is in
  the activity log. The phone's "Ready" pill names the token that asked.
- **keyra-mcp** (`tools/keyra-mcp/`): a dependency-free MCP server (stdio) with
  `keyra_list_logins` and `keyra_type_login` for Claude Desktop, Claude Code and
  other agents; it answers only `typed`, `denied` or `expired`.

- **Passkeys in backups** (firmware, [PASSKEY-BACKUP.md](docs/research/PASSKEY-BACKUP.md)):
  backups (format v3) carry the passkey wrapping keys, the passkey records and
  the signature counter while the new setting `passkeysInBackup` is on
  (default on; turning it on needs a button press, op `passkeys_backup_on`).
  Restoring on another Keyra makes discoverable and non-discoverable
  credentials work there; `merge` adds them, `replace` swaps them in the same
  power-cut-safe commit as the entries (a backup without passkeys leaves local
  ones alone). The vault keeps up to 4 wrapping keys (`fido.bin` v2); a restore
  that would pass 4 keys or 50 passkeys is refused with 409 `passkeys_full`
  before anything changes. `POST /api/restore` reports `passkeys` (records
  added); the signature counter is raised past the backup's. v1/v2 backups
  and v1 `fido.bin` still work.
- **Passkeys in backups, web app** (docs/research/PASSKEY-BACKUP.md): Settings
  → "Passkeys in backups" (`passkeysInBackup`, on by default; turning it on is
  a press, `passkeys_backup_on`), the Backup screen says whether the file holds
  passkeys, restore results name the passkeys, and a merge over 50 passkeys or
  4 wrap keys shows a clear refusal; the mock writes and reads v3 backups with
  the same merge/replace rules.
- **Auto-type sequences in the web app** (SPEC §10.4): an optional "Typing
  sequence" on each account (advanced, collapsed) with token buttons, live
  checking against the firmware grammar and a masked preview per part; "Type
  sequence" on the account, with "Part 1 of 2 — press again" on the Ready card
  for each `{PRESS}`; Settings → Typing → Order for "Both" (`bothSequence`, with
  reset to default), which "Both" then types. The mock stores, validates and
  types sequences part by part like the firmware.
- **Keyboard layouts in the web app (SPEC §10.1-10.3):** Settings → Typing
  has a keyboard layout row for USB and for Bluetooth (the firmware's list,
  layouts not yet confirmed on hardware marked "Experimental"); the **Layout
  Doctor** types the probe after a button press, then matches what appeared on
  the computer against each layout's probe string (the computer's system
  breaks a Windows/Mac tie) and saves the suggestion. The generator gains
  "Safe for my keyboard layouts" (`layoutSafe` with both outputs' layouts) and
  a custom symbol set (`symbolSet`), remembered per browser. The mock reads
  the firmware's `layouts.txt` for `GET /api/keyboard`, the probe and the
  layout-safe characters.

### Changed

- **Deleting needs Keyra's button** (SPEC §5, §12.3): `DELETE /api/entries/{id}`
  and `DELETE /api/fido/{id}` now answer 202 `{awaiting:"button", op:
  "delete_entry"|"delete_passkey", expiresIn, cancel}` instead of 204; the
  account or passkey is removed only on the press, and only if the vault is
  still unlocked and the item still there (else the op ends `failed`). The web
  app shows the press screen after the confirm alert ("Press Keyra's button to
  delete this account/passkey"); cancelling or letting it expire keeps the item.
  The mock does the same.

### Fixed

- **iPhone / phones over USB:** Keyra caps its Wi‑Fi transmit power at 13 dBm.
  At 20 dBm its current peaks were more than a phone's USB port supplies; the
  supply dipped, the brownout detector restarted Keyra, and the phone saw the
  keyboard come and go. `/api/state` reports `device.powerDip` after such a
  restart and the app says why.
- **"Type into" on every typing screen:** the generator and Type text now offer
  the USB / Bluetooth device choice the account screen had.
- **Bluetooth stuck on "Connecting…":** after 12 s the app explains the usual
  cause (the device still holds an old pairing) and how to pair again.

- **Type text checks the keyboard layout, not US-ASCII.** `GET /api/keyboard`
  now lists each layout's `chars`; the Type text screen accepts exactly those
  (e.g. ü and ß on German) and names what the set layout lacks (Latin letters
  on Arabic 101 used to pass the app and fail on the device).
- The "Keyra is starting fresh" message shows after a factory reset (the
  screen closed before it could say it).

- **Button presses are bound to who asked (security):** any session could
  replace the item waiting for the press (so the user's press approved
  something else), and one reveal press also allowed a backup download and a
  recovery-key change for 60 s. Now the waiting item belongs to its session
  (others get `busy`), and a press grants only the op it was for — backup and
  recovery-key changes are single use (SPEC §12.5a).

- **Passkeys (security):** a signature could still be made after the vault
  locked — from keys unwrapped before the lock, while waiting for the touch,
  or by GetNextAssertion (which also worked from another USB channel and kept
  the other credentials' private keys in RAM until the next command). Keys are
  now unwrapped only right before signing, after the touch and with the vault
  checked; GetNextAssertion keeps credential IDs only and stays on its channel;
  key wipes can no longer be optimised away.

- Firmware update: after the button press installs an update, a new upload or
  download can no longer start before the restart and erase the image just
  installed; `state.update.phase` reads `restarting` meanwhile.
- Firmware update: the press installs only the image the phone showed; if a
  different one was staged in between, nothing is installed.
- Firmware update: redirects while fetching from GitHub must stay on `https://`.
- Firmware update: GitHub's request limit is reported as `rate_limited` ("try
  again in an hour"), and a cut-off answer as a network error instead of "no
  release found".
- Firmware update: a new image now stays on probation for ~15 s after boot
  before it is kept, so a crash shortly after start-up also rolls back; a
  flash storage error no longer rolls back (it cannot help), and an image with
  nothing to roll back to keeps running instead of being left unconfirmed.
- **Restore** (SPEC §5): "replace" is now atomic — a power cut or full storage
  mid-restore leaves the old vault or the restored one, never a half-empty mix.
  "Merge" no longer lets an older backup overwrite a newer local edit (newer
  `updated` wins; a replaced local password goes into history), and an id
  reused by a different account is kept as a separate entry. A wrong backup
  passphrase or bad file is reported before the button press is requested.

## [0.2.0] - 2026-10-08

Highlights:
- Updates over Wi-Fi: Settings → Firmware update (signed, button-gated, rolls back on failure)
- Password health and "change every password"
- Activity log, encrypted, with a warning after wrong passphrase attempts
- Accounts that delete themselves after their password is typed
- Bluetooth: pair a new device while another is connected


### Added

- **Firmware updates over the network** (SPEC §14): Settings → Firmware update
  checks the latest GitHub release and installs it, or takes a file. Images
  must be signed with the owner's key (Secure Boot V2 RSA-3072, checked by the
  running firmware, no eFuses), older versions are refused, installing needs a
  press, and a new image that fails its first boot is rolled back.
  `tools/sign_release.sh` and `tools/release.sh` sign and publish releases.

- **Delete after typing** (SPEC §16): an account can delete itself after its
  password is typed N times (1–99). Entry format 4 and backups carry
  `burnAfter`; the deletion is logged in Activity.

- **Change every password** (SPEC §13.1): Password health can start a
  "change every password" round after a possible leak; it lists every account
  whose password was not changed since, with progress, and nothing per entry is
  stored (the password history decides).

- **Activity log** (SPEC §15): Settings → Activity lists unlocks, wrong
  passphrase attempts, locks and their reason, typing, shown passwords, backups,
  restores and security changes — encrypted in the vault, at most 200 events,
  never a secret, and no way to clear it from a session. After an unlock the app
  warns when wrong passphrases were tried in between (`failedAttempts`).

- **Password health** (SPEC §13): Settings → Password health lists reused,
  could-be-stronger and year-old passwords. Keyra checks them on the device
  (`GET /api/health` returns entry ids and flags only), with the same strength
  estimate as the app's meter.

- Typing in any input language (SPEC §10.5): each host has a system (USB in
  Settings → Typing, each Bluetooth device in Settings → Bluetooth; guessed from
  the name at pairing). Windows hosts get Alt + keypad codes (Num Lock handled
  like Caps Lock); Mac/iOS hosts marked "in another language now" get Ctrl+Space
  before and after typing; Android gets a one-time hint. API: `osUsb`,
  `PUT /api/ble/bonds/{addr} {os}`, `switchLang` on `POST /api/type`.
- Keyboard layouts per output, Layout Doctor probe, layout-proof generator and
  auto-type sequences (SPEC §10.1–10.4, firmware and API).
- **Passkeys and security key over USB** ([docs/FIDO.md](docs/FIDO.md)). Keyra
  enumerates a second HID interface (FIDO usage page `0xF1D0`) next to the
  keyboard in both builds and speaks CTAPHID, CTAP 2.0 (MakeCredential,
  GetAssertion, GetNextAssertion, GetInfo, Reset, Selection; ES256; discoverable
  credentials, up to 50) and U2F/CTAP1. A short press while the LED double-blinks
  white approves, a long press refuses; requests wait up to 30 s for the vault to
  be unlocked. Credential keys are wrapped with a key derived from the vault's
  data key; passkey records are encrypted vault files. U2F attestation uses a
  per-device key made on first use with a self-signed certificate built on the
  device (NVS; new after factory reset). Self attestation, AAGUID
  `b722a2aa-5acc-4835-9c91-5fa93812679d`, not FIDO certified, no ClientPIN yet.
- Web app: **Settings → Passkeys** lists passkeys (site, account, date added)
  and deletes them (`GET /api/fido`, `DELETE /api/fido/{id}`).
- Host tests for the FIDO core (CBOR vectors, CTAPHID framing, MakeCredential/
  GetAssertion and U2F signatures checked with an independent verifier,
  malformed input) and `tools/fido_harness.py`, which runs python-fido2's
  client and server against the core through a fake HID link.
- Password generator on the device (SPEC §9.1): `POST /api/generate` uses the
  hardware RNG, uniform per character, class minimums by rejecting whole
  candidates, exact entropy. In the app: a **Generate** button in the vault's
  top bar (length 8–128 with slider and number, a–z / A–Z / 0–9 / symbols,
  minimum numbers and symbols, avoid look-alikes, strength in bits, settings
  remembered per browser) with **Type it**, **Type twice**, **Copy** and
  **Save** (new account, or update an account). The same generator is inline in
  Add/Edit and replaces the browser-side generator.
- Type any text (SPEC §9.2): `POST /api/type {text, repeat, separator}` arms
  free text like an entry action (`what:"text"`, `title:null`), up to 256
  printable ASCII characters, wiped from RAM after typing, cancel, expiry or
  lock. In the app: **⋯ → Type text…**.
- Password history (SPEC §9.3): up to 10 previous passwords with dates inside
  each encrypted entry; `GET /api/entries/{id}` returns `history`; the account
  sheet lists them (reveal, copy). Entry plaintext format 2 (format 1 still
  read, migrated on next write); backup file version 2 (version 1 still
  imports).

- Web app: **Scan QR from a photo** next to the 2FA field. Reads a plain
  `otpauth://totp/` QR in the browser (jsQR, Apache-2.0; photos are downscaled on
  a scratch canvas, never uploaded) and fills the key, plus the name and user name
  when empty.
- Web app: **Google Authenticator export** card in Import. Decodes
  `otpauth-migration://` QR codes (hand-written protobuf reader, several QR codes
  per export), previews the accounts, and adds them as new accounts or adds the
  key to existing accounts that match by name and user name.
- Unsupported codes (HOTP, MD5, digits other than 6 or 8, periods other than 30
  or 60 s) are rejected with a message, matching what the firmware can compute.
  The 2FA field now applies the same rules to pasted `otpauth://` links.
- The embedded web app grows from about 78 KB to about 133 KB gzipped (the QR
  decoder is inlined in the single file; home Wi-Fi screens add about 4 KB).
- Home Wi-Fi (optional): Keyra joins a WPA2/WPA3 home network so
  `http://keyra.local` opens from any device on it. Joining, changing and
  turning it off need a button press. Retries back off from 2 s to 5 min.
  "Keep Keyra's own Wi-Fi on" can be turned off: Keyra's Wi-Fi then switches
  off while the home network is connected and comes back after 60 s without it
  (30 s after power-up). Network picker with signal and lock icons, live status
  (connected, address, signal).
- Trusted browsers: the first unlock from each browser on the home network needs
  a button press; up to 8 are remembered (hashed) and can be removed in Settings.
- Clock from NTP while the home network is connected.
- API: `GET /api/wifi/scan`, `PUT /api/wifi/home`, `GET /api/trusted`,
  `DELETE /api/trusted/{id}`, `state.net`, settings `homeWifi` and `apMode`.
- Bluetooth LE keyboard (HID over GATT on NimBLE) with the same typing engine,
  Caps Lock handling and key-release guarantees as USB. Pairing needs a button
  press and stays open for 2 minutes (LED pulses cyan); LE Secure Connections
  only, Just Works, up to 4 bonded devices. Outside the window only bonded
  devices can connect.
- Settings `bleEnabled`, `output` (`auto`, `usb`, `ble`) and `bleConnect`;
  `auto` types over USB when plugged in, otherwise into the most recently used
  Bluetooth device.
- API: `GET /api/ble`, `POST /api/ble/pair` (presence op `ble_pair`),
  `DELETE /api/ble/bonds/{addr}`; `state.host.ble` and `state.host.output`;
  result code `no_host`.
- Bluetooth connects on demand by default (`bleConnect: "on_demand"`): idle
  Keyra neither advertises nor holds a link (iPhone and iPad hide their
  on-screen keyboard while a keyboard is connected); an armed action makes it
  advertise to its host alone, a press types once that host is connected, and
  the link ends about 20 s after typing. `"always"` keeps paired hosts connected.
- `POST /api/type` takes `target` (`"usb"` or a paired device); `state.host`
  gains `bleTarget` and `connecting`, `pending` gains `target`.
- Web app: Settings → Bluetooth (on/off, Type into, Connect, paired devices,
  Pair a new device); a Type-into picker in the account sheet, remembered per
  browser; Ready shows "Connecting to <device>…" and then where it will type.

### Changed

- USB: both descriptors gain the FIDO interface (interface 1; the dev build's
  CDC console moves to interfaces 2-3) and `bcdDevice` becomes 0x0110 (release)
  / 0x0111 (dev) so hosts do not reuse cached descriptors.

- The captive DNS answers only clients on Keyra's own Wi-Fi, and connectivity
  probes are answered only there.
- Factory reset also forgets every paired Bluetooth device.
- Locking the vault closes an open Bluetooth pairing window.

### Fixed

- Bluetooth: a new device can pair while another host is connected (in "always"
  mode it never could — Keyra stopped advertising whenever a host was linked).
  A second link slot takes the host being paired; once it has paired it takes
  over and the linked host is let go (SPEC §8.1).

- Vault QA round: an open account with a 2FA code no longer keeps the vault
  from auto-locking (code refreshes are not activity); "Try again" / "Copy
  instead" after a failed type act on the field that was typed, not Both;
  Both is off without a username; CSV import fits rows to the device limits
  and says what was shortened, left out or skipped; Add/Edit names the field
  that is too long; the generator's length box shows the length in use;
  Arabic/English counts, restore wording and passphrase label, recovery-key
  errors next to the key, mixed-direction notes, no wrong device on the Ready
  card for a moment.
- Cancelling a "press Keyra's button" screen only cleared the screen: the
  device kept the op and a later press still ran it (a cancelled factory
  reset erased the vault). `POST /api/presence/cancel {op, cancel}` withdraws
  it, with a secret token only the requester received in the 202 answer (so
  nobody else can free the slot and swap in their own request).
- Setup after a reload could confirm an earlier request with a different Wi‑Fi
  password and skip the reconnect screen; a refused request now shows an
  error instead of a press ring.
- Home Wi‑Fi with a wrong password said "Connecting…" forever: the device now
  reports why a join failed (wrong password / not found / failed).
- Settings: "Add to Home Screen" works on phones, the locked screen says when
  the computer was unplugged, an emptied device name returns, the offline
  banner names the real network, and Arabic layout fixes (system picker arrow,
  Latin names, long Bluetooth names, disabled pair row).
- Audit round (Bluetooth, firmware security, app ↔ firmware contract):
  - Security: nested-JSON crash from the network (cJSON depth 16), home-LAN
    spoofing of the AP to skip browser approval, slow-body stall of the web
    server, 10M-iteration backups, lost settings updates between tasks,
    sessions reviving after an internal lock.
  - Bluetooth: advertising stuck off after an early link loss, Caps Lock
    state lost on reconnect (now also waits for the host's LED report), a new
    action cutting a password mid-typing, keys stuck after a failed release.
  - App: imports split by size as well as count (no more network error with
    long notes), passphrase limits 10–128 and name limits in bytes as on the
    device, passphrase change shows the wait after wrong attempts, an invalid
    2FA key shows a message instead of retrying forever.
- Choosing a Bluetooth device's system and forgetting a device both failed
  with "Something went wrong": the app percent-encoded the address
  (`A4%3AC1…`) and the firmware matched paths literally. The app now sends the
  address as is, and the firmware decodes `%XX` before routing (malformed
  escapes and encoded `/` or NUL never match). The mock server does the same,
  so it can no longer hide this.
- Bluetooth dropped every 30 s: when a bonded host re-encrypts by itself,
  NimBLE reports the encryption (and the restored subscriptions) before the
  connection. Keyra then asked an already encrypted link to encrypt again, got
  no answer, and cut the link at the 30 s SMP timeout. Both events are now
  taken over by the connection, and an encrypted link is not asked again.
- Bluetooth on macOS: the Mac paired but never typed. The HID description and
  the input report's subscription are now readable/writable before encryption
  (macOS reads them during discovery and never retries), the report map
  declares Report ID 1, and SC-only mode (which demands MITM a display-less
  device cannot give) is off; keystrokes still go only to a bonded, encrypted
  host.

## [0.1.0] - Unreleased

First public release.

### Added

- Encrypted vault on the device: PBKDF2-HMAC-SHA256 key derivation calibrated to
  about 1.2 s, a wrapped random data key, and per-entry AES-256-GCM with
  authenticated entry IDs. Persisted unlock rate limiting.
- USB HID keyboard typing (US layout) triggered only by a physical button press,
  with Caps Lock handling and guaranteed key release.
- Phone-first web app (Preact, single gzipped file embedded in the firmware):
  onboarding, unlock, search, add/edit, password generator, TOTP codes,
  CSV import, encrypted backup and restore, settings. English and Arabic (RTL).
- Device Wi-Fi access point, `keyra.local` via mDNS, and connectivity-probe
  answers so phones join without a captive-portal sheet.
- RGB status LED and long-press to cancel or lock.
- Dev profile with a USB CDC log port and a 1200-baud reboot-to-ROM hook;
  release profile with HID only and logging off.
- Host test suite (vault, TOTP, key map, action state machine) and CI.

[Unreleased]: https://github.com/hasanalaaa/keyra/compare/v0.1.0...HEAD
[0.1.0]: https://github.com/hasanalaaa/keyra/releases/tag/v0.1.0
