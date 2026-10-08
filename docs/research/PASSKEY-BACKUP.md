# Passkeys in backups — design note

Written 2026-10-08 (ROADMAP wave 4, item 3). Status: decided, being built.

## Problem

A backup restores passwords but no passkeys (FIDO.md "Keys and storage"). A
lost or broken Keyra takes every passkey with it, and the sites that only had
a passkey are then locked. Two kinds of credential are affected:

- **Discoverable** (resident): vault records `f/<id>.bin` holding the RP ID,
  user fields and the credential ID.
- **Non-discoverable** (U2F key handles and CTAP2 `rk:false`): nothing is
  stored on Keyra; the site keeps the credential ID, which is the private key
  sealed under `Kwrap`.

Copying the records is not enough: every credential ID is sealed under
`Kwrap = HMAC-SHA256(DEK, label || salt)`, and a new Keyra has a different DEK.
The new Keyra must hold the old `Kwrap` itself.

## Decision

1. **The backup carries the wrap key(s) and the passkey records.** Anyone
   holding a backup file and its passphrase can then sign in with those
   passkeys. That person already has every password in the same file, so the
   added risk is small, and losing all passkeys with the device is the
   larger, everyday risk. Passkeys are included by default; `POST /api/backup
   {passkeys:false}` and a switch on the Backup screen leave them out.
2. **Wrap keys become a list.** `fido.bin` v2 = AES-256-GCM(DEK, AAD
   `"keyra/fido/v2/keys"`) over `n | n × 32-byte keys`, at most 4, the first
   being the one new credentials use. A credential ID is tried against each
   key (GCM authenticates, so a wrong key fails cleanly). v1 files (the salt)
   are still read: the key is derived as today, and the file is rewritten as
   v2 only when a restore adds a key. `authenticatorReset` and a factory reset
   still delete the file (a fresh single key next time).
3. **Backup format v3.** The encrypted plaintext becomes an object
   `{"entries":[…], "passkeys":{"keys":["<b64 32>",…], "records":["<b64>",…],
   "counter":n}}`. v1/v2 (a bare array) are still imported. Firmware older than
   this refuses v3 with "unsupported version" rather than half-importing it.
   `passkeys` is absent when the owner leaves passkeys out.
4. **Restore.**
   - *merge*: add the backup's keys not already present (refuse the restore if
     the list would exceed 4) and the records whose credential ID is not
     already present (refuse if over 50). Nothing local is removed.
   - *replace*: the records and keys become the backup's when it has a
     `passkeys` section; a backup without one leaves local passkeys untouched
     (as today). Part of the same staged, power-cut-safe commit as the
     entries (`restore.commit`).
   - Both report `passkeys` (records added) next to `added`/`updated`.
5. **Signature counter.** The backup stores the device counter; a restore
   raises the local counter to at least that value + 1000, so sites that
   check the counter never see it go backwards on the new Keyra.
6. **Flags unchanged.** Credentials keep BE = 0 / BS = 0. WebAuthn forbids
   changing BE after registration, and some sites would reject the
   credential. A restored passkey behaves like the same security key, moved.

## Honest limits (go into FIDO.md)

- Restoring makes a second working copy if the old Keyra still exists. Sites
  that track the counter may flag the old one once the new one is used. Erase
  or lock away the old Keyra.
- Passkeys created after the backup are not in it, as for passwords.
- The U2F attestation key is per device and not backed up: it only signs
  registrations, so existing sign-ins are unaffected.

## Acceptance

- Host tests: export → import on a vault with a different DEK, then
  GetAssertion for a discoverable and a non-discoverable credential signs and
  verifies; merge onto a Keyra with its own passkeys keeps both sets working;
  replace with and without a `passkeys` section; v1/v2 backups still import;
  key-list and 50-record limits refuse before anything changes; a power cut
  between stage and commit leaves old or new, never mixed.
- Counter: after a restore it is above the backup's.
- Web: Backup screen switch (default on), restore result names the passkeys,
  text no longer says passkeys are left out; mock mirrors it; e2e covers it.
