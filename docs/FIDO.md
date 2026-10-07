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
| CTAP2 | `authenticatorGetInfo`, `MakeCredential`, `GetAssertion`, `GetNextAssertion`, `Reset`, `Selection` (0x0B). ES256 (P-256) only. Options `rk`, `up`, `uv`. `excludeList`, `allowList`. Zero-length `pinAuth` (the "touch to pick this key" probe) is answered `PIN_NOT_SET` after a touch. |
| CTAP1 / U2F | `REGISTER`, `AUTHENTICATE` (check-only, enforce, don't-enforce), `VERSION`. U2F credentials also work through CTAP2 (the WebAuthn `appid` extension). |
| Versions advertised | `U2F_V2`, `FIDO_2_0`. The CTAP 2.1 command set above is implemented, but `FIDO_2_1` is not claimed, because 2.1 makes `pinUvAuthToken` mandatory for authenticators with user verification and Keyra does not implement ClientPIN yet. |
| Extensions | None in v1 (`hmac-secret`, `credProtect`, `largeBlob` are not offered). |
| Attestation | **Self attestation** (`packed`, no certificate) for CTAP2. U2F registration needs a certificate, so Keyra signs it with a fixed key whose private half is published in this repository; it attests nothing. |
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
  unlocked Keyra can press its button. Lock it when you walk away. ClientPIN
  (with the master passphrase as PIN) is a later step.

### Reset

`authenticatorReset` deletes all discoverable credentials and makes every
non-discoverable credential unusable (a new wrapping key). It needs a button
press and, as the spec requires, is only accepted within 10 s of Keyra
powering up (`CTAP2_ERR_NOT_ALLOWED` otherwise). The vault's passwords are not
touched. A Keyra factory reset also destroys all FIDO credentials.

## Keys and storage

Nothing FIDO-related is stored in plaintext. All keys depend on the vault's
data encryption key (DEK), which only exists in RAM while the vault is
unlocked. A flash dump without the passphrase reveals no credential key.

- **Wrapping key** `Kwrap = HMAC-SHA256(DEK, "keyra/fido/v1/wrap" || salt)`,
  where `salt` is 16 random bytes kept in the vault (encrypted) and replaced by
  `authenticatorReset`.
- **Credential ID** (also the U2F key handle), 62 bytes:
  `0x01 | nonce[12] | AES-256-GCM(Kwrap, nonce, AAD = rpIdHash, privateKey[32] | flags[1]) | tag[16]`.
  The private key is random (hardware RNG). The AAD binds the credential to its
  relying party: presented to another RP it does not decrypt. `flags` bit 0
  marks a discoverable credential.
- **Discoverable credentials** are vault records `f/<id>.bin`, encrypted with
  the DEK like entries (AES-256-GCM, AAD `"keyra/f/v1/<id>"`). A record holds
  the RP ID, user handle, user name, display name, creation time and the
  credential ID (no separate private key). Deleting the record also revokes the
  credential: a resident credential ID is only accepted while its record exists.
- Passkeys are **not** in encrypted backups in v1. Restoring a backup on a new
  Keyra does not bring passkeys along; register a second key or keep another
  sign-in method on each account.

## Honest limits

- **Not FIDO certified**, no conformance run recorded, no attestation chain.
  Relying parties that require certified or attested authenticators (some
  enterprise policies) will refuse Keyra.
- **No secure element.** Keys are as safe as the vault: a flash dump plus a
  weak passphrase means the attacker gets your passkeys too. Flash encryption
  is optional (see [HARDWARE.md](HARDWARE.md)).
- **UV = unlocked**, as described above; no ClientPIN yet.
- **USB only.** Bluetooth FIDO is not supported by iOS, macOS, Android or
  ChromeOS, so Keyra does not offer it. Passkeys on a phone need a USB
  connection to the phone.
- **One credential type** (ES256). Sites that only accept EdDSA or RS256 will
  not work (rare).
- **If the vault is reset, every FIDO credential is gone.** Always keep a
  second way into important accounts.

## Threat model (FIDO part)

| Threat | Result |
|---|---|
| Phishing site asks for a credential of another site | The browser sends the phishing site's RP ID; the credential ID does not decrypt under its hash. Same protection as any security key. |
| Malware on the computer triggers requests | Nothing is signed without a button press; the LED shows the FIDO pattern so a press meant for typing can be told apart. Malware can still race a press the user makes for a FIDO prompt it caused. |
| Lost Keyra, locked | No signature possible; credential keys are encrypted under the DEK. |
| Lost Keyra, unlocked | Whoever holds it can sign in where you have passkeys, until auto-lock. |
| Flash dump | Offline passphrase guessing (PBKDF2, ≈1.2 s per guess on the device); same as the password vault. |
| Cloned credential | The global counter lets RPs that check it notice a clone only if both copies are used; Keyra cannot export keys, so a clone needs the flash and the passphrase. |

## Code map

- `firmware/components/keyra_fido/src/core/` — plain C++, host-tested:
  `cbor` (encoder/decoder), `ctaphid` (framing), `ctap` (CTAP2 + U2F),
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
