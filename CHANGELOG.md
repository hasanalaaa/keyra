# Changelog

All notable changes to Keyra are documented here. The format follows
[Keep a Changelog](https://keepachangelog.com/en/1.1.0/) and the project uses
[Semantic Versioning](https://semver.org/).

## [Unreleased]

### Added

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
- The embedded web app grows from about 78 KB to about 129 KB gzipped (the QR
  decoder is inlined in the single file).

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
