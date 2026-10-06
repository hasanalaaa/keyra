# Changelog

All notable changes to Keyra are documented here. The format follows
[Keep a Changelog](https://keepachangelog.com/en/1.1.0/) and the project uses
[Semantic Versioning](https://semver.org/).

## [Unreleased]

### Added

- Bluetooth LE keyboard (HID over GATT on NimBLE) with the same typing engine,
  Caps Lock handling and key-release guarantees as USB. Pairing needs a button
  press and stays open for 2 minutes (LED pulses cyan); LE Secure Connections
  only, Just Works, up to 4 bonded devices. Outside the window Keyra advertises
  only to bonded devices.
- Settings `bleEnabled` and `output` (`auto`, `usb`, `ble`); `auto` types over USB
  when plugged in, otherwise into the connected Bluetooth device.
- API: `GET /api/ble`, `POST /api/ble/pair` (presence op `ble_pair`),
  `DELETE /api/ble/bonds/{addr}`; `state.host.ble` and `state.host.output`;
  result code `no_host`.
- Web app: Settings → Bluetooth (on/off, Type into, paired devices, Pair a new
  device), and the Ready screen says when it will type over Bluetooth and to which device.

### Changed

- Factory reset also forgets every paired Bluetooth device.
- Locking the vault closes an open Bluetooth pairing window.

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
