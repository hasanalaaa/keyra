# Changelog

All notable changes to Keyra are documented here. The format follows
[Keep a Changelog](https://keepachangelog.com/en/1.1.0/) and the project uses
[Semantic Versioning](https://semver.org/).

## [Unreleased]

## [0.4.1] - 2026-10-12

Highlights:
- One press of Keyra's button approves one request: this account, this browser, once
- "Show, then edit" is still one press; another account asks again
- Every request waiting for the button has its own id, so apps follow and cancel exactly theirs
- An activity log that can no longer be read is kept aside, never written over

### Added

- **Request ids (SPEC §12.6a).** Every answer that starts a wait for the
  button carries `requestId` (128 random bits, hex). `GET /api/state?requestId=`,
  `GET /api/agent/status?requestId=`, and the cancels (`/api/type/cancel`,
  `/api/agent/cancel`, `/api/presence/cancel`, with `{requestId}` in the body)
  act on exactly that request and only for its owner. A request replaced by a
  newer one of the same owner reports `code: "superseded"`, and a cancel with
  its id answers 409 `superseded` without touching the newer one. Without a
  `requestId` everything works as before, so older extensions, Android apps
  and agents keep working. The web app, the extension, the Android app and
  keyra-mcp now follow their own request by its id.

### Changed

- **One press, one request (SPEC §12.5b).** A press of Keyra's button no
  longer opens 60 seconds of reading every account's secrets. It approves
  exactly the one request that asked for it — this account (and this version
  of it), this session — and that request uses it up, within 30 seconds. A
  reveal for another account, from another browser, or after the account was
  edited (409 `entry_changed`) needs its own press. Backups, recovery-key
  creation and removal (now two separate approvals), access tokens and NFC
  tags (bound to the exact request body) follow the same rule.
  `state.graceMs` stays and is always 0.
- **Activity log (SPEC §15).** A log that can no longer be read is kept
  aside unchanged (`activity.bad`) instead of being written over, and a new
  one starts; `GET /api/activity` says `aside: true` and the app shows a note.
- **Web app.** One press shows the whole account (password, 2FA, notes, old
  passwords) until its sheet closes; "show, then edit" stays one press; the
  next opening asks again. Password health keeps the weak and reused it was
  shown when "change every password" is switched.

## [0.4.0] - 2026-10-12

Highlights:
- Up to 5000 accounts on boards with 16 MB of flash
- Unlocking a large vault takes about a second instead of minutes
- The vault moves once to the unused flash, safely; a power cut never loses it
- After the move, older firmware cannot be installed (it could not see the vault)

### Added

- **5000 accounts on boards with 16 MB of flash.** The vault moves once, at
  the first start after this firmware has passed its probation, from the
  `vault` partition to a second LittleFS in the unused upper 8 MB of the flash
  (registered at run time; the partition table and bootloader do not change).
  There, accounts are packed into 128 bucket files, so unlocking reads 128
  files instead of one per account: about 0.2–0.8 s for 1000 typical
  accounts (was 1–3 minutes) and 0.5–1.8 s for 5000 (estimated from flash
  counts, see docs/research/CAPACITY.md). The move copies the encrypted files
  as they are (no passphrase needed), reads every byte back, and only then
  commits with one atomic write; a power cut before that leaves the vault
  where it was, untouched. Afterwards the old partition is erased and holds
  only a marker that any older firmware reads as `corrupt`. 8 MB boards keep
  today's layout and the limit of 1000. `GET /api/state` reports `maxEntries`.
  On the owner's board the move took about 6 seconds (147 steps).

### Changed

- **No going back below the moving version.** Once the vault has moved, the
  updater refuses any firmware older than the one that moved it (`downgrade`),
  since an older firmware cannot see the vault there.

## [0.3.3] - 2026-10-11

Highlights:
- Backups are written and restored piece by piece, so large vaults no longer run out of memory
- A full storage now says "full" instead of a general storage error
- Merging a backup into a large vault is much faster

### Changed

- **Backup and restore stream.** `POST /api/backup` encrypts and sends the
  file in chunks (PSA multipart AES-GCM) and `POST /api/restore` decrypts the
  request as it arrives, so neither holds the whole file, its plaintext and
  every account at once. The file format is unchanged (v3, byte for byte):
  older backups restore and new ones open in older firmware. Nothing is kept
  before the file's authentication tag checks, and a wrong passphrase is always
  reported as such. A restore request may now be up to 8 MB.
- **Merging is no longer quadratic.** A merge looks accounts up by id and by
  title, username and address in hash maps instead of scanning the vault for
  each incoming account.

### Fixed

- **Out of space is reported as `507 full`.** Saving, importing and restoring
  on a full storage now answer `full` instead of `500 storage`.

## [0.3.2] - 2026-10-11

Highlights:
- The web app opens faster: 109.5 KB instead of 189.5 KB
- Thousands of accounts scroll and search smoothly
- Unlocking reads half as much flash; the account list no longer copies the whole vault
- Stricter browser security policy for the web app
- Android app: clear message for every Keyra answer, better Arabic, TalkBack

### Changed

- **The web app opens with less and loads the rest when it is used.** Keyra
  now serves the app as separate hashed files (`/assets/*`, packed into
  `assets.pack` in the firmware, cached by the browser for good): opening the
  app and the vault list downloads 109.5 KB instead of 189.5 KB, and
  onboarding, add/edit, import, backup, Settings and each of its sheets, the
  QR reader and the recovery kit load on first use. A screen that cannot load
  says so and offers a retry. The byte budget is now 130 KB for the initial
  load and 400 KB in all (DESIGN Appendix A).
- **Stricter Content-Security-Policy.** With nothing inline left in the app,
  `'unsafe-inline'` is gone from `script-src` and `style-src`, and the policy
  adds `object-src 'none'; base-uri 'none'; frame-ancestors 'none'` (the NFC
  tap page keeps its inline script and style).
- **Thousands of accounts stay smooth.** Long account lists draw only the rows
  near the screen, and search normalizes each account once instead of on
  every keystroke and comparison. With 5000 accounts the list draws under 200
  rows and each search keystroke answers in the next frame. The mock takes
  `MOCK_ENTRIES=N` to try it.

### Fixed

- **Large vaults use far less memory and flash time.** `GET /api/entries` and
  `GET /api/agent/entries` build their answer while the vault walks its
  accounts one at a time instead of copying every account (passwords and notes
  included) first, and reading an account file no longer looks its path up
  twice, which halves the flash reads of an unlock with many accounts
  (docs/research/CAPACITY.md has the measurements and the storage plan).

### Added

- **Android app: every answer from Keyra has its own message** (changed
  account, nothing waiting, newer format, rate limit with the wait), Arabic
  copy reviewed, TalkBack labels, 48dp touch targets and large-font support.
- **docs/HARDWARE-TESTS.md**: a checklist for testing on real devices.

## [0.3.1] - 2026-10-11

Highlights:
- Passkey counters never go back, even after a restore or a settings wipe
- An old button press never approves a newer request
- Revoking a browser also withdraws what it left waiting for the button
- Nothing is typed if the account changed after it was armed
- Notes stay hidden until you press the button (with reveal protection)

### Changed

- **The web app shows hidden notes and the new typing result.** An account's
  notes appear as a masked row with a show button until the reveal press, and
  a press that typed nothing because the account was edited since the request
  (`entry_changed`) says so in the web app and the extension, in Arabic and
  English.
- **The web app explains an erased settings storage.** Before the unlock, a
  banner says Keyra had to erase its settings storage, what is safe, what is
  lost and that unlocking is delayed (`state.nvsReset`); the unlock that logs
  it says so once, and the activity log names `nvs_reset`. The token and tag
  screens say when their record was saved by a newer Keyra or is damaged, and
  offer nothing that would write over it. Cancelling an action that is not
  yours says nothing of yours is waiting.

### Fixed

- **A press made before a passkey request no longer approves it.** A press
  that waited in the button queue while Keyra was busy could count as the
  security-key touch for a request that started after it. Every FIDO prompt
  now remembers when it began and ignores older presses.
- **Ending a session ends its pending work.** Removing a trusted browser, or a
  fifth browser pushing out the oldest session, now withdraws what that session
  left waiting for the button; a queued slow request (restore, backup,
  passphrase change) is refused with 401 `locked` if its session ended or the
  vault locked before it ran.
- **Auto-type sequences keep their owner at every `{PRESS}`.** After the first
  part, the session that armed a sequence could no longer cancel or replace it;
  now every part belongs to it, and another session still gets 409 `busy`.
- **Typing types the account you armed.** An edit to the account (from any
  session) while the press is awaited now makes the press type nothing and
  report `entry_changed`, instead of typing the new values.
- **Notes need the reveal press.** With "Protect reveal" on, an account's notes
  are no longer sent before the press; `hasNotes` says whether there are any.
- **One session can no longer cancel another's typing.** `POST /api/type/cancel`
  withdrew whatever action was waiting, whoever armed it; it now withdraws the
  calling session's own action or one an access token or tap tag armed, and
  answers 409 `not_cancelled` (shown in the app) when nothing of that kind waits.
- **Restored passkeys never sign with an old counter.** A restore with passkeys
  now reserves the signature counter past the backup's before it writes any of
  them, and fails with nothing changed if it cannot (it used to write first and
  only log a failed raise).
- **A lost settings storage (NVS) no longer resets the passkey counter or the
  unlock delay.** The counter carries on from a reservation kept in the vault;
  the unlock delay starts at its longest instead of zero. Erasing NVS at boot
  is no longer silent: the app is told (`nvsReset` in `/api/state` and the
  unlock answer) and the activity log records it.
- **Access tokens and NFC tags saved by a newer firmware are kept.** A record
  this firmware cannot read is reported (`unsupported_format` / `corrupt`)
  instead of being treated as empty and overwritten by the next new token or tag.
- **Delete after typing counts before typing.** The use is saved (or the entry
  deleted on its last one) before the first key; if that fails nothing is typed.
- **A sequence's password is wiped on every path**, also when its 2FA code
  cannot be made.

## [0.3.0] - 2026-10-11

Highlights:
- Bluetooth: the iPhone stays paired and reconnects at once; Caps Lock is known
- Keyra Companion browser extension: fill by typing, save and update prompts
- Android app and access tokens for apps and AI agents (Agent Gate)
- NFC tap tags; security-key PIN, hmac-secret; passkeys in backups
- Security fixes from a full review of the device, app and extension

### Added

- **Keyra Companion, the browser extension** (SPEC §9.4, [extension/README.md](extension/README.md)):
  Manifest V3 for Chrome, Edge, Brave, Opera, Vivaldi and Firefox. A Keyra key
  in login fields lists the logins for that site (`POST /api/agent/match`, the
  shared host rule of [HOST-MATCH.md](docs/research/HOST-MATCH.md)), focuses
  the right field and shows the press card with the countdown and the result;
  "Other login…" on another site warns with both hosts and sends `anyHost`.
  Sign-up and change-password forms get a strong password from Keyra; after a
  form is sent it offers **Save to Keyra?** or **Update the password for …?**
  (a press each; a plain sign-in with a known login asks nothing). Toolbar
  popup with connection state, the site's logins, search with "Type into this
  page", the generator and settings. Connects with one press: `#/connect` in
  the web app creates a token of the new kind **extension** and hands it to
  the extension's tab by `postMessage` (nonce-checked, not kept by the web
  app). Arabic and English, light and dark; unit tests and a Chromium
  end-to-end run against the mock.
- Web app: **#/connect** pairing screen; **Browser** kind in Settings → Apps and
  agents; the activity log and the Ready pill name the other site when a login
  was typed there.
- Mock: kind `extension`, `/api/agent/match`, `host`/`anyHost`/`host_mismatch`
  on type, `replace` and `mode` on save, extension origins on agent routes —
  matching the firmware's rules.
- **Keyra Companion on the device** (firmware, SPEC §9.4 and §17,
  [HOST-MATCH.md](docs/research/HOST-MATCH.md)): a third access-token kind,
  `extension` (kind byte 2 in `tokens.bin`; existing agent and app tokens read
  unchanged), with the app's rights plus `POST /api/agent/match`, which offers
  the logins in scope for a page's host (at most 20; titles and hosts only,
  `sameUser` by exact username, never a password comparison). `/api/agent/*`
  accepts `chrome-extension://`, `moz-extension://` and
  `safari-web-extension://` origins; session routes still refuse them. An
  extension must name the page's host when it arms typing: a login for another
  site is refused (409 `host_mismatch`) unless the user chose it (`anyHost`),
  and then the page's host is shown with the pending item and logged.
  `POST /api/agent/save` can update an existing login (`replace`, any token
  that may save): after the press the old password moves to the entry's
  history; the answer says `mode: "create"|"update"`. The host rule is one
  table shared with the extension and the Android app; the firmware's host
  test reads it straight from the doc.
