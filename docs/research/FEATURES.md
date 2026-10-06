# Keyra feature research

Status: research only, no code. Written 2026-10-06 against `docs/SPEC.md` (v1 + §8 v1.1 BLE and home Wi-Fi) and README (0.1.0, unreleased).

How to read this file:

- Every claim about another product or a platform has a URL next to it. Claims marked **[verify]** are my own engineering assessment or something I could not confirm from a source in this pass; check before building on them.
- Effort scale on ESP32-S3 with the current stack (ESP-IDF 6.0, TinyUSB, NimBLE, Preact web app): **S** = under half a day of agent work, **M** = about a day, **L** = 2-4 days, **XL** = a week or more.
- Value is 1-5 for a daily Keyra user (not for a security researcher).
- "Contract" in the security column means it touches something SPEC.md or persisted data fixes (REST API, vault entry format, backup JSON). Keyra is still **0.1.0 Unreleased** (CHANGELOG.md), so these can still change for free. After the first release they cannot (global rule: published contracts).

---

## 0. Findings in one page

1. **The market has a hole exactly where Keyra sits.** Hardware password managers are either expensive with a bad setup UI (OnlyKey, Mooltipass, Hideez), or hobby projects with a tiny OLED and no phone UI (PasswordPump, Flipper apps, most ESP32 repos). Nobody combines a $5-10 board, a phone-first web app, USB plus BLE typing and physical-press gating. That combination is the product; do not dilute it.
2. **The number one reported pain is keyboard layouts**, in every product that types (Mooltipass issues, OnlyKey HN thread, Yubico docs). Keyra is US-only today, and the owner's audience (Arabic/English bilingual hosts) hits the hardest version of this problem. Layouts plus auto-type sequences are the best value-for-effort items.
3. **The second pain is setup friction and lost-device recovery.** Hardware keys cannot be cloned; users are told to buy a second one. Keyra can do better: encrypted backup already exists, and a second $8 board can become a mirror ("Keyra Twin").
4. **The web app runs on plain HTTP, which is not a secure context.** That forbids WebCrypto, async clipboard, live camera (`getUserMedia`), WebAuthn, Web NFC, service workers and more (see §2.0). None of the recommended features need them; the ones that look like they do have a workaround listed.
5. **Cheap security wins that fit the brand:** presence-gated reveal/export, arm-time host binding, auto-lock on USB unplug/suspend, signed OTA. These strengthen "malware cannot make it type" instead of adding surface.
6. **FIDO2/passkeys are feasible but XL and security-sensitive** (no secure element). Ship U2F-style 2FA or defer; do not start it before layouts, sequences, OTA and backups are solid.

---

## 1. Landscape

### 1.1 Hardware password managers, security keys and hobby projects

