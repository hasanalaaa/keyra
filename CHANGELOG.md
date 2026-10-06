# Changelog

All notable changes to Keyra are documented here. The format follows
[Keep a Changelog](https://keepachangelog.com/en/1.1.0/) and the project uses
[Semantic Versioning](https://semver.org/).

## [Unreleased]

### Added

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

### Changed

- The captive DNS answers only clients on Keyra's own Wi-Fi, and connectivity
  probes are answered only there.

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