- **NFC tap tags** (SPEC §18, [NFC-TAGS.md](docs/research/NFC-TAGS.md)):
  Settings → **NFC tags** makes a sticker that gets one account ready to type
  when your phone taps it on Keyra's network — you still press Keyra's button,
  and the tag never shows a secret. *Simple* tags work with any NTAG213/215/216
  (write the link with NFC Tools or NXP TagWriter; copyable by design, Keyra
  keeps only a hash). *Secure* tags use an NTAG 424 DNA with SUN messages:
  Keyra checks the AES-CMAC (NXP AN12196), binds the chip's UID and refuses
  replayed counters; the keys and the SDM offsets for TagWriter are shown once.
  Tags live in a new vault record sealed with the data key (`tags.bin`, at most
  16), need a press to create, are revoked without one, are rate limited and
  logged. The tag's link opens a small page that arms nothing by itself (link
  previews are safe) and follows the press to "Typed".

- **Android app** ([android/README.md](android/README.md)): uses an "app" access
  token (SPEC §17) to list logins, arm typing (username, password, both, 2FA
  code) with a countdown and the result, generate passwords on Keyra, and an
  autofill service whose suggestions hold no value (choosing one arms Keyra,
  the button press types) plus "Save to Keyra" (the press saves). Stores only
  the address, the Keystore-encrypted token and remembered app/site choices;
  English and Arabic, light and dark; no accessibility service, no Play
  Services.
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