| Product | Key features | Price / openness | Users praise | Users complain |
|---|---|---|---|---|
| **OnlyKey** | 24 accounts in 2 profiles of 12 slots, types username/password/2FA, FIDO2/U2F, TOTP, Yubico OTP, challenge-response, PIN entered on the device, 10 wrong PINs wipes it ([Amazon listing](https://www.amazon.com/OnlyKey-Stealth-Black-Case-Communication/dp/B06Y1CSRZX)). Duress PIN opens the second profile, self-destruct PIN, 23+ keyboard layouts, typing speed 1-10, inactivity lock, encrypted backup, SSH/GPG agent ([User's Guide](https://docs.onlykey.io/usersguide/)) | Open-source firmware; price not checked | Multi-year reliability, 24 slots and backup vs YubiKey's 2 slots, password manager plus 2FA together ([HN thread](https://news.ycombinator.com/item?id=21884184)) | Poor UI: non-technical users cannot set it up, technical users spent 30+ minutes; no public audit; critics call the RNG code unsafe and say keys are trivially extractable if lost; limited layouts, Dvorak requested; LED malfunction in USB 3 ports ([same HN thread](https://news.ycombinator.com/item?id=21884184)) |
| **Mooltipass Mini BLE** | Secure element, smart card plus PIN, BLE keyboard, OLED, scroll wheel, WebAuthn, TOTP, SSH keys, breach hints, browser extensions and the Moolticute desktop app ([SecurityWeek](https://www.securityweek.com/bluetooth-enabled-mooltipass-hardware-password-manager-unveiled/), [Custom PC](https://www.custompc.com/mooltipass-mini-ble-review)). Backup via Moolticute; clone the smart card for a spare ([manual](https://fccid.io/2AYPT-MBLE1/User-Manual/User-Manual-5088694.pdf)) | Open-source firmware ([repo](https://github.com/mooltipass/minible)); $125-160 ([Tindie listing](https://www.tindie.com/products/stephanelec/mooltipass-mini-ble-authenticator/), SecurityWeek) | Login is fast and easy including 2FA, saving credentials from the browser is fast ([Hackaday](https://hackaday.com/2020/07/23/hands-on-wireless-login-with-the-new-mooltipass-mini-ble-secure-password-keeper/)) | Price; no mobile management app; WebAuthn only (no U2F, so no Firefox on Linux); non-replaceable battery ([Custom PC](https://www.custompc.com/mooltipass-mini-ble-review)). Wrong characters with UK layout over BLE ([#848](https://github.com/mooltipass/moolticute/issues/848)), iOS custom keyboard app breaks typing ([#215](https://github.com/mooltipass/minible/issues/215)), no Colemak ([#409](https://github.com/limpkin/mooltipass/issues/409)), pound sign ([#394](https://github.com/limpkin/mooltipass/issues/394)). Original Mooltipass called over-engineered and cassette-player sized ([HN](https://news.ycombinator.com/item?id=11983563)) |
| **Hideez Key 5** | FIDO2 key, password manager for 1,000 logins, TOTP, Windows proximity lock/unlock, RFID door tag ([product page](https://hideez.com/products/hideez-key-5)) | Commercial; price not checked | Small, 6-month battery ([MacSources](https://macsources.com/hideez-key-review-lots-promise-limited-utility/)) | Lock/unlock not available on Apple devices, setup bounces between app and website, theft alarm triggers at 30 ft, app crashes on firmware update; 2/5 ([MacSources](https://macsources.com/hideez-key-review-lots-promise-limited-utility/)) |
| **Everykey** | BLE proximity unlock of devices and accounts ([TechCrunch](https://techcrunch.com/2014/11/08/everykey/)) | Commercial | Idea | Pairing trouble on Windows 10, loses contact with key, over a minute to load credentials, devices lasting about 6 months; 3.2/5 ([Best Buy reviews](https://www.bestbuy.com/site/reviews/everykey-wireless-hardware-password-manager-black/6317288)) |
| **Nitrokey 3** | FIDO2/U2F, TOTP/HOTP, OpenPGP, PIV, password safe up to 50 entries (login, password, comment, OTP) ([docs](https://docs.nitrokey.com/nitrokey3/index), [secrets app](https://github.com/Nitrokey/trussed-secrets-app)) | Open source; Common Criteria secure element | Open and auditable | Small password capacity |
| **SoloKeys Solo 2** | FIDO2/U2F only, no password manager ([comparison](https://stateofsurveillance.org/guides/advanced/open-source-security-keys/)) | Open hardware and firmware | Cheap, simple | No OTP or password storage |
| **YubiKey 5** | FIDO2, OTP slots incl. static password (must pick a keyboard layout; ModHex avoids it) ([static password docs](https://docs.yubico.com/yesdk/users-manual/application-otp/how-to-program-a-static-password.html), [ModHex](https://docs.yubico.com/yesdk/users-manual/application-otp/modhex.html)); 25 passkeys on firmware 5.0-5.6, 100 on 5.7+ ([Yubico](https://support.yubico.com/hc/en-us/articles/360013790319-How-many-accounts-can-I-register-my-YubiKey-with)) | Closed, commercial | Reliability | Cannot be backed up; users are told to register a second key ([MakeUseOf](https://www.makeuseof.com/passkeys-are-great-no-one-tells-you-about-catch-until-its-too-late/)) |
| **Trezor Password Manager** | Per-entry keys derived from the device, physical confirmation per decrypt, browser extension, Dropbox sync ([Bitcoin.com](https://news.bitcoin.com/trezor-unveils-password-manager/)) | Open source extension | Only one secret decrypted at a time, with physical consent ([HN discussion](https://news.ycombinator.com/item?id=25293611)) | Needs a hardware wallet and a cloud account; I did not verify its current status |
| **PasswordPump 2.0** | 250 credentials, AES-256, OLED plus rotary encoder, types username/password/URL/old password, 31-char generator, backup to second EEPROM, wipe after 10 bad tries ([Tindie](https://www.tindie.com/products/passwordpump/passwordpump-v20-with-rotary-encoder/), [Instructables](https://www.instructables.com/PasswordPump-Passwords-Manager/)) | Open source DIY kit | Fully offline, hackable | Tiny screen, on-device entry, no phone UI |
| **Flipper Zero + apps** | BadUSB with DuckyScript, per-layout files on SD, ALTSTRING for foreign layouts ([docs](https://docs.flipper.net/zero/bad-usb)); password apps: [FlipPass](https://lab.flipper.net/apps/flippass) (KDBX, types username, password, AutoType sequences, OTP over USB or BLE), [flipper-passmanager](https://github.com/OG34/flipper-passmanager) (ChaCha20, PIN), [Gatekeeper](https://github.com/enexis1337/Gatekeeper) (30 passwords) | Open source apps; device about $170 **[verify]** | KeePass compatibility, layouts shipped | Small screen; password apps are side features |
| **SecureGen** (ESP32 / ESP32-S3) | TOTP and password manager, BLE and USB HID, two vaults via hidden space with separate PINs, encrypted export, web UI over STA/AP/offline, optional RTC, display, 5 languages ([repo](https://github.com/makepkg/SecureGen)) | Open source | Closest peer to Keyra; has a web UI and a hidden vault | Needs a display board; no button-gated typing model described |
| **Other ESP32 typers** | [geo-tp/Password-Manager](https://github.com/geo-tp/Password-Manager), [SaladClimbing/USB-Password-Manager](https://github.com/SaladClimbing/USB-Password-Manager) (rotate and click), [YeetTheAnson/PortableCredentialStorage](https://github.com/YeetTheAnson/PortableCredentialStorage), [ekoslav BLE keyboard](https://github.com/ekoslav/ESP32_Password_keyboard) | Open source | Cheap | Mostly single-purpose |
| **ESP32-S3 FIDO2 projects** | [aaka3h](https://github.com/aaka3h/ESP32-S3-FIDO2-Security-Key) (CTAP2 over TinyUSB HID, usage page 0xF1D0, "experimental, not FIDO certified"), [Crypto-T-key-S3](https://github.com/ImNotMrReaper/Crypto-T-key-S3) (CTAP2.0 plus U2F plus resident keys on LilyGo T-Dongle S3), [swiss_sec](https://github.com/AdamPodymniak1/swiss_sec) (vault plus FIDO2 plus TOTP), [Pico-Fido](https://www.picokeys.com/pico-fido/) (CTAP 2.3/1.2, resident keys, hmac-secret, PIN, button for presence, runs on ESP32-S3; AGPLv3) | Open source | Proves the platform can do FIDO | Experimental; no secure element; Pico-Fido's AGPLv3 conflicts with Keyra's MIT license **[verify license terms before reusing any code]** |
| **Pwnagotchi / Minigotchi** | Not a password tool. Face and moods gave a pocket device a personality and emotional attachment ([Hackster](https://www.hackster.io/news/pwnagotchi-combines-artificial-intelligence-with-a-cute-pet-to-capture-the-world-s-wi-fi-handshakes-e1c8b4efa33a), [Minigotchi faces](https://github.com/ATOMNFT/Minigotchi-ESP32/blob/main/FACES.md)) | Open source | Delight, community | Lesson only: a status LED with character is cheap brand value |

### 1.2 Software managers (feature donors)

| Product | Killer features worth adapting | Source |
|---|---|---|
| **KeePassXC / KeePass** | Auto-type sequences with `{USERNAME}{TAB}{PASSWORD}{ENTER}` default; placeholders `{TITLE} {URL} {NOTES} {TOTP} {S:custom}`; actions `{TAB} {ENTER} {DELAY n} {DELAY=n} {CLEARFIELD} {PICKCHARS}`; per-entry sequences matched by window title; global and per-key delays ([AutoType docs](https://github.com/keepassxreboot/keepassxc/blob/develop/docs/topics/AutoType.adoc)). Browser integration for Chrome, Firefox, Edge, Brave and others, SSH agent, KDBX ([user guide](https://keepassxc.org/docs/KeePassXC_UserGuide)). Pain: auto-type continues when the window changes; users ask to cancel on title change ([#4668](https://github.com/keepassxreboot/keepassxc/issues/4668)) and to wait for a title change ([#2060](https://github.com/keepassxreboot/keepassxc/issues/2060)) | linked |
| **Apple Passwords** | Security section: reused, weak, compromised; built-in verification codes via QR or setup key; shared groups with trusted contacts including 2FA codes ([Apple Support](https://support.apple.com/en-gb/120758), [MacRumors](https://www.macrumors.com/guide/ios-18-passwords/)) | linked |
| **1Password** | Watchtower: breached, reused, weak, 2FA available, expiring items (cards, passports); checks run locally ([support](https://support.1password.com/watchtower/)); Have I Been Pwned integration ([1Password](https://1password.com/haveibeenpwned)); Travel Mode ([comparison](https://password-manager.cybernews.com/proton-pass-vs-1password/)) | linked |
| **Bitwarden** | Emergency access with view or takeover for trusted contacts ([help](https://bitwarden.com/help/emergency-access/)); Send for expiring shares ([blog](https://bitwarden.com/blog/quick-tips-to-secure-and-share-your-information/)) | linked |
| **Proton Pass** | Hide-my-email aliases, secure notes, cards, identities, item sharing and secure links, Pass Monitor for weak/reused/inactive 2FA ([proton.me/pass](https://proton.me/pass)) | linked |

### 1.3 Pain points reported, and where Keyra stands

| # | Pain point | Evidence | Keyra today | Response |
|---|---|---|---|---|
| P1 | **Wrong characters on non-US layouts**, per host and per transport | Mooltipass [#848](https://github.com/mooltipass/moolticute/issues/848), [#394](https://github.com/limpkin/mooltipass/issues/394), [#504](https://github.com/limpkin/mooltipass/issues/504), [#409](https://github.com/limpkin/mooltipass/issues/409), [Google group](https://groups.google.com/g/mooltipass/c/64Sq927t_RI); Yubico requires a layout choice ([docs](https://docs.yubico.com/yesdk/users-manual/application-otp/how-to-program-a-static-password.html)); OnlyKey layouts limited ([HN](https://news.ycombinator.com/item?id=21884184)) | US only, refuses non-ASCII | F02, F03, N7, N11 |
| P2 | **Setup friction and a bad UI** | OnlyKey 30+ min setup ([HN](https://news.ycombinator.com/item?id=21884184)); Hideez app/website bounce ([MacSources](https://macsources.com/hideez-key-review-lots-promise-limited-utility/)); Everykey pairing ([reviews](https://www.bestbuy.com/site/reviews/everykey-wireless-hardware-password-manager-black/6317288)) | Three-step onboarding, no app | Keep it. Protect this advantage; every new feature must stay out of the first-run path |
| P3 | **BLE reconnect/bonding bugs** | Windows keyboard dead after reconnect ([#328](https://github.com/T-vK/ESP32-BLE-Keyboard/issues/328)); iOS "Peer removed pairing information" until bond is forgotten, fixed by persisting bonds in NVS and resolving random addresses ([esp-nimble #33](https://github.com/espressif/esp-nimble/issues/33)); Android forgets device ([#246](https://github.com/T-vK/ESP32-BLE-Keyboard/issues/246)) | v1.1 BLE in design | Build a BLE test matrix (Windows 11, macOS, iOS, Android, Linux) before claiming BLE works; enable NimBLE NVS persistence and RPA resolution |
| P4 | **Lost device, no clone** | Hardware passkeys vanish without a second key ([MakeUseOf](https://www.makeuseof.com/passkeys-are-great-no-one-tells-you-about-catch-until-its-too-late/)); Mooltipass needs a card clone plus Moolticute backup ([manual](https://fccid.io/2AYPT-MBLE1/User-Manual/User-Manual-5088694.pdf)); OnlyKey file backup criticized as unusual for a key ([HN](https://news.ycombinator.com/item?id=21884184)) | Encrypted backup file, manual | F33, F34, N4 |
| P5 | **Cost and bulk** | Mooltipass $125-160; "80s cassette player" ([HN](https://news.ycombinator.com/item?id=11983563)) | $5-10 board | Marketing, not engineering |
| P6 | **No mobile management** | Mooltipass has no mobile Moolticute ([Custom PC](https://www.custompc.com/mooltipass-mini-ble-review)) | Phone-first web app | Core differentiator; keep the phone path first |
| P7 | **Trust: no audit, weak RNG, extractable keys** | OnlyKey HN thread ([link](https://news.ycombinator.com/item?id=21884184)) | Honest limits in README/SECURITY.md | Keep honesty. Document RNG source, publish test vectors, encourage an audit |
| P8 | **Typing into the wrong window** | KeePassXC [#4668](https://github.com/keepassxreboot/keepassxc/issues/4668) | 60 s pending window, one-shot | F08, N1, N8 |
| P9 | **macOS "Keyboard Setup Assistant" shown for unidentified keyboards** | CircuitPython [#1176](https://github.com/adafruit/circuitpython/issues/1176) (HID country code is the suspected lever) | Not verified on Keyra | Test on a Mac; set HID `bCountryCode` and a stable VID/PID/product string **[verify]** |
| P10 | **Hosts that detect injected typing** | Research on keystroke-dynamics detection of Rubber Ducky style injection ([arXiv 2604.15845](https://arxiv.org/pdf/2604.15845)) | 12 ms key delay default | Honest: some managed hosts will block or flag unknown HID devices. Do not build evasion. Offer slower typing for reliability only |

---

## 2. Feature catalogue

### 2.0 What plain HTTP forbids (applies to every feature below)

`http://keyra.local` and `http://192.168.4.1` are not secure contexts. MDN lists the APIs that require one ([MDN](https://developer.mozilla.org/en-US/docs/Web/Security/Secure_Contexts/features_restricted_to_secure_contexts)). What that means for Keyra:

| Not available on the Keyra page | Consequence | Workaround |
|---|---|---|
| Web Crypto (`crypto.subtle`) | No SHA-1/HMAC/AES in the browser for free | Bundle a small pure-JS or WASM implementation when needed (SHA-1 for HIBP, ed25519/X25519 for pairing). WASM itself is not restricted **[verify on iOS Safari]** |
| Async Clipboard API | `navigator.clipboard.writeText` is undefined | `document.execCommand('copy')` from a user gesture. Deprecated but widely implemented **[verify on iOS and Android]**. Cannot auto-clear the clipboard reliably |
| `getUserMedia` (live camera) | No in-page live QR scanner | `<input type="file" accept="image/*" capture="environment">` opens the native camera app; decode the photo in JS with [jsQR](https://www.npmjs.com/package/jsqr) (about 45 KB). Works fully offline and client-side **[verify capture behaviour on iOS]** |
| WebAuthn / Credential Management | The Keyra page cannot register passkeys or use platform biometrics | Irrelevant to Keyra-as-FIDO-key: the browser talks to the key over USB HID, not through Keyra's page. For biometric unlock, rely on the phone's own password autofill of the master passphrase (F51), a user choice |
| Web NFC, Web Bluetooth, WebHID, WebUSB | The page cannot read tags or talk to Keyra over BLE/USB | NFC sticker tags are read by the OS and open a URL (F42). A separate HTTPS site (for example a GitHub Pages flasher) can use Web Serial/WebUSB |
| Service workers, Push, Notifications, Web Share | No offline PWA cache, no push, no share sheet | "Add to Home Screen" still works as a bookmark-style app (as SPEC §1 assumes). Download backups as files (`Content-Disposition: attachment`) |
| `Secure` cookie flag | Session cookie travels in clear on the wire | Already `HttpOnly; SameSite=Strict` plus CSRF header. See N3 for real link encryption |

A browser extension (F40) is the exception: extensions with host permissions can call `http://keyra.local` and are not subject to Chrome's Local Network Access prompt ([Chrome blog](https://developer.chrome.com/blog/local-network-access)).

### 2.1 Typing engine

**F01. Auto-type sequences and templates** (value 5, effort M, security: medium, contract: entry format + `/api/type`)
- What: per-entry (and global default) sequence such as `{USER}{TAB}{PASS}{ENTER}`, with `{DELAY ms}`, `{TOTP}`, `{URL}`, `{NOTES}`, custom field refs, `{ENTER}`, `{TAB}`, arrows, and a wait-for-press token (F08). Replaces today's fixed username/password/both/code buttons, which become presets.
- Who: KeePass/KeePassXC ([docs](https://github.com/keepassxreboot/keepassxc/blob/develop/docs/topics/AutoType.adoc)), FlipPass (AutoType over USB/BLE, [link](https://lab.flipper.net/apps/flippass)), OnlyKey (per-slot username/password/TAB/ENTER settings, [guide](https://docs.onlykey.io/usersguide/)), DuckyScript on Flipper ([docs](https://docs.flipper.net/zero/bad-usb)).
- Feasibility: pure firmware logic on top of `typeText`/`tapKey`; fully host-testable. **Security requirement:** a sequence is data inside an encrypted entry and could arrive via import or restore, so the grammar must be a whitelist: named keys only (Tab, Enter, Space, arrows, Home/End, Esc, Backspace, Delete), no modifier chords other than Shift for literals (no Win/Ctrl/Alt/Cmd combinations), literal text length-capped, total expansion capped (for example 512 chars) and previewed (password masked) on the phone while pending. Otherwise a malicious backup turns Keyra into a BadUSB payload.

**F02. Keyboard layouts** (value 5, effort M, security: low)
- What: table-driven keymaps selectable globally and per entry, and (via F53) per host: US, UK, German QWERTZ, French AZERTY, Spanish, Italian, Turkish-Q, Nordic, Dvorak, Colemak, Arabic 101. One source file (CSV or YAML) generates the C++ tables; host tests assert round-trip for every printable character each layout can produce.
- Who: OnlyKey 23+ layouts ([guide](https://docs.onlykey.io/usersguide/)), Flipper layout files ([docs](https://docs.flipper.net/zero/bad-usb)), YubiKey static password layout choice ([docs](https://docs.yubico.com/yesdk/users-manual/application-otp/how-to-program-a-static-password.html)); Mooltipass supports few and cannot auto-detect ([Google group](https://groups.google.com/g/mooltipass/c/64Sq927t_RI)).
- Feasibility: USB HID sends key positions (usage IDs); the host maps them to characters, so Keyra must know the host's layout. Characters needing AltGr or dead keys are the hard part (AZERTY, QWERTZ symbols, accents). Arabic strategy is in F02a.
- **F02a. Arabic and bilingual hosts.** Reality for the owner's users: Windows/macOS machines with Latin plus Arabic layouts toggled by a shortcut. Typing Latin on a host currently set to Arabic yields Arabic letters. Options, in order of preference: (1) Layout Doctor and per-host profile (N7) so the user knows and fixes the host layout; (2) optional per-host `{LAYOUT_SWITCH}` token in sequences with a configurable chord (for example Alt+Shift, Win+Space, Ctrl+Space; **exception to the F01 no-chord rule, limited to this one token**); (3) Arabic-101 table so Keyra can type Arabic usernames/passwords when the host is on Arabic; (4) OS Unicode fallbacks (Windows Alt codes and Alt+X, macOS Unicode Hex Input, Linux Ctrl+Shift+U) as an opt-in, flaky last resort ([Wikipedia Alt code](https://en.wikipedia.org/wiki/Alt_code), [John D. Cook](https://www.johndcook.com/blog/2008/08/17/three-ways-to-enter-unicode-characters-in-windows/), [allsymbols](https://allsymbols.org/keyboard-shortcuts)). Do not build (4) in the first pass.

**F03. Timing profiles and preboot typing** (value 4, effort S-M, security: low)
- What: start delay, per-key delay, per-entry slow mode (VMs, RDP, old BIOS), a "no Enter" disk-unlock preset for FileVault/LUKS/BitLocker prompts. For preboot use, advertise the keyboard as a HID boot-protocol device **[verify TinyUSB boot-protocol support in the esp_tinyusb wrapper]**.
- Who: OnlyKey typing speed 1-10 ([guide](https://docs.onlykey.io/usersguide/)); KeePassXC delays ([docs](https://github.com/keepassxreboot/keepassxc/blob/develop/docs/topics/AutoType.adoc)).
- Notes: `keyDelayMs` already exists in settings. Add the per-entry override and the start delay. Do not add cadence randomisation to hide from detection (P10).

**F05. Remote keyboard (type arbitrary text from the phone)** (value 4, effort S, security: medium)
- What: a text box in the phone UI; pressing the device button types it on the host. Use cases: TV and console login screens over BLE, long Wi-Fi keys, one-off strings, typing on a locked-down kiosk.
- Who: none of the hardware password managers surveyed; Flipper BadUSB scripts do fixed text ([docs](https://docs.flipper.net/zero/bad-usb)).
- Feasibility: reuse the pending-action state machine with a `text` action. **Requirements:** same button gate as any type action, length cap (for example 256 chars), no control characters, LED/phone show a distinct "free text" state, text not logged, cleared from RAM after use.

**F06. Extra typed fields** (value 3, effort S): URL, notes, custom fields by name ({S:name} in KeePassXC, [docs](https://github.com/keepassxreboot/keepassxc/blob/develop/docs/topics/AutoType.adoc)). Falls out of F01 plus the entry schema (F21).

**F08. Wait-for-press step** (value 4, effort S given F01, security: positive)
- What: token `{PRESS}` pauses the sequence until the next physical press (with its own 60 s expiry). Handles two-page logins (username, Next, wait for page, password) without guessing delays. KeePassXC users ask for a title-change wait for the same reason ([#2060](https://github.com/keepassxreboot/keepassxc/issues/2060)); Keyra's equivalent is "the human presses when the page is ready".

### 2.2 Security keys and second factors

**F10. FIDO2/U2F security key (passkeys)** (value 5, effort XL for CTAP2 with resident keys, L for U2F only, security: high)
- What: Keyra enumerates a second HID interface (FIDO usage page 0xF1D0) and answers CTAPHID. The existing button is the user-presence gesture, which is a natural fit.
- Who: OnlyKey, Nitrokey, Solo, YubiKey, Mooltipass Mini BLE (WebAuthn only, [Custom PC](https://www.custompc.com/mooltipass-mini-ble-review)); ESP32-S3 proofs: [aaka3h](https://github.com/aaka3h/ESP32-S3-FIDO2-Security-Key), [Crypto-T-key-S3](https://github.com/ImNotMrReaper/Crypto-T-key-S3), [swiss_sec](https://github.com/AdamPodymniak1/swiss_sec), [Pico-Fido](https://www.picokeys.com/pico-fido/) (AGPLv3, see licence note in §1.1).
- Feasibility: TinyUSB supports multiple HID interfaces, so a composite keyboard plus FIDO device is plausible **[verify with the pinned esp_tinyusb version]**. Staging that keeps risk low: (a) U2F/CTAP1 with key-handle-wrapped keys needs no credential storage (the credential is encrypted into the key handle), which gives 2FA but not passwordless; (b) CTAP2 with resident keys stored in the encrypted vault and included in encrypted backups (an advantage over hardware keys that cannot be cloned, see P4). Passkey limits elsewhere: 25 or 100 on YubiKey ([Yubico](https://support.yubico.com/hc/en-us/articles/360013790319-How-many-accounts-can-I-register-my-YubiKey-with)); Keyra's flash vault is not that constrained.
- **Security requirements:** no secure element, so a flash dump exposes credential keys offline: require flash encryption to be recommended and displayed in the UI before enabling FIDO; self-attestation only; say plainly it is not FIDO certified (as the ESP32-S3 projects do). A wrong implementation can lock users out of accounts, so this is a Wave C item with its own test plan (use a public conformance test tool **[verify which]**). Ship only when the vault, backup and OTA are stable.

**F11. TOTP** (exists). Improvements: QR import (F20), auto-time via SNTP on home Wi-Fi (SPEC §8.2, exists), and a visible "clock not set" state.

**F12. HOTP counter tokens** (value 1, effort S): rare. Skip until requested.

**F13. SSH/GPG agent** (value 2, effort XL): OnlyKey and KeePassXC have it ([guide](https://docs.onlykey.io/usersguide/), [KeePassXC](https://keepassxc.org/docs/KeePassXC_UserGuide)); needs a host helper and breaks "nothing to install". **Do not build.**

**F14. Challenge-response / hmac-secret** (value 2, effort L): falls out of CTAP2 hmac-secret later; do not build separately.

### 2.3 Vault model

**F20. TOTP QR import by photo** (value 5, effort S-M, security: low)
- What: in the phone UI, "Scan QR": pick or capture a photo, decode with jsQR in the browser, parse `otpauth://totp/...` (secret, issuer, digits, period, algorithm) and `otpauth-migration://offline?data=...` (Google Authenticator export: base64 protobuf holding several accounts, [format](https://zwyx.dev/blog/google-authenticator-export-format)). Apple Passwords and others also accept QR or setup key ([Apple](https://support.apple.com/en-gb/120758)).
- Feasibility: no secure context needed (see §2.0). Image never leaves the phone. The protobuf decode is small enough to hand-write. Also accept pasting an `otpauth://` string and typing a base32 secret.

**F21. More item types: secure notes, cards, identities, Wi-Fi, PINs, license keys** (value 4, effort M, contract: entry schema, backup JSON, REST)
- Who: Proton Pass (logins, notes, cards, identities, aliases, [proton.me/pass](https://proton.me/pass)); 1Password expiring cards and passports ([support](https://support.1password.com/watchtower/)); Nitrokey entries carry login, password, comment, OTP ([docs](https://docs.nitrokey.com/nitrokey3/index)).
- Feasibility: do it as `type` plus a generic list of named, typed fields (text, secret, number, date) with per-field "typeable" flag, rather than a new struct per type. A card number or Wi-Fi password is just a secret field you can type. **Do this before release**: it changes the entry AAD/version and backup JSON (A1 in the roadmap).

**F22. Tags** (value 3, effort S): multiple tags per entry plus filter chips; skip folders (nesting adds UI cost for a few hundred entries). Tags are metadata that sit in the encrypted entry.

**F23. Monogram icons, no favicon fetching** (value 3, effort S)
- Deterministic colored monogram from the title or host. Do not fetch favicons: every fetch tells a third party which sites you use, and the AP has no internet. An offline brand-glyph pack is optional later (check icon license and trademark use **[verify]**).

**F24. Password health** (value 4, effort M, security: low-medium)
- What: weak (length plus character classes plus a small common-password list; full zxcvbn dictionaries are large, so use a compact heuristic), reused, old (needs valid time), and missing-2FA hints. Apple, 1Password and Proton all ship this ([Apple](https://support.apple.com/en-gb/120758), [1Password: checks run locally](https://support.1password.com/watchtower/), [Proton](https://proton.me/pass)); Mooltipass shows breach hints ([SecurityWeek](https://www.securityweek.com/bluetooth-enabled-mooltipass-hardware-password-manager-unveiled/)).
- Feasibility: compute on the device while unlocked and return only entry IDs and flags (`GET /api/health`), so the phone never receives all passwords at once. Reuse detection via a per-session keyed hash, not plaintext comparison on the wire.

**F25. Have I Been Pwned check** (value 4, effort M, security: privacy trade-off, opt-in)
- What: Pwned Passwords range API: send the first 5 hex chars of the SHA-1, get all matching suffixes and counts, compare locally; no key and no rate limit; `Add-Padding: true` hides how many real matches there were ([HIBP docs](https://haveibeenpwned.com/API/v3#PwnedPasswords)). 1Password integrates it ([link](https://1password.com/haveibeenpwned)).
- Feasibility: needs internet, so only in home Wi-Fi mode. Two designs: device-side HTTPS using the ESP-IDF certificate bundle (adds flash and TLS RAM; keeps passwords on the device), or phone-side JS (needs a bundled SHA-1 because `crypto.subtle` is unavailable on HTTP, and needs the phone itself to have internet; passwords pass through the browser). Prefer device-side. Opt-in per check, never automatic, and disclose that a 5-char prefix and the user's IP reach the service.

**F26. Time-based access windows** (value 2, effort M): the device has no RTC and trusts a phone/SNTP clock, so windows are weak. **Do not build.**

**F27. Audit log** (value 4, effort S-M, security: positive)
- What: ring buffer of events: unlock ok/failed, typed (entry title, what, transport), arm/cancel/expire, settings and Wi-Fi changes, new trusted browser, BLE bond added, backup/restore, OTA. Answers "did anything type while I was away?".
- Feasibility: encrypt entries with the DEK (no secrets in it, titles are metadata); while locked only counters can be kept (failed-unlock count and last time already live in NVS). Hash-chain the records to make silent edits detectable. Cap size.

**F28. Duress, wipe and hidden vault** (value 3, effort S for wipe-after-N, L for a decoy vault, security: high risk of foot-guns)
- Who: OnlyKey self-destruct PIN and duress PIN opening a second profile ([guide](https://docs.onlykey.io/usersguide/)); PasswordPump wipes after 10 failures ([Tindie](https://www.tindie.com/products/passwordpump/passwordpump-v20-with-rotary-encoder/)); SecureGen two independent vaults with separate PINs ([repo](https://github.com/makepkg/SecureGen)).
- Honest limits: an attacker with the board can dump flash and guess offline, so counters and wipes only help against someone using the UI. Plausible deniability needs two vaults of identical size in a pre-filled random region so the hidden one is indistinguishable, and an examiner who knows Keyra's design knows the feature exists. Offer in this order: (1) optional wipe after N failed unlocks (10-50), off by default; (2) panic wipe by a very long hold (for example 8 s) with a distinct LED warning, off by default; (3) decoy vault later, only with a written design.

**F29. Multi-user profiles** (value 2, effort L): one board per person costs $8. **Do not build.** A decoy vault (F28) covers the "second profile" idea if ever wanted.

### 2.4 Lock and safety behaviour

**F30. Auto-lock on USB unplug, host suspend and BLE disconnect** (value 4, effort S, security: positive)
- What: lock when the active USB host disappears or suspends (laptop lid closed), and when the BLE host disconnects. Only after the transport was previously connected, so powering from a charger does not instantly lock. TinyUSB exposes suspend/resume callbacks **[verify exact callback names in the pinned version]**.
- Who: Hideez proximity lock ([product page](https://hideez.com/products/hideez-key-5)) uses signal strength; Keyra's disconnect-based version avoids RSSI flakiness (see F31).

**F31. RSSI proximity lock** (value 2, effort M): Hideez and Everykey show how unreliable it is (lost contact, 30 ft false alarms, [MacSources](https://macsources.com/hideez-key-review-lots-promise-limited-utility/), [Best Buy reviews](https://www.bestbuy.com/site/reviews/everykey-wireless-hardware-password-manager-black/6317288)). **Do not build.**

**F32. Protected entries** (value 3, effort S): per-entry flag requiring a long hold (2 s) or a passphrase re-entry on the phone within the last 60 s before typing, for banking and root credentials. Analog: OnlyKey's challenge-code mode ([guide](https://docs.onlykey.io/usersguide/)).

**F33. Backup upgrades** (value 4, effort S-M)
- Reminder if the last backup is older than 30 days or N changes ago; backup always downloads as a file (Share API is unavailable, §2.0); verify-after-backup (offer to restore into a temporary memory buffer, check it decrypts); selective export of chosen entries (also the basis for F36). Backup and CSV export must be presence-gated (N9).

**F34. Recovery key and Shamir shares** (value 4 for recovery key, 3 for Shamir; effort M each)
- Recovery key: a random 128-256 bit key printed once at setup that wraps a second copy of the DEK (HKDF, no slow KDF needed because it is random), so a forgotten passphrase does not mean data loss while the board is intact. Needs the printed sheet stored safely.
- Shamir: split the recovery key into n shares, k needed, as Trezor does for wallet backups (SLIP-39, up to 16 shares, [Trezor](https://trezor.io/learn/advanced/standards-proposals/what-is-shamir-backup)). Gives a "heir kit" without a cloud service (compare Bitwarden Emergency Access, which needs their server, [help](https://bitwarden.com/help/emergency-access/)). GF(256) interpolation is small; do it in the web app and show shares once.

**F36. Sharing an entry with another Keyra** (value 2-3, effort S-M): Apple and Proton share across accounts ([Apple](https://support.apple.com/en-gb/120758), [Proton](https://proton.me/pass)); Keyra version is an encrypted single-entry file or QR with a one-time passphrase, reusing the backup format. Do after selective export exists.

### 2.5 Updates and distribution

**F37. OTA from the web UI with signature verification** (value 5, effort L, security: critical)
- What: upload a `.bin` in settings, device verifies a signature, writes the inactive slot, reboots with rollback. README already lists OTA as roadmap and the partition table has two 3 MB slots.
- Feasibility: ESP-IDF's `CONFIG_SECURE_SIGNED_APPS_NO_SECURE_BOOT` verifies the new image against the public key in the running app, using the Secure Boot v2 scheme (RSA-PSS 3072). It does not protect against an attacker who can write flash, only against a bad image arriving over the network, and only the first key in the signature block is used, so there is no key rotation ([ESP-IDF docs](https://docs.espressif.com/projects/esp-idf/en/stable/esp32s3/security/secure-boot-v2.html)). Full secure boot burns eFuses irreversibly (same doc); keep it optional as README says.
- **Requirements:** physical press to start (it is a presence op), refuse unsigned and downgraded versions, stream the upload straight to the OTA slot (SPEC limits bodies to 64 KiB, so this endpoint is an explicit exception), reboot only when GPIO0 is high (existing rule), mark valid only after a successful self-check (rollback otherwise), vault format migrations must be forward-only and tested. Signing key lives in CI secrets, never in the repo. Community builds sign with their own key and flash once over USB. Anyone who can ship firmware can read the vault while unlocked, so this feature is as sensitive as the crypto code.

**F38. Browser flasher** (value 4, effort S-M): ESP Web Tools installs firmware over Web Serial from a manifest hosted on an HTTPS page, in Chrome, Edge and Firefox ([ESP Web Tools](https://esphome.github.io/esp-web-tools/)). Removes the esptool step from the README and helps adoption. It runs on a separate HTTPS site, so it is not constrained by §2.0.

**F39. Verify-this-firmware** (value 2, effort M): show the running image hash on the phone and compare to the release page. Marginal without secure boot; low priority.

### 2.6 Connectivity and integrations

**F40. Browser extension that tells Keyra which site is focused** (value 5, effort L, security: medium)
- What: a small extension reads the active tab's hostname and asks Keyra for matching accounts; Keyra arms the best match (or shows a pick list on the phone); the user still presses the physical button to type. The extension never receives secrets.
- Who: Mooltipass (browser extensions plus Moolticute, [SecurityWeek](https://www.securityweek.com/bluetooth-enabled-mooltipass-hardware-password-manager-unveiled/)), KeePassXC-Browser (uses native messaging, [issue](https://github.com/keepassxreboot/keepassxc/issues/287)), Trezor Password Manager extension ([Bitcoin.com](https://news.bitcoin.com/trezor-unveils-password-manager/)).
- Feasibility: an MV3 extension with host permission for `http://keyra.local/*` can reach Keyra over the LAN and is exempt from the Local Network Access prompt ([Chrome blog](https://developer.chrome.com/blog/local-network-access)). The computer must be on the same network as Keyra, so this realistically means home Wi-Fi mode (SPEC §8.2), not the device AP. Reuse the trusted-browser pairing (button approval) to authorise the extension. SPEC §5 rejects foreign `Origin` values, so the extension's origin needs an explicit pairing-time allow. Safari needs an Xcode wrapper. Do Chrome/Edge/Firefox first.
- **Requirements:** matching is done by Keyra (hostname only is sent, never full URL with query); extension holds a revocable pairing token, no vault data; works without the extension (it is optional, preserving the "nothing to install" promise in README).

**F41. Home Assistant and Matter** (value 1): no real use case, and Matter would add a large stack for nothing. **Do not build.** (My assessment.)

**F42. NFC** (value 3 for tag, 1 for reader)
- NFC sticker on the enclosure or desk holding the URL `http://keyra.local`: the phone OS reads NDEF and opens it, no firmware and no Web NFC needed (Web NFC is Android Chrome only and secure-context only, [MDN](https://developer.mozilla.org/en-US/docs/Web/API/Web_NFC_API)). Effort S, documentation only. Background tag reading on iPhone **[verify supported models]**.
- An NFC reader on the board (tap a ring or card to unlock) is extra hardware and soldering: **do not build in the core.**

**F43. Wi-Fi join QR** (value 3, effort S): the web app shows a QR using the `WIFI:T:WPA;S:<ssid>;P:<password>;;` syntax so a second phone, tablet or laptop joins by scanning ([ZXing format](https://github.com/zxing/zxing/wiki/Barcode-Contents); supported on Android and iOS 11+ per the same guidance). Needs a small JS QR generator. The password is shown only after unlock.

**F44. Display, rotary and multi-button add-ons** (value 5 for security, effort L, hardware-dependent)
- Who: Mooltipass scroll wheel and OLED, PasswordPump OLED plus rotary ([Tindie](https://www.tindie.com/products/passwordpump/passwordpump-v20-with-rotary-encoder/)), SecureGen TFT ([repo](https://github.com/makepkg/SecureGen)), OnlyKey PIN on the device ([guide](https://docs.onlykey.io/usersguide/)).
- Why it matters: a trusted display shows what is about to be typed ("GitHub, password") independent of the phone, so a compromised phone session cannot swap the target; it also allows phone-free use.
- Feasibility: the stock DevKitC-1 stays the supported baseline. For a display variant, the LilyGo T-Dongle-S3 is a USB-A stick with ESP32-S3, 16 MB flash, no PSRAM, a 0.96 in 160x80 LCD, TF slot, BOOT button and an APA102 LED (not WS2812, so the LED driver needs a port) ([LilyGo wiki](https://wiki.lilygo.cc/products/t-dongle-series/t-dongle-s3/), [specs](https://www.sdrstore.eu/lilygo-t-dongle-s3-esp32-s3-comprehensive-review/)). That is the natural "Keyra Stick". Add a `keyra_display` component behind a board profile; do not make the core depend on it. Rotary/e-ink: later, only if a PCB happens.

**F45. SD or USB mass-storage "recovery drive"** (value 2, effort L, security: conflicts with the product promise)
- ESP-IDF supports TinyUSB MSC on S3, with SD-MMC or SPI flash ([example](https://github.com/espressif/esp-idf/blob/master/examples/peripherals/usb/device/tusb_msc/README.md), [ESP-USB docs](https://docs.espressif.com/projects/esp-usb/en/latest/esp32s3/usb_device.html)). But README promises "Keyra presents only a keyboard", and a storage interface gives the host a new attack surface and a re-enumeration quirk. The phone already downloads encrypted backups. **Do not build.** A DevKitC-1 has no SD slot, which also breaks the no-soldering rule.

### 2.7 UX and onboarding

**F50. Presence-gated reveal, copy and export** (see N9).

**F51. Phone autofill of the master passphrase** (value 3, effort S): render the unlock form with a username field and the right `autocomplete` attributes so iOS/Android password managers can offer to fill it, giving biometric unlock without WebAuthn. It stores the master passphrase in the phone's manager, which weakens the model; make it a documented user choice, not a default **[verify autofill behaviour on http://keyra.local]**.

**F52. Copy buttons without the Clipboard API** (value 3, effort S): confirm that "Copy" works on HTTP in iOS Safari and Android Chrome via the `execCommand` fallback, and show the manual select-and-copy path if it fails (see §2.0).

**F53. Per-host profiles** (value 4, effort S after F02): layout, key delay, optional layout-switch chord and sequence overrides stored per USB host class and per BLE bond. Needed because the same Keyra types into a US work PC, an Arabic-layout home Mac and an iPhone ([Mooltipass BLE layout bug](https://github.com/mooltipass/moolticute/issues/848) is exactly this).

**F54. More CSV importers** (value 4, effort S each): Firefox, LastPass, Dashlane, KeePassXC, Proton Pass, Enpass, Edge (same as Chrome). Each is a column mapper plus a sample-file test **[verify each export schema from a real sample]**. KDBX import itself (Argon2 plus AES/ChaCha plus XML in the browser) is heavy; skip, since KeePassXC exports CSV.

**F55. Login playlist** (value 2, effort M): ordered list of entries, one press each. Cute, rarely needed; later.

---

## 3. Ideas nobody (I found) has built

Scope of the claim: I surveyed the products in §1 plus GitHub, Hacker News and vendor docs, about 40 searches. "Not found" means I saw no evidence in those sources, not that it does not exist. Each idea leans on what is unique to Keyra: a physical press gates typing, a phone UI exists, there is BLE as well as USB, and the device is cheap enough to own two.

**N1. Zero-tap login** (builds on F40; value 5, effort L)
- The browser extension tells Keyra the hostname; Keyra arms the best-matching entry and its sequence automatically; the LED turns blue; the user clicks the field and presses the button. The phone is never touched. Mooltipass and KeePassXC-Browser fill or ask on the computer; here the only human action is the physical press, so the gating model stays intact.
- Risks: needs the computer on the same LAN (home Wi-Fi mode); a malicious page cannot read anything (extension is the only client), but a malicious extension could arm the wrong entry, so the arm preview must show the entry title on the phone and the device is still one-shot with a 60 s expiry.

**N2. LED handshake against evil-twin Wi-Fi** (value 4, effort S-M)
- Threat: the link is plain HTTP, and the master passphrase is typed into a page that anyone broadcasting "Keyra-XXXX" with a guessed or public factory password (`keyra1234` is public until setup) could imitate. Defence: on the unlock page, the page requests a challenge; the real device flashes its LED in a short random colour sequence for 2 s and the page displays the same coloured chips; the user compares before typing the passphrase. A clone without the real board cannot light the real LED.
- Risks: users who do not look defeat it; it does not stop a relay to a real Keyra the attacker controls (a clone-board relay). It is a cheap speed bump, not authentication; see N3 for the real fix.

**N3. Encrypted link without HTTPS** (value 5, effort XL, security: high)
- Today passphrase and all vault data cross the link in clear inside WPA2 (README states this as a limit). Replace the passphrase POST with a PAKE or OPAQUE-style exchange computed in the browser (bundled JS, since `crypto.subtle` is unavailable), derive a session key, and wrap every API body in an AEAD (X25519 plus ChaCha20-Poly1305 via a small audited JS library **[verify library choice and size]**). Pin the device's public key in the browser on first setup (trust on first use) so later loads detect a different board. This removes the "someone on the Wi-Fi sees everything" limit and matters more once home Wi-Fi mode puts Keyra on a shared LAN.
- Risks: crypto protocol design is where agents and humans make mistakes; needs a written protocol, test vectors and ideally review before release. Handle the 1.2 s KDF cost and the PAKE verifier (an offline-guessable verifier exists on flash, same as the vault KDF today). Do not roll a custom protocol without review.

**N4. Keyra Twin** (value 5, effort L, security: medium)
- Pair two boards ($10 each) as live mirrors: a one-time pairing where both devices demand a button press within the same minute and the phone shows a short code to compare, then an encrypted device-to-device sync over Wi-Fi (one joins the other's AP or both use home Wi-Fi). Lose one board, the other already has everything. Hardware keys cannot do this; users are told to buy and register a second key ([MakeUseOf](https://www.makeuseof.com/passkeys-are-great-no-one-tells-you-about-catch-until-its-too-late/)), and Mooltipass needs a card clone plus a desktop backup ([manual](https://fccid.io/2AYPT-MBLE1/User-Manual/User-Manual-5088694.pdf)).
- Risks: conflict resolution (last-writer-wins per entry with timestamps is enough), clock trust, and the second board is a second theft target. Needs F27 (audit log) and the encrypted-channel code from N3 or a mbedTLS ECDH session. Positions the product as "buy two".

**N5. Arm-only capability tokens: Siri, Shortcuts, NFC stickers, QR cards** (value 4, effort M)
- A revocable token that can only arm one named entry's sequence, never read data. Put it behind an iOS Shortcut ("Hey Siri, GitHub login" or Back Tap), an NFC sticker on the monitor stand (the OS reads the URL tag, [no Web NFC needed](https://developer.mozilla.org/en-US/docs/Web/API/Web_NFC_API)), or a printed QR card. Then the user presses the Keyra button. Speed of a hotkey, security of a physical press.
- Risks: a token in a URL lands in browser history; make it single purpose, rate-limited, revocable, shown in the trusted-browsers-style list, and unusable for anything but arming. Only works while the phone is on the Keyra network.

**N6. Rotate-by-typing with two-phase commit** (value 4, effort M)
- Password changes are the worst chore. Add a "change password" sequence on an entry: Keyra generates a new password, stages it (not saved as current), types `{OLD}{TAB}{NEW}{TAB}{NEW}{ENTER}` on button press, and the phone asks "Did the site accept it?" Only on yes does it commit and move the old one to history. No clipboard, no copy-paste. KeePass-style auto-type can type fields but does not stage and commit.
- Risks: sites with unusual flows; keep old and staged passwords until confirmed; needs password history (A1). Pair with "rotate after use" on untrusted computers.

**N7. Layout Doctor and host profiles** (value 5, effort M)
- A 30-second guided check: Keyra types a probe string that exposes the common layout differences (punctuation row, Y/Z, A/Q, `\ | @ # £ " ~`, and a bilingual-toggle test), the phone shows 4-6 pictures of possible results, the user taps the one matching their screen, and Keyra stores the layout (and slow-mode need) in a host profile keyed by USB class or BLE bond. Mooltipass cannot auto-detect layouts and users file bug after bug ([Google group](https://groups.google.com/g/mooltipass/c/64Sq927t_RI)); OS guessing from enumeration patterns exists in patents ([example](https://patents.google.com/patent/US20120054372A1)) and the host reports Caps Lock state back over the HID LED output report ([CTRL-ALT-LED paper](https://arxiv.org/pdf/1907.05851)), but neither reveals the layout, so the human-in-the-loop picture quiz is the honest answer.
- Risks: probe strings must be harmless (no Enter, no chords); user error; OS guess is a hint only.

**N8. Arm-time host binding** (value 3, effort S)
- When you arm a type action, Keyra records which transport and host it is connected to (USB session or BLE address). If the host changes before the press (USB re-enumerates, BLE reconnects to another device), the action cancels. Prevents typing into a different machine after a cable swap or a BLE auto-reconnect. KeePassXC users ask for the same kind of cancel when the target window changes ([#4668](https://github.com/keepassxreboot/keepassxc/issues/4668)).
- Risks: false cancels when a host wakes from suspend and re-enumerates; show a clear "host changed, arm again" message.

**N9. "Blind phone": presence-gated reveal and export** (value 4, effort S-M, security: strongly positive)
- Today `GET /api/entries/{id}` returns the plaintext password to the phone and backup/CSV leave the device without a press. Make reveal, copy, CSV export and backup download each need a physical press (with a short grace window), so a stolen session cookie, a phished page (N2) or an XSS cannot bulk-exfiltrate the vault. The existing flow stays: typing never exposes the password to the phone. Trezor's Password Manager is praised for exposing only one secret at a time with physical consent ([HN](https://news.ycombinator.com/item?id=25293611)); Keyra extends it to a phone UI.
- Risks: friction; offer a "relaxed" setting that is off the default only after a clear warning. API contract change (new 202 flows), so do it before release.

**N10. Couch mode** (value 3, effort M)
- Templates for devices with on-screen keyboards or login prompts: Smart TVs, consoles, streaming boxes. Sequences with navigation keys and delays (`{DOWN}{DOWN}{ENTER}` style), typed over BLE to the TV or over USB to a console, with a "Wi-Fi password" item type (F21). Typing a long password with a TV remote is a universal complaint; a pocket vault that does it is a good demo.
- Risks: each device family needs real-device testing; ship as community-contributed templates, not a promise.

**N11. Layout-proof generator** (value 4, effort S)
- Generate passwords only from characters that produce the same character on every layout the user selected, so the password types correctly everywhere. This generalises Yubico's ModHex idea (16 characters, `bcdefghijklnrtuv`, same HID usage IDs across Latin layouts, [docs](https://docs.yubico.com/yesdk/users-manual/application-otp/modhex.html)) to the intersection of the user's actual layouts. A 24-character password from 16 symbols is about 96 bits, which is plenty for a random password.
- Risks: websites with composition rules (needs digits or symbols); offer "layout-proof" as a generator preset, not the only mode.

**N12. Burn-after-typing entries** (value 3, effort S)
- Entry flag "delete after N types or after T minutes". Use for a guest's Wi-Fi key, a one-time recovery code, or a temporary password you are handing to a colleague's machine. A typing analogue of Bitwarden Send expiry ([blog](https://bitwarden.com/blog/quick-tips-to-secure-and-share-your-information/)). Needs a trustworthy clock for time-based expiry; count-based expiry does not.
- Risks: accidental deletion; require confirmation when creating and show a pending badge.

**N13. Tripwire entries** (value 2, effort S)
- A decoy entry that looks attractive ("Bank old") and, if revealed or armed, logs a high-priority audit event and locks the vault. Cheap tamper evidence for a borrowed unlocked phone. Weak alone; becomes meaningful with F27 and F28.
- Risks: false alarms if the owner taps it; clearly mark it in the owner's own list.

---

## 4. Recommendation and roadmap

### 4.1 Principles

1. **Land schema and API-shape changes now.** Keyra is unreleased; after the first release the entry format, backup JSON and REST API are published contracts. Anything that changes them (F01, F21, F22, N9, N6 history, N12) goes first.
2. **Protect the three-minute first run.** New features live behind Settings or Advanced; the first-run path (passphrase, Wi-Fi password, press) does not grow.
3. **Everything that types stays button-gated and one-shot.** New ways to arm (extension, tokens, sequences) do not change that.
4. **No feature that needs the cloud, an account or a desktop install in the default path.** Optional helpers are fine.
5. **Tests as for existing core:** host tests for new vault/keymap/sequence code, e2e for new screens.

### 4.2 Wave A: next 1-2 days of agent work (high value, low-medium effort)

Sequencing note: A1 changes the entry format and must merge first; A2 and A3 depend on it; A4-A8 are independent and can run in parallel worktrees.

| # | Item | Effort | Acceptance criteria | Security requirements |
|---|---|---|---|---|
| A1 | **Entry schema v2**: `type`, named typed fields, tags, per-entry sequence, layout override, password history, burn-after flag (F21, F22, N6/N12 groundwork) | M | Host tests round-trip every field; backup JSON carries a `version`; AAD or entry version bumped; SPEC §4-5 updated in the same change; web edit screen shows tags and extra fields; CSV import still passes | Secrets stay inside the encrypted entry; no new plaintext metadata on flash; migration refuses corrupt input |
| A2 | **Auto-type sequences** with `{PRESS}` (F01, F06, F08) | M | Grammar documented in SPEC; existing four buttons become presets with identical behaviour; tests reject chords, unknown tokens and over-length expansions; phone shows masked preview while pending; keys always released on every error path; Caps Lock wrapper unchanged | Whitelist grammar only (see F01); sequences from import/restore pass the same validator; typing still requires press, 60 s expiry |
| A3 | **Layouts** (US, UK, DE, FR, ES, IT, Dvorak, Colemak) plus layout-proof generator (F02, N11, F53 for BLE vs USB) | M | One source table generates the C++ map; host test covers every declared character per layout; unsupported characters give the existing `unsupported_char` error; separate layout setting for USB and BLE output; HID country code and a stable product string set and tested on one Mac **[verify]** | A layout bug types the wrong password: label any layout not confirmed on real hardware "experimental" in the UI |
| A4 | **Auto-lock on USB unplug/suspend and BLE disconnect** plus **arm-time host binding** (F30, N8) | S | Unplugging or suspending after a connected session locks within 2 s; charger-only power does not lock; changing host cancels a pending action with a clear message; tests in the action state machine | Lock path zeroizes keys as today |
| A5 | **TOTP QR import by photo** (F20) | S-M | Decodes a sample plain `otpauth://` QR and a Google Authenticator export QR with several accounts in e2e; works with the AP offline; the image is never sent anywhere; paste-URI and manual base32 paths remain | None beyond client-side handling; clear the image from memory after decode |
| A6 | **Presence-gated reveal, copy-on-device flow, export and backup** plus **backup reminder** (N9, F33) | S-M | Reveal/export/backup return 202 and complete on a press (short grace window); a "relaxed" setting exists, default strict; UI nags when backup is older than 30 days; SPEC updated | Closes bulk exfiltration through a stolen session |
| A7 | **Remote keyboard** (F05) | S | Text box typed on press; 256-char cap; control characters rejected; distinct LED/phone state; text zeroized afterward; works over USB and BLE | Press-gated, never logged |
| A8 | **Password health** on device (F24, weak/reused/old) | M | `GET /api/health` returns IDs and flags only; unit tests for weak/reused/old with fixed fixtures; "old" hidden when time is not valid | No plaintext leaves the device |
| A9 | Docs-only quick wins: NFC sticker recipe (F42), Wi-Fi join QR (F43), autofill attributes check (F51), copy-button check (F52) | S | README/HARDWARE section; QR renders only after unlock; copy works on iOS and Android | Wi-Fi QR shows the AP password, so only after unlock |

If time is short, drop A8 and A9 first; do not drop A1 (it blocks contract stability).

### 4.3 Wave B: next week

| # | Item | Effort | Acceptance criteria | Security requirements |
|---|---|---|---|---|
| B1 | **OTA with signature verification** (F37) | L | Upload in Settings, presence press, signature check, write inactive slot, reboot, self-test, rollback on failure; unsigned and downgrade refused; CI signs release images; docs say how to sign your own build; flash and verify on a real board | `CONFIG_SECURE_SIGNED_APPS_NO_SECURE_BOOT` semantics and limits stated ([ESP-IDF](https://docs.espressif.com/projects/esp-idf/en/stable/esp32s3/security/secure-boot-v2.html)); key custody documented; no OTA without press |
| B2 | **Layout Doctor, host profiles, Arabic-101 and layout-switch token** (N7, F53, F02a) | M | Guided quiz stores a profile per USB class and BLE bond; Arabic table passes character round-trip; documented behaviour for bilingual hosts; real-hardware report from an Arabic-layout Windows or macOS machine | Probe strings contain no Enter or chords; the switch chord is the only chord allowed and is per-host opt-in |
| B3 | **Browser flasher** (F38) | S-M | One HTTPS page flashes a release on Chrome/Edge; README points to it; fallback to esptool remains | Manifest and binaries come from the signed release |
| B4 | **Audit log** (F27) | S-M | Events listed in F27 recorded, encrypted at rest, capped, hash-chained; shown in Settings; no secrets inside | Locked-state events limited to counters |
| B5 | **Recovery key** (F34, first half) | M | Generated at setup or later in Settings, shown once with print layout; a test restores access with a wiped passphrase; documented risk | Random 128+ bit key only; never stored in clear on device |
| B6 | **LED handshake** (N2) | S-M | Real device flashes a nonce-derived colour sequence on unlock page load; page shows the chips; automated test of nonce binding; docs state the limit (not authentication) | Nonce single-use, short-lived |
| B7 | **Arm-only tokens** (N5) | M | Token arms exactly one entry; cannot read; revocable; rate-limited; listed in settings; Shortcuts recipe in docs | Scope enforced server-side, token hashed at rest like trusted browsers |
| B8 | **Browser extension and Zero-tap login** (F40, N1) | L | Chrome/Edge/Firefox extension, paired with a button press, sends hostname only, Keyra arms the match; works on home Wi-Fi; extension holds no secrets; optional to use | Foreign-origin rule updated narrowly; revocable pairing; no full URLs |
| B9 | **HIBP check** (F25) | M | Opt-in, device-side range query over home Wi-Fi with padding header; shows count per entry; privacy note in UI | Only 5-char prefix leaves; user warned; no auto-run |
| B10 | **Rotate-by-typing** (N6) and **burn-after-typing** (N12) | M | Staged password committed only after phone confirms; history keeps old; burn flag deletes after N types | Staged secrets encrypted, wiped on cancel |
| B11 | **Protected entries, wipe-after-N, CSV importer pack** (F32, F28 parts 1-2, F54) | M | As described in the catalogue; importers verified against real sample files | Wipe features off by default with explicit warning |
| B12 | **BLE test matrix and fixes** (P3) | M | Pairing/reconnect pass on Windows 11, macOS, iOS, Android, Linux, with NVS-persisted bonds and RPA resolution; published results table | Pairing remains button-gated |

### 4.4 Wave C: later, each needs its own design note

| # | Item | Effort | Why later |
|---|---|---|---|
| C1 | **FIDO2/U2F** (F10), start with U2F key-handle design | L then XL | Highest value and highest risk; requires stable vault, backup, OTA and licensing check; hardware has no secure element |
| C2 | **Encrypted link without HTTPS** (N3) | XL | Needs a written protocol and review; do not improvise |
| C3 | **Keyra Twin** (N4) | L | Depends on B4 and C2 channel work |
| C4 | **Keyra Stick display variant** (F44) | L | Board profile, display component, trusted confirm screen |
| C5 | **Shamir shares and heir kit** (F34 second half) | M | Needs B5 first |
| C6 | **Decoy/hidden vault** (F28 part 3) | L | Needs a design for indistinguishable fixed-size regions; high foot-gun risk |
| C7 | **Couch mode templates** (N10), brand glyph pack (F23), login playlist (F55), tripwire (N13), entry sharing (F36) | S-M each | Nice-to-have; add on demand |

### 4.5 Tempting features NOT to build (and why)

| Feature | Why not |
|---|---|
| Cloud sync, accounts, subscriptions | Contradicts the "no cloud, no account" promise; the Twin (N4) covers the need locally |
| Desktop "Keyra Bridge" helper | Breaks "nothing to install"; the optional extension (B8) gives the same benefit for browsers with no secrets |
| Favicon fetching | Leaks which sites you use; monograms (F23) are free |
| RSSI proximity lock | Reported as unreliable by users ([MacSources](https://macsources.com/hideez-key-review-lots-promise-limited-utility/), [Best Buy reviews](https://www.bestbuy.com/site/reviews/everykey-wireless-hardware-password-manager-black/6317288)); disconnect-based lock (A4) is deterministic |
| USB mass-storage recovery drive, SD slot | Gives the computer a storage interface, against the README promise; DevKitC-1 has no SD slot; phone backup already works |
| SSH/GPG agent, HOTP, Yubico OTP, challenge-response, static-slot emulation | Needs host software or serves a niche; part of it returns for free if CTAP2 hmac-secret ships |
| Multi-user profiles | One board per person costs about $8; decoy vault covers duress |
| Time-based access windows | No RTC, clock is client or SNTP supplied, false sense of security |
| Home Assistant and Matter | No use case; large stack |
| Native KDBX import/export | Heavy crypto in the browser for little gain; KeePassXC exports CSV |
| NFC reader hardware in the core | Extra hardware and soldering; sticker tags (F42) deliver most of the value |
| Typing cadence randomisation to evade detection | Wrong direction; managed hosts that block unknown HID devices should be respected (P10). Offer slow mode only for reliability |
| Biometric unlock inside the page | WebAuthn is unavailable on HTTP; use the phone's own autofill (F51) if the user wants it |
| Folders | Tags give the same organisation with less UI |
| Over-promising FIDO | Without a secure element say "not certified"; do not market it as a YubiKey replacement |

### 4.6 Open items to verify before building on this document

- Whether the pinned `esp_tinyusb` supports a second HID interface and boot protocol on ESP-IDF 6.0 (F03, F10) and exposes suspend/resume callbacks (F30).
- `<input type="file" capture>` behaviour on iOS Safari and Android Chrome for QR photos (F20), `execCommand('copy')` on both (F52), and autofill on `http://keyra.local` (F51).
- That macOS does not show Keyboard Setup Assistant for Keyra and what descriptor values help (P9).
- Licence terms of any FIDO code considered for reuse (Pico-Fido is AGPLv3 per [its page](https://www.picokeys.com/pico-fido/)).
- The CSV export schemas for each importer in F54, from real samples.
- Prices of OnlyKey, Hideez and Flipper (not checked here).
- Real-world numbers for Wave B1: upload speed of a 1-2 MB image over the device AP, and the flash cost of adding the TLS certificate bundle for F25.
