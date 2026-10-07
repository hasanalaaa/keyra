# Security

Keyra stores secrets, so this document says plainly what it protects, how, and
where it does not. If you find a problem, see [Reporting a vulnerability](#reporting-a-vulnerability).

## Summary

| Question | Answer |
|---|---|
| Are my passwords encrypted at rest? | Yes. Every entry is AES-256-GCM encrypted with a random data key that your passphrase protects. |
| Can someone with the device guess my passphrase offline? | Yes, if they dump the flash, at the speed of PBKDF2 on their hardware. Use a long passphrase. |
| Is the phone-to-device link encrypted by TLS? | No. It is plain HTTP over the device's own WPA2 Wi-Fi, or over your home network if you turn on home Wi-Fi. |
| Can other devices on my home network reach Keyra? | Only if you turn on home Wi-Fi. They can load the page, but unlocking from a new browser there also needs a press of Keyra's button. |
| Can malware on the computer make Keyra type? | No. Every typing action needs a physical button press. |
| Can malware on the computer read the vault? | Not through Keyra. It sees only what is typed into it, like any keyboard input. |
| Can malware on the computer use my passkeys? | Only with your button press while Keyra is unlocked and its light double-blinks white. It can start a request and hope you press for it. See [Passkeys and security key](#passkeys-and-security-key-usb). |
| Can a stranger pair with Keyra over Bluetooth? | Only during a 2-minute window you open with a button press, and only Just Works pairing (no code). See [Bluetooth](#bluetooth). |
| Is there a secure element? | No. |
| Is flash encryption or secure boot on by default? | No. They are optional and irreversible; see [docs/HARDWARE.md](docs/HARDWARE.md). |

Keyra has not had an independent security audit.

## Threat model

### What Keyra is designed to resist

1. **A lost or stolen device (powered off or locked).** Without the passphrase,
   entries are AES-256-GCM ciphertext. The passphrase is stretched with
   PBKDF2-HMAC-SHA256 calibrated to take about 1.2 s on the device, and online
   guessing through the device is rate limited (below).
2. **Remote or in-browser abuse of the web app.** Requests need an `HttpOnly`,
   `SameSite=Strict` session cookie plus a CSRF header, foreign `Origin` values are
   rejected, and the page ships a strict Content Security Policy, `X-Frame-Options: DENY`
   and `Referrer-Policy: no-referrer`.
3. **Malicious software on the computer Keyra is plugged into.** Keyra
   presents a USB keyboard and a FIDO security key. It has no storage or network
   interface toward the computer, nothing is typed until a human presses the
   button on the device, and the security key signs nothing without a press
   either (and never while locked).
4. **A stranger within Wi-Fi range.** The access point uses WPA2-PSK. Setup forces
   you to replace the factory Wi-Fi password, and the vault stays encrypted and
   locked behind your passphrase regardless of Wi-Fi access.
5. **Power loss during a write.** Entry and metadata writes are atomic
   (temporary file, then rename). The failed-unlock counter is stored before the key
   derivation runs, so cutting power mid-attempt still counts it.

### What Keyra does not protect against

- **Someone who has the unlocked device and the button.** While unlocked, anyone
  who can use the phone session and press the button can type your credentials.
  Lock it (long-press the button, or wait for auto-lock; default 15 minutes).
- **An attacker on your Wi-Fi who sees traffic.** The web app uses HTTP, not HTTPS.
  A passive attacker who has the WPA2 passphrase, or who captured the WPA2
  handshake and later learns it, can read session traffic, including the master
  passphrase when you type it. Keep the Wi-Fi password private and rotate it if it leaks.
  Why no TLS: a device-local certificate cannot be trusted by browsers without
  per-device installation, and warnings would train users to click through.
- **Physical attacks on the chip.** There is no secure element and no tamper
  resistance. With flash encryption off, an attacker with physical access can read
  the flash and attack the passphrase offline. Fault injection and decapping are out of scope.
- **Keyloggers and screen capture on the host computer.** Typed text is ordinary
  keyboard input. Keyra cannot protect a password from software that already
  watches the keyboard.
- **Shoulder surfing and clipboard exposure.** The phone UI can show and copy
  fields. Copying puts the value in the phone's clipboard.
- **A weak master passphrase.** Offline guessing cost scales with passphrase
  strength. The minimum is 10 characters; longer is much better.
- **Supply-chain attacks** on the toolchain, dependencies, or the board itself. Build from a
  tagged source and verify what you flash.
- **Denial of service.** Anyone in Wi-Fi range or with the device can wipe it
  (see factory reset) or jam the radio.

### Home Wi-Fi (optional)

Home Wi-Fi is off by default. Turning it on, changing the network or turning it
off needs a button press, because it changes who can reach the device.

- **Plain HTTP on your home network is visible to that network.** WPA2/WPA3
  keeps out people who do not know the home Wi-Fi password. It does not protect
  you from other devices on the same network, from a compromised router, or from
  anyone who can intercept traffic there (for example by ARP spoofing). Such an
  attacker can read the master passphrase as you type it, the session and
  trusted-browser cookies, and any entry you open, and can copy those cookies to
  act as your browser. **On a network you do not control, unlock through Keyra's
  own Wi-Fi instead, or keep home Wi-Fi off.**
- **Trusted browsers.** A browser that unlocks through the home network must be
  approved once with the button. After the correct passphrase, Keyra answers
  `202` and gives the browser a random 256-bit `kt` cookie (`HttpOnly`,
  `SameSite=Strict`); only its SHA-256 is stored in NVS, and only after the
  press. A device on the network that learns or guesses the passphrase still
  cannot unlock without someone pressing the button. Wrong passphrases never ask
  for the button and count toward the rate limit. Up to 8 browsers are kept;
  adding a ninth forgets the least recently used one. Removing a browser in
  Settings ends its sessions. This does not stop the interception attacker
  above, who can copy a trusted browser's cookies.
- **Keyra's own Wi-Fi never asks for trust.** Joining it already requires its
  private WPA2 password, so a browser on it is treated as close by.
- **The same request checks apply on both networks.** Session cookie, CSRF
  header and `Origin` checks are unchanged; an `Origin` is accepted only for
  `http://keyra.local`, `http://192.168.4.1` or Keyra's current home-network
  address, and requests for any other `Host` are redirected, which blocks DNS
  rebinding. The catch-all DNS server is bound to Keyra's own Wi-Fi address and
  answers only its clients, and connectivity probes are answered only there; on
  the home network Keyra announces only `keyra.local` over mDNS.
- **Keyra joins only WPA2/WPA3 networks** and refuses to fall back to an open,
  WEP or WPA1 network with the same name.
- **Stored credentials.** The home Wi-Fi password is stored in NVS like Keyra's
  own Wi-Fi password: not encrypted unless you enable flash encryption. The API
  never returns it and it is never logged.
- **Clock.** While home Wi-Fi is connected, the clock comes from NTP
  (`pool.ntp.org`, `time.google.com`); once NTP has set it, browsers' clocks are
  ignored (for 3 hours after each sync). NTP is
  unauthenticated, so a network attacker could shift the clock and with it the
  2FA codes Keyra shows or types.
- **Denial of service from the home network.** Any device there can request a
  factory reset, which waits for a button press that you can refuse with a long
  press.

### Factory reset is deliberately possible without the passphrase

If you forget the passphrase the data is unrecoverable by design. The
factory-reset endpoint therefore needs no session, but it only does anything
after a **physical button press** and it erases the vault. Someone who holds
the device can always wipe it; they cannot read it.

## Cryptographic design

All values below are implemented in `firmware/components/keyra_vault/` and covered
by host tests in `firmware/components/keyra_vault/host_test/`.

| Item | Detail |
|---|---|
| Passphrase to key | PBKDF2-HMAC-SHA256, 16-byte random salt, 32-byte output (the KEK). |
| Iteration count | Calibrated on the device at setup to take about 1.2 s; never below 60,000 and capped at 2,000,000. Stored in the vault metadata. |
| Data key (DEK) | Random 256-bit key. The KEK wraps it with AES-256-GCM (AAD `keyra/meta/v1`); only the wrapped DEK is stored (`meta.bin`). |
| Changing passphrase | Re-wraps the DEK only. Entries are not re-encrypted. |
| Entries | One file per entry: AES-256-GCM under the DEK, random 96-bit IV per write, 128-bit tag, AAD `keyra/e/v1/<id>`. Binding the ID prevents swapping one entry's ciphertext into another's file. |
| Password history | Up to 10 previous passwords and the time each was replaced live **inside** the entry's encrypted plaintext (format 2), so flash holds no history metadata in the clear. Only the vault adds to it, when an update changes the password; clients cannot write or erase it. Entries written by earlier firmware (format 1) are read as-is and rewritten as format 2 on their next change. |
| Storage | LittleFS on a dedicated `vault` partition. Mount failure never auto-formats. |
| Backup | Separate backup passphrase (at least 12 characters), PBKDF2-HMAC-SHA256 with a fresh salt and AES-256-GCM, written as JSON (`keyra-backup`, version 2, which includes password history; version 1 files still import). Treat the file as sensitive. |
| Randomness | `psa_generate_random`, which ESP-IDF backs with the ESP32-S3 hardware RNG. |
| Password generator | `POST /api/generate` draws from `esp_fill_random` (the hardware RNG, a true random source while the radio is on; Keyra's radio is always on). Each character is uniform over the enabled character classes by rejection sampling (no modulo bias); class minimums are met by discarding whole candidates, so every password that fits the settings is equally likely and no position is favoured. Settings where fewer than 1 candidate in 1,000 would qualify are refused. The reported entropy is exact (log2 of the number of possible passwords). Generated passwords are never logged and are not stored unless you save them. |
| Crypto library | mbedTLS through the PSA Crypto API, shipped with ESP-IDF. No custom primitives. |

### Unlock rate limiting

The failure counter is saved to NVS **before** the key derivation runs. After
`n` consecutive failures the next attempt is refused for:

| Consecutive failures | Delay before the next attempt |
|---|---|
| 1 to 4 | none (each attempt still costs about 1.2 s of PBKDF2) |
| 5 | 2 s |
| 6 | 4 s |
| 7 | 8 s |
| 8 | 16 s |
| 9 to 12 | 32 s, 64 s, 128 s, 256 s |
| 13 | 512 s |
| 14 or more | 900 s (15 min, the cap) |

A reboot restarts the full delay for the stored count, so power-cycling never
shortens it. A correct unlock resets the counter. Rate limiting protects the
device's own API; it does **not** slow an attacker who copies the flash and
runs PBKDF2 elsewhere. Only passphrase strength does.

### Memory handling

- Decrypted entries and keys exist in RAM only while the vault is unlocked.
- Locking (button long-press, auto-lock, factory reset, or lock from the app)
  zeroizes the DEK and decrypted entries with `mbedtls_platform_zeroize`, which the
  compiler cannot optimise away.
- Secrets are never written to logs. Release builds disable logging entirely.
- The list endpoint returns no secrets; passwords are fetched one entry at a time.
- Free text for **Type text** (SPEC §9.2) lives in one buffer that is zeroized when the typing finishes, is cancelled, expires, is replaced, or the vault locks. A host test swaps the allocator to prove no copy is freed un-wiped.

### Physical confirmation

Nothing is typed without a button press, and the press must happen within 60
seconds of the request. Setup, Wi-Fi credential changes, joining, changing or
leaving the home network, trusting a browser on the home network, opening the
Bluetooth pairing window, a replacing restore, factory reset and every passkey or
security-key registration and sign-in also need a button press. The button is GPIO0 (the BOOT button),
and the firmware never restarts while it is held low, to avoid latching ROM download mode.

## Bluetooth

Keyra can also type as a Bluetooth LE keyboard (HID over GATT, NimBLE). The
same button rule applies: nothing is typed over Bluetooth without a press.

**How pairing is gated.**

- A new device can pair only inside a **120-second window** that opens after an
  authenticated request (`POST /api/ble/pair`) *and* a press of Keyra's button.
  The LED pulses cyan while it is open. The window closes early as soon as one
  device has paired, when the vault is locked, and when Bluetooth is switched off.
- Outside the window Keyra is not discoverable. It advertises only for paired
  devices (by default only while an action waits for one, and then for that
  device alone), with a filter accept list in the radio controller, so only
  those devices can scan or connect. Their identity keys are in the
  controller's resolving list, so phones with rotating private addresses still match.
  The firmware checks the same rule again in software when a link comes up, refuses
  a paired device asking for new keys (re-pairing) outside the window, and removes
  any bond that appears outside the window.
- Pairing uses **LE Secure Connections only** (legacy pairing is refused) with
  bonding. Keys are stored in NVS. At most **4** devices; when full, Keyra refuses
  new pairings instead of silently forgetting an old device, and you forget one in
  the app (`DELETE /api/ble/bonds/{addr}`). Factory reset forgets all of them.
- Every HID characteristic (report map, keystrokes, LED report) requires an
  encrypted link, and Keyra sends keystrokes only to a **bonded** device on an
  encrypted link that has subscribed to keyboard reports.

**What it does not protect against.**

- **Man-in-the-middle during the pairing window.** Keyra has no screen or
  keypad, so pairing is "Just Works": it is encrypted but not authenticated. An
  attacker in radio range during those 2 minutes could pair a device of their own,
  or relay between Keyra and your device. If that happens they receive what Keyra
  types to that bond. Mitigations: the window is short, needs the button and an
  unlocked session, closes after the first pairing, and the app lists every paired
  device with its name so an unexpected one stands out. Pair in a place where you
  can see who is around, and forget anything you do not recognise.
- **A paired device that is compromised.** It receives the keystrokes you send
  it, exactly like a USB computer would. Forget devices you no longer use.
- **Radio tracking.** When it advertises, Keyra uses its fixed public Bluetooth
  address, so a nearby scanner can tell the same device is around. With the
  default **Connect: When typing** it advertises only while an action waits for
  a host (and during pairing); with **Always** it advertises whenever it has
  paired devices. Turn Bluetooth off in Settings if that matters to you.
- **The device name a host reports** (shown in the app) is chosen by that host
  and cannot be trusted as proof of identity.

## Passkeys and security key (USB)

Keyra is also a FIDO2 (CTAP 2.0) and U2F security key over USB. The design, the
exact key formats and the list of what is implemented are in
[docs/FIDO.md](docs/FIDO.md). In short:

- **Keys at rest.** Each credential's P-256 private key exists only inside its
  credential ID, encrypted with AES-256-GCM under a wrapping key derived from the
  vault's data key (HMAC-SHA256 with a random salt) and bound to the website's
  RP ID hash. Passkey records (site, user name, credential ID) are vault files
  encrypted like entries. Without the passphrase a flash dump reveals no key;
  with a weak passphrase it reveals all of them, exactly like your passwords.
- **Locked means no signatures.** While locked, requests wait up to 30 s for you
  to unlock from the phone, then fail.
- **Presence.** A short press while the light double-blinks white approves one
  request; a long press refuses it. While a request waits the button belongs to
  it, so malware that starts a request just before you press for typing could
  get that press: if the light shows the FIDO pattern when you did not ask for
  a passkey, long-press.
- **User verification is "unlocked".** Keyra reports built-in user verification
  and sets the UV flag because it never signs while locked, but the check is the
  master passphrase entered on the phone up to the auto-lock time earlier, not a
  PIN or fingerprint for each sign-in. Anyone holding an unlocked Keyra can use
  your passkeys. There is no ClientPIN yet.
- **Not certified, self attestation.** CTAP2 registrations use self attestation.
  U2F registration requires a certificate, so each Keyra generates its own
  P-256 attestation key on first use and self-signs a certificate for it (both
  in NVS, replaced by a factory reset). It is not vault-encrypted, because it
  only signs registration statements and never signs you in; it vouches for
  nothing beyond "this Keyra". Relying parties that require attested,
  certified authenticators will refuse Keyra.
- **Phishing.** The browser supplies the RP ID; a credential ID presented under
  another RP ID does not decrypt. Deleted passkeys stop working even if a site
  still holds their ID.
- **Counter.** One global signature counter in NVS, written before each
  signature, never reset.
- **Reset.** `authenticatorReset` (within 10 s of power-up, with a press and the
  vault unlocked) deletes passkeys and changes the wrapping key; factory reset
  destroys everything.
- **Backups.** Passkeys are not in encrypted backups (yet). Losing or resetting
  Keyra loses them: keep a second sign-in method on every account.

## Optional hardening (not enabled by default)

Flash encryption and secure boot raise the cost of physical attacks. They are
**irreversible eFuse changes** that can make a board unflashable if done wrong.
Read the warnings in [docs/HARDWARE.md](docs/HARDWARE.md#optional-hardening-irreversible) first.

## Non-goals

Keyra is a convenience-and-isolation device for personal credentials. It is not:

- a replacement for a certified hardware security key with a secure element: its FIDO support
  (above) is a convenience for personal accounts, not a YubiKey substitute,
- a multi-user or enterprise secret manager,
- a defence against a nation-state with physical access and lab equipment,
- a way to sync secrets over the internet (there is no cloud and no remote access).

## Supported versions

Security fixes land on `main` and in the latest release only.

## Reporting a vulnerability

Please **do not open a public issue** for security problems.

1. Go to the [Security tab](https://github.com/hasanalaaa/keyra/security/advisories/new)
   and choose **Report a vulnerability** (a GitHub private security advisory).
2. Include the firmware version, the board, what you did, what you saw, and, if possible, a proof of concept. Do not include real secrets.

You can expect an acknowledgement within a few days. This is a volunteer
project; please allow reasonable time for a fix before public disclosure and
we will credit you in the advisory unless you prefer otherwise.

In scope: the firmware, the web app, the build and release pipeline, and the
documentation in this repository. Out of scope: attacks needing a modified device,
vulnerabilities in ESP-IDF or third-party components themselves (report those upstream, but tell us if Keyra's use of them makes it worse), and the limits listed above.
