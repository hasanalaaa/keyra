# Keyra as a FIDO2 / passkey security key (USB)

Status: v1, USB only. **Not FIDO certified.** Not a YubiKey replacement (see
[Honest limits](#honest-limits)).

Keyra adds a second USB HID interface (usage page `0xF1D0`, 64-byte reports)
next to its keyboard and speaks CTAPHID. Browsers and operating systems then
see it as a security key: it can create and use **passkeys** (discoverable
credentials) and act as a **second factor** (non-discoverable credentials,
including legacy U2F/CTAP1 sites).

## How a sign-in looks

1. Keyra is plugged in by USB and **unlocked** (from the phone, as usual).
2. The website asks for a security key / passkey.
3. Keyra's LED shows the **FIDO pattern** (white double-blink) and the browser
   says "touch your security key".
4. Press Keyra's button within 30 s. A long press refuses the request.

If Keyra is locked when a request arrives, it waits up to 30 s for you to
unlock it on the phone (the LED shows the FIDO pattern), then continues. After
that the request fails with `CTAP2_ERR_OPERATION_DENIED`, and U2F requests are
answered "conditions not satisfied" so the browser keeps retrying.

## What is implemented

| Area | Support |
|---|---|
| Transport | USB HID (CTAPHID): INIT, PING, MSG, CBOR, CANCEL, WINK (blinks the LED), KEEPALIVE (`PROCESSING` / `UP_NEEDED` every 100 ms), ERROR. Channel allocation, message reassembly up to 7609 bytes, 750 ms continuation timeout, `CHANNEL_BUSY` for other channels while one request runs. No LOCK. |
| CTAP2 | `authenticatorGetInfo`, `MakeCredential`, `GetAssertion`, `GetNextAssertion`, `ClientPIN` (0x06), `Reset`, `Selection` (0x0B). ES256 (P-256) only. Options `rk`, `up`, `uv` (only while no PIN is set), `clientPin`. `excludeList`, `allowList`. Zero-length `pinAuth` (the "touch to pick this key" probe) is answered after a touch: `PIN_NOT_SET`, or `PIN_INVALID` when a PIN is set. |
| ClientPIN | Subcommands `getPINRetries`, `getKeyAgreement`, `setPIN`, `changePIN`, `getPINToken` with PIN/UV auth protocols **2 and 1** (`pinUvAuthProtocols: [2, 1]`). `pinAuth`/`pinUvAuthParam` on MakeCredential and GetAssertion. The 2.1 subcommands (`getPinUvAuthTokenUsing…WithPermissions`) answer `INVALID_SUBCOMMAND`. See [ClientPIN](#clientpin). |
| CTAP1 / U2F | `REGISTER`, `AUTHENTICATE` (check-only, enforce, don't-enforce), `VERSION`. U2F credentials also work through CTAP2 (the WebAuthn `appid` extension). |
| Versions advertised | `U2F_V2`, `FIDO_2_0`. `FIDO_2_1` is not claimed: 2.1 also requires PIN token permissions and RP binding (subcommand 9), credential management and more that Keyra does not have. The 2.1 getInfo fields `maxCredentialCountInList` (8) and `maxCredentialIdLength` (64) are sent; 2.0 platforms ignore them. |
| Extensions | `hmac-secret` and `credProtect` (levels 1-3). `largeBlob`, `credBlob`, `minPinLength` are not offered. |
| Attestation | **Self attestation** (`packed`, no certificate) for CTAP2. U2F registration needs a certificate, so each Keyra makes its own P-256 attestation key on first use (hardware RNG) and a self-signed X.509 certificate for it, both kept in NVS and replaced by a factory reset. It identifies only "this Keyra" and is not certified. |
| AAGUID | `b722a2aa-5acc-4835-9c91-5fa93812679d` (random, fixed for every Keyra). |
| Discoverable credentials | Up to **50**, listed and deletable in the web app (Settings → Passkeys). |
| Signature counter | One global counter in NVS, incremented before every signature. It never goes backwards, also not across a factory reset. |

### User presence and user verification

- **User presence (UP)** is a short press of Keyra's button while the LED
  shows the FIDO pattern. While a FIDO request waits, the button belongs to it
  (a type action stays armed and runs on the next press). 30 s timeout →
  `CTAP2_ERR_USER_ACTION_TIMEOUT`. A browser cancel stops the wait at once
  (`CTAP2_ERR_KEEPALIVE_CANCEL`).
- **User verification (UV)** is "Keyra is unlocked": somebody entered the
  master passphrase on the phone within the auto-lock window (default 15 min).
  Keyra reports `uv: true` in `getInfo` and sets the UV flag on every signature,
  because no signature is ever made while the vault is locked. This is weaker
  than a PIN or fingerprint checked *for each* request: anyone who holds an
  unlocked Keyra can press its button. Lock it when you walk away.
- **With a security key PIN set** (below), only the PIN verifies: `getInfo`
  drops `uv` and says `clientPin: true`, the UV flag is set only when the
  request carries a valid `pinAuth`, MakeCredential without one is
  `PIN_REQUIRED` (CTAP 2.0), and an assertion without one has UV = 0 and
  leaves out the user's name and display name. The vault must still be
  unlocked for anything to be signed.

### ClientPIN

The **security key PIN** is its own secret, set and changed from the computer
(Chrome → Settings → Privacy and security → Manage security keys, Windows →
Settings → Accounts → Sign-in options → Security key, macOS/Chrome prompts).
It is deliberately **not** the master passphrase: a stored PIN hash next to
the vault would give anyone with a flash dump a fast way to test passphrases.

- Stored in the vault as `fidopin.bin` (AES-256-GCM with the DEK, AAD
  `"keyra/fidopin/v1"`): `u8 version=1 | u8 retries | LEFT(SHA-256(PIN), 16)`.
  Only whether the file exists is readable while locked (getInfo needs it);
  checking or changing the PIN needs the vault unlocked, so a ClientPIN
  request on a locked Keyra waits for the unlock like any other request.
- PIN rules: 4-63 bytes, at least 4 Unicode code points (CTAP 2.1).
- **Retries: 8.** The counter is decremented and written *before* the PIN is
  compared, so pulling the plug mid-check cannot buy a free guess. A right
  PIN resets it to 8. Three wrong PINs in a row answer `PIN_AUTH_BLOCKED`
  until Keyra is power-cycled; at 0 the PIN is `PIN_BLOCKED` and only
  `authenticatorReset` clears it, which also deletes every passkey (restore
  them from a backup). Every wrong PIN also replaces the key agreement key.
- **Protocols 1 and 2** (CTAP 2.1 §6.5.6/§6.5.7): ECDH P-256 with a key Keyra
  makes on first use (and again after a wrong PIN and on reset); protocol 1
  derives SHA-256(Z) and uses AES-256-CBC with a zero IV and 16-byte HMACs;
  protocol 2 derives separate HMAC and AES keys with HKDF-SHA-256, a random IV
  and full 32-byte HMACs.
- **PIN token**: 32 random bytes from `getPINToken`, replaced by each new one,
  and forgotten when the vault locks, on a PIN change, on reset and at
  power-off. It has no permissions or RP binding (CTAP 2.0).
- The web app shows whether a PIN is set and how many tries are left
  (`GET /api/fido`); it cannot set or remove the PIN.
- **Not in backups.** Backup format v3 has no place for it, and a PIN with a
  retry count belongs to one device. After restoring on a new Keyra, set a
  PIN there.

### Extensions

- **credProtect** (CTAP 2.1 §12.1): the level a site asks for at registration
  is kept in the credential ID's flags byte (bits 2-3; IDs from before have 0
  there and count as level 1) and returned in the attested extensions.
  Level 2 credentials are not found by a discoverable request without user
  verification; level 3 need user verification always and are never usable
  over U2F. While no PIN is set, every request counts as verified (vault
  unlocked), so all levels work as before.
- **hmac-secret** (CTAP 2.1 §12.5): requested at registration it sets flag
  bit 1 in the credential ID and makes `hmac-secret: true`. At GetAssertion
  Keyra checks `saltAuth`, decrypts one or two salts and returns
  `HMAC-SHA-256(CredRandom, salt)` encrypted under the shared secret. CredRandom
  is not stored: it is `HMAC-SHA-256(Kwrap, "keyra/fido/v1/hmac-secret" || uv
  || credentialId)` with `uv` 0 or 1, using the wrapping key the ID opens
  under, so the same secrets come back after a backup is restored on another
  Keyra, and requests with and without user verification get different
  secrets as the spec requires.

### Reset

`authenticatorReset` deletes all discoverable credentials, makes every
non-discoverable credential unusable (a new wrapping key) and removes the
security key PIN. It needs a button
press and, as the spec requires, is only accepted within 10 s of Keyra
powering up (`CTAP2_ERR_NOT_ALLOWED` otherwise). The vault's passwords are not
touched. A Keyra factory reset also destroys all FIDO credentials.

## Keys and storage

Nothing FIDO-related is stored in plaintext. All keys depend on the vault's
data encryption key (DEK), which only exists in RAM while the vault is
unlocked. A flash dump without the passphrase reveals no credential key.

- **Wrapping key** `Kwrap = HMAC-SHA256(DEK, "keyra/fido/v1/wrap" || salt)`,
  where `salt` is 16 random bytes kept in the vault (encrypted) and replaced by
  `authenticatorReset`. After a backup from another Keyra is restored, the vault
  holds a list of up to 4 wrapping keys (`fido.bin` v2, encrypted with the DEK):
  this Keyra's own key first, used for every new credential, then the restored
  ones. A credential ID is tried against each (AES-GCM fails cleanly on a wrong key).
- **Credential ID** (also the U2F key handle), 62 bytes:
  `0x01 | nonce[12] | AES-256-GCM(Kwrap, nonce, AAD = rpIdHash, privateKey[32] | flags[1]) | tag[16]`.
  The private key is random (hardware RNG). The AAD binds the credential to its
  relying party: presented to another RP it does not decrypt. `flags` bit 0
  marks a discoverable credential, bit 1 hmac-secret, bits 2-3 the
  credProtect level (0 = 1); bits 4-7 are zero. The format is unchanged, so
  every ID made before keeps working.
- **U2F attestation key** (per device, NVS, not vault-wrapped): it only signs
  registration statements, never authenticates you, so it needs no passphrase
  protection. Its certificate is a minimal X.509 v1, CN "Keyra U2F self
  attestation", valid 2026-01-01 to 9999-12-31, built on the device
  (`core/attest.cpp`). No private key is shipped in the firmware or the repo.
- **Discoverable credentials** are vault records `f/<id>.bin`, encrypted with
  the DEK like entries (AES-256-GCM, AAD `"keyra/f/v1/<id>"`). A record holds
  the RP ID, user handle, user name, display name, creation time and the
  credential ID (no separate private key). Deleting the record also revokes the
  credential: a resident credential ID is only accepted while its record exists.
- **Backups carry the passkeys** (design: [PASSKEY-BACKUP.md](research/PASSKEY-BACKUP.md)):
  the wrapping keys, the discoverable-credential records and the signature
  counter go into the encrypted backup while Settings → "Passkeys in backups"
  (`passkeysInBackup`, default on) is on; turning it back on needs a button
  press. Restoring on a new Keyra makes both discoverable and non-discoverable
  (U2F, `rk:false`) credentials work there: `merge` adds them next to the ones
  it has, `replace` makes them the backup's (a backup without passkeys leaves
  them alone). A restore that would pass 4 wrapping keys or 50 passkeys is
  refused before anything changes. The signature counter is raised to at least
  the backup's + 1000. Anyone with the backup file and its passphrase can use
  these passkeys, just as they can read every password in it.

## Honest limits

- **Not FIDO certified**, no conformance run recorded, no attestation chain.
  Relying parties that require certified or attested authenticators (some
  enterprise policies) will refuse Keyra.
- **No secure element.** Keys are as safe as the vault: a flash dump plus a
  weak passphrase means the attacker gets your passkeys too. Flash encryption
  is optional (see [HARDWARE.md](HARDWARE.md)).
- **UV = unlocked** unless you set a security key PIN, as described above.
  The PIN token has no 2.1 permissions; `FIDO_2_1` is not claimed.
- **A blocked PIN costs your passkeys**: only `authenticatorReset` clears it
  (within 10 s of power-up), and that deletes them. Keep a backup.
- **hmac-secret, PIN protocols and ECDH were checked on the host only** (spec
  derivations, OpenSSL as the platform); the device runs the same core over
  PSA crypto, not yet tried against a real browser or `systemd-cryptenroll`.
- **USB only.** Bluetooth FIDO is not supported by iOS, macOS, Android or
  ChromeOS, so Keyra does not offer it. Passkeys on a phone need a USB
  connection to the phone.
- **One credential type** (ES256). Sites that only accept EdDSA or RS256 will
  not work (rare).
- **If the vault is reset, every FIDO credential is gone** unless it is in a
  backup. Always keep a second way into important accounts.
- **A restored passkey is a copy.** Restoring while the old Keyra still exists
  leaves two working copies; sites that track the counter may flag the old one
  once the new one is used. Erase or lock away the old Keyra.
- **Backups are snapshots.** Passkeys created after the backup are not in it,
  as for passwords.
- **The U2F attestation key is per device** and not backed up. It only signs
  registrations, so existing sign-ins are unaffected.
- **Flags stay BE = 0 / BS = 0.** WebAuthn forbids changing them after
  registration; a restored passkey behaves like the same security key, moved.

## Threat model (FIDO part)

| Threat | Result |
|---|---|
| Phishing site asks for a credential of another site | The browser sends the phishing site's RP ID; the credential ID does not decrypt under its hash. Same protection as any security key. |
| Malware on the computer triggers requests | Nothing is signed without a button press; the LED shows the FIDO pattern so a press meant for typing can be told apart. Malware can still race a press the user makes for a FIDO prompt it caused. |
| Lost Keyra, locked | No signature possible; credential keys are encrypted under the DEK. |
| Lost Keyra, unlocked | Whoever holds it can sign in where you have passkeys, until auto-lock. With a security key PIN set, sites that require user verification also need the PIN (8 tries, then blocked). |
| Flash dump | Offline passphrase guessing (PBKDF2, ≈1.2 s per guess on the device); same as the password vault. |
| Cloned credential | The global counter lets RPs that check it notice a clone only if both copies are used; Keyra cannot export keys, so a clone needs the flash and the passphrase. |

## Code map

- `firmware/components/keyra_fido/src/core/` — plain C++, host-tested:
  `cbor` (encoder/decoder), `ctaphid` (framing), `ctap` (CTAP2 + U2F),
  `ctap_pin` + `pin` (ClientPIN, PIN/UV auth protocols, hmac-secret exchange),
  `cred` (credential ID wrapping, resident record codec), `touch` (button gate),
  `device` (packet loop, keepalive while waiting).
- `firmware/components/keyra_fido/src/esp/` — TinyUSB glue, PSA crypto, NVS
  counter, vault-backed store, the `fido` task.
- `firmware/components/keyra_fido/host_test/` — known-answer and negative
  tests; `harness.cpp` + `tools/fido_harness.py` drive the core with
  [python-fido2](https://github.com/Yubico/python-fido2) through a fake HID
  link.

Design references: the CTAP 2.1 specification, LionKey (MIT) and Solo 1
(Apache-2.0/MIT) for behaviour. No code was copied from them, and nothing from
Pico-Fido (AGPL).