- **Keyra Companion security review:** stays out of Keyra's own web app at any address (it
  offered to save the passphrase); clickjacking guards on its in-page buttons; the token is
  sealed and out of content scripts' reach; save is offered only after a real submit; a failed
  cancel or an unconfirmed pairing says so (the web app revokes the unconfirmed token); `$` in
  "Symbols !@#$"; accurate privacy note. Web: activity titles split at the last arrow.
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

### Security

- **Security key: no signature without a press** ([docs/FIDO.md](docs/FIDO.md#user-presence-and-user-verification)).
  A CTAP2 GetAssertion with `up: false` used to be signed by the credential
  (UP clear, UV set) and could return `hmac-secret` outputs, and U2F
  AUTHENTICATE with "don't enforce presence" (`0x08`) signed without asking:
  software on the computer could get signatures from an unlocked Keyra
  without the owner. Now `up: false` with `hmac-secret` is refused
  (`CTAP2_ERR_UNSUPPORTED_OPTION`); `up: false` alone (the browsers' silent
  "pre-flight") still finds the credential but the answer is signed by a
  throwaway key, with no flags and counter 0, so no site can accept it; U2F
  `0x08` needs a press like `0x03`.
- **Password health is no longer a password oracle** (SPEC §13). With
  `protectReveal` on, a stolen session could save a guess in one entry and ask
  `GET /api/health` whether it now "reuses" another account's password. The
  weak and reused lists now need the reveal press (`revealed: false` and
  empty lists outside the grace); `POST /api/health/reveal` asks for it.
  Counts, old passwords and "change every password" are unchanged.
- **The activity log cannot be flushed** (SPEC §15). A stolen session turning
  "change every password" on and off, a token alternating list and generate
  or arming and cancelling, or wrong tag taps could push every earlier event
  (unlocks, reveals, token creation) out of the 200. Those repeatable
  events are now counted on one line within the newest 16 (not only the
  newest one), keep at most 40 lines, and are the first dropped when room
  is needed. `activity.bin` keeps its format.
- **A queued press approves nothing armed after it.** Presses made while the
  button task was busy with a long confirmed action (a replacing restore) or
  while a sequence part was typing waited in the queue and could approve,
  cancel or lock whatever was armed next — also a security-key touch. Each
  press now carries its time and is ignored when it is older than what waits
  for it or than the end of the last confirmed action.
- **NFC tag taps are limited before they are checked** (SPEC §18). A wrong
  secret or MAC for a known tag was verified and written to the activity log
  before any limit, and a replayed tap was never limited, so anyone on the
  network could make Keyra check and write to flash without end. The
  per-tag budget is now spent first, for every tap.
- **Only a signed-in browser sets the clock** (SPEC §5). Any request,
  including a page load from any device on the network, could set Keyra's
  clock through `X-Keyra-Time` while NTP had not set it, shifting 2FA codes
  and activity times. The header is now taken only with a live session, at
  setup of an empty Keyra, or from an unlock whose passphrase was right.

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

[Unreleased]: https://github.com/hasanalaaa/keyra/compare/v0.3.1...HEAD
[0.3.1]: https://github.com/hasanalaaa/keyra/compare/v0.3.0...v0.3.1
[0.3.0]: https://github.com/hasanalaaa/keyra/compare/v0.2.0...v0.3.0
[0.2.0]: https://github.com/hasanalaaa/keyra/compare/v0.1.0...v0.2.0
[0.1.0]: https://github.com/hasanalaaa/keyra/releases/tag/v0.1.0
