<div align="center">

<img src="docs/images/hero.png" alt="Keyra: a pocket password vault that types for you" width="100%">

# Keyra

**A pocket hardware password vault. Your passwords stay on the device, you manage them from your phone, and Keyra types them for you only when you press its button.**

[![CI](https://github.com/hasanalaaa/keyra/actions/workflows/ci.yml/badge.svg)](https://github.com/hasanalaaa/keyra/actions/workflows/ci.yml)
[![License: MIT](https://img.shields.io/badge/license-MIT-0B57F0.svg)](LICENSE)
[![ESP-IDF 6.0](https://img.shields.io/badge/ESP--IDF-6.0-E7352C.svg)](https://docs.espressif.com/projects/esp-idf/en/stable/esp32s3/)
[![Platform: ESP32-S3](https://img.shields.io/badge/platform-ESP32--S3-2F6BFF.svg)](docs/HARDWARE.md)

[How it works](#how-it-works) ·
[Quick start](#quick-start) ·
[Security](#security-model) ·
[Hardware](#hardware) ·
[FAQ](#faq) ·
[بالعربي](README.ar.md)

</div>

---

## بالعربي

**Keyra** خزنة كلمات مرور صغيرة بحجم الـ USB، مبنية على لوحة ESP32-S3. تبقى كلماتك مشفّرة داخل الجهاز، وتديرها من متصفح هاتفك عبر شبكة الجهاز نفسه (`http://keyra.local`)، وعند الحاجة **يكتب Keyra اسم المستخدم وكلمة المرور** في الكمبيوتر كأنه لوحة مفاتيح، **ولكن فقط بعد أن تضغط زر الجهاز بيدك**. لا حساب، لا سحابة، لا إنترنت.

اقرأ الشرح كاملاً بالعربية: **[README.ar.md](README.ar.md)**

---

## Why Keyra

Most password managers live in the same computer and browser that attackers target. Keyra moves the secret somewhere else.

- **Your vault never lives on the computer.** Nothing is stored or synced there: the computer only sees a keyboard typing the one credential you chose, at the moment you chose. No browser extension, no clipboard, no app to install.
- **Malware cannot make it type.** Every action needs a physical press of the button on the device. No press, no typing.
- **No cloud, no account, no subscription.** The vault is encrypted on the device and managed over the device's own Wi-Fi. Nothing leaves your desk.
- **Works on any computer.** If it accepts a USB keyboard (a work laptop, a locked-down kiosk, a TV, a console), Keyra works there. Nothing to install.
- **Open and cheap.** MIT licensed firmware for a development board that costs a few dollars. Read it, build it, change it.
- **Easy enough to use daily.** A phone-first app in Arabic and English, built to feel as simple as the password manager on your phone.

Keyra is a convenience-and-isolation device, not a magic shield. Read the [security model](#security-model) for exactly what it does and does not protect.

## How it works

<div align="center">
<img src="docs/images/how-it-works.png" alt="Plug in, pick an account on your phone, press the button" width="100%">
</div>

1. **Plug in.** Connect Keyra's USB port to the computer. It shows up as a keyboard.
2. **Join the Wi-Fi.** On your phone, join the network **Keyra-XXXX** and open **http://keyra.local**. (No sign-in sheet pops up; see the [FAQ](#faq).)
3. **Pick an account and what to type.** Unlock with your master passphrase, tap an account, and choose **Username**, **Password**, **Both** or **Code** (2FA).
4. **Press the button.** Click the login field on your computer, press Keyra's button, and it types. The LED flashes green and your phone says **Typed**.

A long press (1.5 seconds) cancels a pending action, or locks the vault if nothing is pending.

## Features

| | |
|---|---|
| **Types like a keyboard** | USB HID keyboard (US layout). Handles Caps Lock and always releases keys, even on errors. |
| **Phone-first web app** | Installable to the home screen. Search, favorites, recents, password generator and strength meter. |
| **Arabic and English** | Full RTL support, auto-detected, switchable at any time. |
| **2FA codes** | Built-in TOTP (SHA-1/256/512, 6 or 8 digits). Keyra can type the code too. |
| **Encrypted vault** | PBKDF2-HMAC-SHA256 (about 1.2 s on the device) and per-entry AES-256-GCM. |
| **Unlock rate limiting** | Failed attempts are counted before the key derivation runs; power-cycling does not reset the delay. |
| **Import** | Move from Apple Passwords, Chrome, Bitwarden or 1Password CSV exports. |
| **Encrypted backup** | A passphrase-protected JSON file; restore by merging or replacing. |
| **Physical confirmation** | Typing, setup, Wi-Fi changes and factory reset all need a button press. |
| **Status LED** | Locked, ready, typing, success and error, readable at a glance. |
| **Auto-lock** | Locks after idle (1 to 120 minutes) and zeroizes keys in RAM. |
| **No lock-in** | Standard formats, open protocol ([docs/SPEC.md](docs/SPEC.md)), MIT license. |

## Screenshots

<!-- Screenshots are generated into web/screenshots/ by the web app's e2e run. Replace this block with the table below once the files exist.

| Unlock | Vault | Account | Ready to type |
|:--:|:--:|:--:|:--:|
| <img src="web/screenshots/unlock.png" width="200"> | <img src="web/screenshots/vault.png" width="200"> | <img src="web/screenshots/account.png" width="200"> | <img src="web/screenshots/ready.png" width="200"> |

-->

_Screenshots coming with the first release._

## Security model

The short version. The full threat model, cryptographic design and rate-limit schedule are in **[SECURITY.md](SECURITY.md)**.

**What Keyra does**

- Encrypts every entry with AES-256-GCM under a random data key, itself protected by a key derived from your passphrase (PBKDF2-HMAC-SHA256, calibrated to about 1.2 s).
- Rate-limits unlock attempts and counts them durably.
- Types only after a physical button press. The computer never gets a storage or network channel from Keyra.
- Keeps decrypted data in RAM only while unlocked, and zeroizes it on lock.

**What it does not do (honest limits)**

- **The phone link is plain HTTP over WPA2**, not TLS. Someone who knows your Keyra Wi-Fi password and is on the network can observe the traffic. Keep the Wi-Fi password private.
- **No secure element.** With physical access to the board, an attacker can dump the flash and guess your passphrase offline. Use a long passphrase.
- **Flash encryption and secure boot are optional** and off by default. They are irreversible eFuse steps ([docs/HARDWARE.md](docs/HARDWARE.md#optional-hardening-irreversible)).
- **Keyra has not been independently audited.**
- A keylogger on the computer can still see what Keyra types, as with any keyboard.

If you need to report a vulnerability, please use a **private GitHub Security Advisory** as described in [SECURITY.md](SECURITY.md#reporting-a-vulnerability).

## Hardware

<div align="center">
<img src="docs/images/device.png" alt="Concept render of a Keyra enclosure" width="70%">
<br><sub>Concept render of a future enclosure. Today Keyra runs on a bare ESP32-S3 dev board.</sub>
</div>

Keyra runs on a stock **ESP32-S3** dev board with at least **8 MB flash**. No soldering.

| | |
|---|---|
| **Reference board** | ESP32-S3-DevKitC-1 **N16R8** (PSRAM is optional) |
| **Button** | GPIO0 (the board's **BOOT** button) |
| **Status LED** | WS2812 on **GPIO38** (DevKitC-1 v1.1) or **GPIO48** (v1.0), set with `CONFIG_KEYRA_LED_GPIO` |
| **USB** | Plug the board's native **USB** port into the computer (not the UART port) |

Pin details, flashing options, enclosure ideas and the optional hardening steps: **[docs/HARDWARE.md](docs/HARDWARE.md)**.

## Quick start

### Option A: flash a prebuilt release

1. Download the release files from the [Releases page](https://github.com/hasanalaaa/keyra/releases).
2. Put the board in download mode (hold **BOOT**, plug in the **USB** port, release **BOOT**), or use the **UART** port, which needs no button.
3. Flash with [esptool](https://docs.espressif.com/projects/esptool/en/latest/) (offsets are from `flasher_args.json`):

```sh
esptool.py --chip esp32s3 -p <PORT> --before default-reset --after hard-reset \
  write_flash --flash-mode dio --flash-size 8MB --flash-freq 80m \
  0x0 bootloader.bin 0x8000 partition-table.bin 0xf000 ota_data_initial.bin 0x20000 keyra.bin
```

### Option B: build from source

You need [ESP-IDF 6.0.x](https://docs.espressif.com/projects/esp-idf/en/stable/esp32s3/get-started/) installed and exported in your shell (CI uses v6.0.2).

```sh
git clone https://github.com/hasanalaaa/keyra.git
cd keyra/firmware

# Release build: plain keyboard, logging off. Recommended for daily use.
idf.py -B build-release -D SDKCONFIG_DEFAULTS="sdkconfig.defaults;sdkconfig.release" build
idf.py -B build-release -p <PORT> flash

# Dev build: adds a USB serial log port and the 1200-baud reboot-to-ROM hook.
idf.py build
idf.py -p <PORT> flash monitor
```

The built web app is committed in `firmware/components/keyra_api/www/`, so you do **not** need Node.js to build the firmware. To rebuild the web app and refresh those files:

```sh
npm --prefix web ci && npm --prefix web run build
```

### First-time setup

1. Plug Keyra's **USB** port into a computer.
2. On your phone, join Wi-Fi **Keyra-XXXX** (the last four characters of the board's address). The factory password is **`keyra1234`**, and it is public. Setup makes you change it.
3. Open **http://keyra.local** (or `http://192.168.4.1`). Tap **Share, then Add to Home Screen** to make it one tap next time.
4. Follow the three steps: choose a **master passphrase** (10 characters or more; longer is better), choose a **new Wi-Fi password**, then **press the button on Keyra** to prove you are holding it.
5. Add accounts, or import a CSV from your current password manager (delete the CSV afterwards, it is unencrypted).

## Project layout

```
keyra/
├── firmware/                  ESP-IDF 6.0 project (C++17)
│   ├── main/                  app_main: wiring only
│   ├── components/
│   │   ├── keyra_vault/       crypto, encrypted storage, TOTP, backup (host-tested)
│   │   ├── keyra_hid/         TinyUSB keyboard, typing engine, dev CDC port
│   │   ├── keyra_io/          button (GPIO0) and RGB status LED
│   │   ├── keyra_net/         Wi-Fi access point, DNS, mDNS (keyra.local)
│   │   └── keyra_api/         HTTP server, REST API, sessions, pending-action state machine
│   ├── test/host/             host test runner (CMake + ctest, no board needed)
│   ├── partitions.csv         8 MB layout: 2 app slots + LittleFS vault
│   └── sdkconfig.defaults / sdkconfig.release   dev and release profiles
├── web/                       Preact + TypeScript app, built into one gzipped file
├── tools/                     devctl.py: flash, reset and log over native USB
├── docs/                      SPEC.md, DESIGN.md, HARDWARE.md, IMAGE_PROMPTS.md, images/
└── .github/                   CI, issue and PR templates
```

[docs/SPEC.md](docs/SPEC.md) is the contract between firmware and web app (REST API, behavior, security model). [docs/DESIGN.md](docs/DESIGN.md) is the design system.

## Development

```sh
# Host tests: vault, TOTP, key map, action state machine. No board needed.
# Requires CMake, a C++17 compiler and OpenSSL 3 headers (libssl-dev, or openssl@3 on Homebrew).
cmake -S firmware/test/host -B build/host && cmake --build build/host && ctest --test-dir build/host

# Web: typecheck and unit tests
npm --prefix web ci
npm --prefix web run typecheck
npm --prefix web test

# Web UI without a board: a mock of the device API
npm --prefix web run mock
```

CI runs the host tests, the web checks and both firmware profiles on every push and pull request. See [CONTRIBUTING.md](CONTRIBUTING.md).

## Roadmap

Ideas, not promises. Priorities follow what real users on real boards report.

- [x] 0.1: encrypted vault, USB typing, phone app (EN/AR), TOTP, import, backup, CI
- [ ] Tested board matrix and a prebuilt release for each
- [ ] Browser-based flashing from the release page
- [ ] More keyboard layouts (the key map is US-only today)
- [ ] Printable enclosure and a purpose-built PCB
- [ ] Over-the-air firmware updates (the partition table already has two app slots)
- [ ] Independent security review

## FAQ

**Why doesn't my phone show a "sign in to Wi-Fi" popup?**
On purpose. Keyra answers your phone's connectivity checks as "online" so the phone joins like any normal network and does not open a cramped captive-portal sheet (which often cannot run the app properly or keep cookies). Just open **http://keyra.local** in your regular browser. If that name does not resolve, use `http://192.168.4.1`.

**Does it work with keyboard layouts other than US?**
Today, Keyra types as a **US-layout** keyboard. If your computer is set to another layout, characters will come out differently. Switch the computer to US for the login field, or keep to characters that are the same on both layouts. Characters outside printable ASCII are refused with a clear message instead of typing the wrong thing. More layouts are on the roadmap.

**I forgot my master passphrase. Can I recover my passwords?**
No. That is the point of encryption, and there is no backdoor. You can **erase the device and start over**: on the unlock screen choose **Forgot passphrase?**, then press Keyra's button to confirm. All accounts are deleted. Restore from a backup if you have one.

**Is it safe to type a password with a button press? What if the wrong window is focused?**
The action is armed for 60 seconds and runs once on the press. Click the right field first. A long press cancels.

**Why HTTP and not HTTPS?**
A device on its own network cannot get a certificate that browsers trust without installing something on each phone, and a browser warning teaches people to ignore warnings. The link is protected by WPA2 and a private Wi-Fi password instead. This is a real limit: see [SECURITY.md](SECURITY.md).

**Do I need an internet connection?**
No. Keyra creates its own Wi-Fi network, and your phone does not need internet while connected to it (it may keep using mobile data for other apps).

**Can the computer read my vault?**
Keyra presents only a keyboard to the computer. It has no storage interface and no network path to it. The computer sees what gets typed, and nothing else.

**What does it cost?**
A compatible ESP32-S3 board is usually a few dollars to about ten.

## Contributing

Bug reports, hardware test reports, translations and pull requests are welcome. Start with [CONTRIBUTING.md](CONTRIBUTING.md) and the [Code of Conduct](CODE_OF_CONDUCT.md). For security issues, use a private advisory ([SECURITY.md](SECURITY.md)).

## License

[MIT](LICENSE) © 2026 hasanalaaa. Third-party components keep their own licenses (ESP-IDF, TinyUSB, LittleFS, cJSON, Preact).
