# Keyra — Product & Engineering Spec (v1)

Keyra is a pocket hardware password vault built on an ESP32-S3 board. It stores
accounts encrypted on the device, is managed from any phone/laptop browser over
the device's own Wi-Fi, and **types** credentials into the focused field of the
computer it is plugged into (USB HID keyboard) after a physical press of its
button.

This document is the contract between the firmware, the web app and the docs.
Change it deliberately; every agent builds against it.

---

## 1. The experience (what "done" feels like)

1. Plug Keyra into a computer (USB). It appears as a keyboard.
2. On the phone, join Wi-Fi **Keyra-XXXX** (first time: password `keyra1234`).
   The phone joins like any normal network. **No captive-portal sheet pops up.**
3. Open **http://keyra.local** (or `http://192.168.4.1`). First visit offers
   "Add to Home Screen" so next time it is one tap, full-screen, like an app.
4. First run: a 3-step onboarding — choose a master passphrase → choose a new
   Wi-Fi password → **press the button on Keyra** to prove you hold it. Done.
5. Daily use:
   - Unlock with the master passphrase (stays unlocked until idle auto-lock).
   - Search/scroll the account list (favorites and recently used on top).
   - Tap an account → three big actions: **Username**, **Password**, **Both**
     (and **Code** if it has 2FA).
   - The screen says "Ready — click the login field on your computer and press
     Keyra's button". Keyra's LED pulses blue.
   - Press the button → Keyra types it. LED flashes green; the phone shows
     "Typed ✓".
   - On the phone itself, every field also has a **Copy** button.
6. Long-press the button (≥1.5 s) at any time: cancels a ready action; when
   nothing is pending it locks the vault.

Design bar: as effortless as Apple Passwords; visually a top-tier 2026 product.
Arabic (RTL) and English, auto-detected, switchable.

## 2. Hardware

- Target: ESP32-S3 DevKitC-1 class boards with **≥ 8 MB flash**. PSRAM optional
  (use it when present; never require it). Reference board: N16R8.
- Button: **GPIO0 (BOOT)**, active-low, internal pull-up. Never hold a restart
  while GPIO0 is low (it would latch download mode): any software restart waits
  for GPIO0 high first.
- LED: onboard WS2812 RGB. GPIO configurable via Kconfig `KEYRA_LED_GPIO`
  (default **38**, DevKitC-1 v1.1; v1.0 uses 48).
- USB: native USB-OTG port (GPIO19/20) runs TinyUSB.

## 3. Repository layout

```
Keyra/
  firmware/                 ESP-IDF 6.0.x project (C++17)
    CMakeLists.txt  sdkconfig.defaults  sdkconfig.release  partitions.csv
    main/                   app_main + wiring only
    components/
      keyra_vault/          crypto, encrypted storage, TOTP, backup (+ host tests)
      keyra_hid/            TinyUSB HID keyboard + dev CDC, typing engine (+ host tests for keymap)
      keyra_io/             button (GPIO0) + RGB LED status
      keyra_net/            SoftAP, DNS, mDNS, captive-probe answers
      keyra_api/            HTTP server, REST API, sessions, pending-action state machine, static web assets
      keyra_fido/           USB FIDO2/U2F security key (CTAPHID, CTAP2, passkeys; §10)
    test/host/              host test runner (CMake + ctest, plain C++)
  web/                      Vite + Preact + TypeScript single-page app
    mock/                   Node mock of the device API (for UI dev, screenshots, e2e)
  tools/                    devctl.py (flash/reset/log over native USB), helper scripts
  docs/                     SPEC.md, DESIGN.md, SECURITY.md, HARDWARE.md, images/
  README.md  LICENSE  .github/
```

Build: `web` builds to a single gzipped `index.html` (+ icons/manifest) that the
firmware embeds (`EMBED_FILES`) from `firmware/components/keyra_api/www/`.
The committed `www/` contains the latest built assets so the firmware builds
without Node.

## 4. Firmware architecture

Single ESP-IDF app, FreeRTOS tasks:
- `httpd` (esp_http_server task) — API + static files.
- `keyra_io` task — debounced button events (queue) + LED animations.
- `dns` task — tiny UDP DNS responder.
- Typing runs in a dedicated `type` task so HTTP stays responsive.

State ownership: the **pending action** lives in `keyra_api/actions` (one at a
time, mutex-protected). Button events are consumed only there.

### 4.1 Component interfaces (C++, namespace `keyra`)

```cpp
// keyra_vault/include/keyra/vault.hpp
namespace keyra::vault {
struct Entry {
  uint32_t id;                 // random non-zero, stable
  std::string title, url, username, password, totp /*otpauth URI or base32*/, notes;
  bool favorite;
  int64_t created, updated, lastUsed;   // unix seconds (0 = unknown)
};
enum class Status { Ok, NotInitialized, AlreadyInitialized, Locked, WrongPassphrase,
                    RateLimited, NotFound, Invalid, Full, StorageError, Corrupt };
Status init();                                   // mount storage, load meta
bool   initialized();
bool   unlocked();
Status setup(const std::string& passphrase);     // creates meta + empty vault, leaves it unlocked
Status unlock(const std::string& passphrase, uint32_t* retryAfterMs);
void   lock();                                   // wipes keys + decrypted entries
Status list(std::vector<Entry>& out);            // passwords/totp included; callers strip
Status get(uint32_t id, Entry& out);
Status put(Entry& e);                            // id==0 → create (assigns id); else update
Status remove(uint32_t id);
Status touch(uint32_t id, int64_t now);          // lastUsed
Status changePassphrase(const std::string& cur, const std::string& next);
Status exportBackup(const std::string& backupPass, std::string& outJson);
Status importBackup(const std::string& backupPass, const std::string& json, bool replace,
                    size_t* added, size_t* updated);
Status factoryReset();                           // erases everything vault-related
}
namespace keyra::totp {
bool code(const std::string& secretOrUri, int64_t unixTime, char out[11], int* period, int* remaining);
}
```

```cpp
// keyra_hid/include/keyra/hid.hpp
namespace keyra::hid {
enum class Result { Ok, NotMounted, Busy, Unsupported /*char not on US layout*/, Failed };
struct Options { uint16_t keyDelayMs = 12; };
void   init(bool devCdc);        // devCdc: composite HID+CDC with 1200-baud → ROM download hook + log mirror
bool   mounted();                // host enumerated & not suspended
bool   capsLock();               // from host LED report
Result typeText(const char* text, const Options&);   // handles CapsLock (toggle off/restore), always releases keys
Result tapKey(uint8_t hidKeycode, const Options&);   // e.g. KEY_TAB, KEY_ENTER
bool   typeable(const char* text);                   // printable US-ASCII only
}
```

```cpp
// keyra_io/include/keyra/io.hpp
namespace keyra::io {
enum class Button { Short, Long };
enum class Led { Off, Locked, Idle, Pending, Typing, Success, Error, AwaitPresence, Setup, Pairing, Fido };
void init();                                   // LED + button task
bool nextButton(Button& out, TickType_t wait); // event queue
void led(Led state);
bool bootPinHigh();                            // for safe restarts
}
```

```cpp
// keyra_net/include/keyra/net.hpp
namespace keyra::net {
struct Config { std::string ssid, password; uint8_t channel = 6; };
esp_err_t start(const Config&);    // single clean AP bring-up (config set before start), DNS, mDNS keyra.local
esp_err_t reconfigure(const Config&);
int stations();
}
```

## 5. HTTP API (JSON, all under `/api`)

Conventions
- Requests/responses `application/json; charset=utf-8`. Errors: `{"error":"<code>","message":"<english>"}` with a proper HTTP status.
- Session: `POST /api/unlock` sets cookie `ks=<token>; HttpOnly; SameSite=Strict; Path=/`
  and returns `{"csrf":"…"}`. Every non-GET request except `/api/unlock`,
  `/api/setup` and `/api/factory-reset` must send header `X-Keyra-CSRF: <csrf>`.
  Missing/invalid → 403 `csrf`.
- Locked vault or missing session on a protected route → 401 `locked`.
- Clock: clients send `X-Keyra-Time: <unix ms>` on every request; the device
  adopts it if it has no time or drifts > 5 s (device has no RTC).
- Max 4 concurrent sessions; `lock` (or auto-lock) ends all.
- Request bodies > 64 KiB → 413 `too_large` (import uses batches). Exception:
  `/api/restore` accepts up to 2 MiB when PSRAM is present (128 KiB without).
- Other error codes: 405 `method_not_allowed`; 409 `busy` (setup/factory reset
  while another item awaits the button); 503 `busy` (worker queue full);
  507 `full`; 403 `csrf` also when `Origin` is foreign.

| Method & path | Auth | Body → Response |
|---|---|---|
| GET `/api/state` | none | `{device:{name,version,model,mac}, initialized, unlocked, session:bool, autoLockMin, host:{usb, ble, capsLock, output, bleTarget, connecting, usbOs?}` (§8.1, §10.5; `usbOs` with a session only)`, pending:Pending\|null, last:Result\|null, presence:{awaiting:bool, op:string\|null, expiresIn:ms, result:{op, ok:bool, code:"done"\|"failed"\|"expired"\|"cancelled", at:ms_ago}\|null}, net?` (§8.2)`, timeValid:bool, graceMs` (§12.3)`, update?` (§14, session only)`}` — polled ~1 s while something is pending, else ~5 s |
| POST `/api/setup` | none, only if !initialized | `{passphrase, wifiPassword, deviceName?}` → 202 `{awaiting:"button", expiresIn}`; completes when the button is pressed (watch `state.presence` / `state.initialized`). passphrase 10–128 chars; wifiPassword 8–63 printable ASCII and ≠ `keyra1234`. The client then calls unlock. AP restarts with the new password ~3 s after commit. |
| POST `/api/unlock` | none | `{passphrase}` → 200 `{csrf, failedAttempts}` (§15) / 401 `{error:"wrong", retryAfterMs}` / 429 `{error:"rate_limited", retryAfterMs}` / 202 trust the browser first (§8.2) |
| POST `/api/lock` | session | → 204 |
| GET `/api/entries` | session | → `{entries:[{id,title,url,username,favorite,hasPassword,hasTotp,hasSequence,updated,lastUsed,burnAfter}]}` (no secrets) |
| GET `/api/entries/{id}` | session | → the entry; `password`, `totp`, old passwords and `sequence` only when `revealed` (§12.3) |
| POST `/api/entries/{id}/reveal` | session | → 200 the revealed entry, or 202 press first (§12.3) |
| POST `/api/entries` | session | entry (no id) → 201 `{id}` |
| PUT `/api/entries/{id}` | session | partial entry → 200 `{id}` |
| DELETE `/api/entries/{id}` | session | → 202 `{awaiting:"button", op:"delete_entry", expiresIn, cancel}` (404 if unknown, 409 `busy` §12.5a); the press removes it (only if still unlocked and still there, else `failed`) |
| POST `/api/entries/import` | session | `{entries:[…≤50]}` → `{added, skipped}` (duplicate = same title+username+url) |
| GET `/api/entries/{id}/totp` | session | → `{code, period, remaining}` / 409 `no_time` / 404 |
| POST `/api/type` | session | `{id, what:"username"\|"password"\|"both"\|"totp"\|"sequence", submit?, target?, switchLang?}`, `{text, repeat?, separator?}` (§9.2), `{test:true}` or `{probe:true}` (§10.3) → 202 `{pending}`; replaces this session's own pending item, 409 `busy` while another session's waits (§12.5a) |
| POST `/api/type/cancel` | session | → 204 |
| GET `/api/settings` | session | → `{deviceName, wifiSsid, autoLockMin, keyDelayMs, bothSeparator:"tab"\|"enter", submitAfterBoth:bool, ledBrightness, bleEnabled, output, bleConnect, protectReveal, passkeysInBackup, lockOnUsb, lockOnBle, lastBackupAt, homeWifi:{enabled, ssid}, apMode, osUsb, layoutUsb, layoutBle, bothSequence}` |
| PUT `/api/settings` | session | partial of the above (+ optional `wifiPassword`; `homeWifi` goes through `/api/wifi/home`, `lastBackupAt` is set by backups) → 200 settings. Changing `wifiSsid`/`wifiPassword`, turning `protectReveal` off or `passkeysInBackup` on (op `passkeys_backup_on`), requires presence → 202 `{awaiting:"button"}` |
| POST `/api/passphrase` | session | `{current, next}` → 204 / 401 `{error:"wrong", retryAfterMs}` / 429 `rate_limited` |
| POST `/api/backup` | session | `{passphrase}` (≥12 chars) → 200 `application/json` attachment `keyra-backup-YYYYMMDD.json` (format v3; carries the passkeys when the `passkeysInBackup` setting is on, §11) |
| POST `/api/restore` | session | `{passphrase, backup:<object>, mode:"merge"\|"replace"}` → `{added, updated, passkeys}` (`passkeys` = passkey records added); `replace` requires presence (202); 409 `passkeys_full` when the passkeys would not fit (§11) |
| POST `/api/factory-reset` | none (must work when passphrase is forgotten) | → 202 `{awaiting:"button"}`; on button press: wipe vault + settings, restart into setup |

`Pending` = `{kind:"type", id, title, what, submit, expiresIn, target, preview?, part?, parts?}` (expires after
**60 s**; `preview`/`part`/`parts` for sequences, §10.4). `Result` = `{ok:bool, code:"typed"|"cancelled"|"expired"|"no_usb"|"no_host"|"host_changed"|"unsupported_char"|"failed", at:ms_ago, title?, what?}`.

Routes added after v1.0, described in their sections: `/api/ble`, `/api/ble/pair`,
`/api/ble/bonds/{addr}` (§8.1); `/api/wifi/scan`, `/api/wifi/home`,
`/api/trusted[/{id}]` (§8.2); `/api/generate` (§9.1); `/api/keyboard` (§10.1);
`/api/fido[/{id}]` (§11); `/api/recovery`, `/api/unlock/recovery`,
`/api/presence/cancel` (§12); `/api/health`, `/api/health/rotate` (§13);
`/api/update`, `/api/update/check|download|apply` (§14); `/api/activity` (§15).

Restore (`/api/restore`):
- The backup passphrase and the file are checked before anything changes, and
  for `replace` before the press is requested (401 `wrong` / 400 invalid / 507 `full`
  / 409 `passkeys_full`).
- `replace` is atomic: the new entries are staged next to the old ones and
  committed by one marker file. A power cut or a write failure (storage full)
  leaves either every old entry or every restored one, never a mix; an
  interrupted commit is finished at the next boot or unlock.
- `merge` matches an entry by id (only when title, username and url agree too)
  or else by title, username and url. The copy with the newer `updated` wins,
  so an old backup never overwrites a newer local edit (a tie keeps the local
  one, and is not counted in `updated`). When the backup wins and changes the
  password, the local password goes into the entry's history (§9.3). The same
  id for a different account is added as a separate entry.

Button semantics (owned by `keyra_api/actions`):
- Short press: if a presence op is awaiting → approve it; else if a type action is pending → run it (one-shot); else no-op (LED blink).
- Long press: cancel presence op / pending action; if nothing pending and unlocked → lock.

Captive / connectivity (owned by `keyra_net` + static handler): answer OS probes as **online** so phones connect normally and no sheet appears:
`/hotspot-detect.html`, `/library/test/success.html` → Apple "Success" HTML; `/generate_204`, `/gen_204` → 204; `/connecttest.txt` → `Microsoft Connect Test`; `/ncsi.txt` → `Microsoft NCSI`; `/success.txt` → `success`. Requests whose `Host` is not the device (IP or `keyra.local`) get a tiny 302 to `http://keyra.local/`. DNS answers every A query with 192.168.4.1 (AAAA → empty).

Static: `GET /` → `index.html` (gzip, `Cache-Control: no-cache`); `/manifest.webmanifest`, `/icon-*.png`, `/apple-touch-icon.png` (long cache).

## 6. Security model

- Master passphrase → PBKDF2-HMAC-SHA256 (salt 16 B, iterations calibrated at
  setup to ≈1.2 s on the device, min 60 000) → KEK. A random 256-bit DEK is
  wrapped by the KEK (AES-256-GCM). Changing the passphrase re-wraps the DEK only.
- Each entry is its own file, AES-256-GCM(DEK, random 96-bit IV, AAD =
  `"keyra/e/v1/" + id`). Writes are atomic (temp file + rename on LittleFS).
- Unlock attempts: failure counter persisted in NVS **before** the KDF runs;
  delay = 0 for the first 4 failures, then 2^(n-4) s capped at 15 min.
- RAM: decrypted entries only while unlocked; keys and plaintext buffers are
  zeroised on lock (`mbedtls_platform_zeroize`). Auto-lock after idle
  (default 15 min, setting 1–120), and immediately on factory reset.
- Typing requires a physical button press for every action. Nothing types
  without it. Secrets never appear in logs.
- Web: CSRF header, HttpOnly SameSite=Strict cookie, strict CSP
  (`default-src 'self'; script-src 'self' 'unsafe-inline'; style-src 'self' 'unsafe-inline'; img-src 'self' data:`
  — inline only because the single-file build inlines assets), `X-Frame-Options: DENY`,
  `Referrer-Policy: no-referrer`.
- Honest limits (document in SECURITY.md): HTTP on a WPA2 link (no TLS);
  no secure element; flash encryption/secure boot are optional eFuse steps
  documented but not enabled by default.

## 7. Builds

- `firmware`: `idf.py build` (dev profile: `sdkconfig.defaults`, HID+CDC with
  logs and the 1200-baud reboot-to-ROM hook) and
  `idf.py -D SDKCONFIG_DEFAULTS="sdkconfig.defaults;sdkconfig.release" build`
  (HID only, logs off).
- Partition table (8 MB baseline, works on 16 MB):
  `nvs 0x9000 24K · otadata 8K · phy_init 4K · app0 3M · app1 3M · vault (littlefs) rest (≥1.5 MB)`.
- `web`: `npm run build` → `firmware/components/keyra_api/www/`.
- `firmware/test/host`: `cmake -S . -B build && cmake --build build && ctest` — vault core, TOTP, keymap, action state machine.

---

## 8. v1.1 — Bluetooth keyboard and home Wi-Fi

### 8.1 Bluetooth LE keyboard (HID over GATT)

Goal: Keyra also types into phones, tablets and computers over Bluetooth, with
the same "prepare → press the button" flow. USB stays the default.

- New component `keyra_ble` (NimBLE, peripheral only). Advertises as the device
  name with keyboard appearance (0x03C1). HID over GATT keyboard report + LED
  output report (Caps Lock handled like USB). Battery service reports 100 %.
- **Bonding is gated by the button.** Pairing mode is a presence op
  (`ble_pair`): `POST /api/ble/pair` → 202 `{awaiting:"button"}`; after the
  press the device is discoverable/pairable for 120 s (LED cyan pulse). Outside
  that window it advertises only to bonded hosts (filter accept list) and
  rejects new pairings. LE Secure Connections, bonding, "Just Works" (no
  display/keypad); max 4 bonds stored in NVS.
- **Connect on demand (default).** Setting `bleConnect` `"on_demand"|"always"`.
  An iPhone/iPad hides its on-screen keyboard while any Bluetooth keyboard is
  connected, so by default Keyra neither advertises nor holds a link while idle:
  - When a type action is armed for a Bluetooth host, Keyra advertises with the
    filter accept list holding only that host until it connects (`state.host.connecting`).
  - A short press types only once the host is connected; before that it is
    ignored and the action stays armed. If it never connects within the 60 s
    expiry, the result is `no_host`.
  - After typing (any result) Keyra keeps the link ~20 s so a quick second
    action reuses it, then disconnects. Cancel, expiry, replacement and lock
    end it at once. A host that has just paired is also kept ~20 s.
  - `always`: bonded hosts may reconnect whenever they are around (accept
    list = all bonds); an action armed for one host still drops another.
  The pairing window is unaffected.
- **Pairing while a host is connected.** One host types at a time, but the
  pairing window does not wait for the linked host to leave: Keyra keeps
  advertising in a second link slot. The host that pairs there takes over —
  the linked host is let go (no auto-lock: Keyra ended it) — unless an armed
  action waits for the linked host, in which case the new bond is stored and
  the new host is let go. A bonded host that merely reconnects through the
  open window while another is linked is let go as well. The second slot
  never outlives the window.
- **Target.** Each type action picks its host when armed: `POST /api/type`
  takes optional `target: "usb" | "<bond addr>"`. Without it, from setting
  `output` `"auto"|"usb"|"ble"`: `usb` = USB (even unplugged → `no_usb`);
  `ble` = the most recently used bond (the connected one first); `auto` = USB
  when mounted, else as `ble`. Every part of the action (both, submit) goes to
  that host. The typing engine, keymap, Caps Lock wrap and "always release"
  guarantees are shared by both transports.
- Settings: `bleEnabled` (default true), `output` (default `"auto"`),
  `bleConnect` (default `"on_demand"`).
- API (session):
  - `GET /api/ble` → `{enabled, pairing:{active:bool, expiresIn:ms}, connected:{addr, name}|null, bonds:[{addr, name, lastSeen}]}`
  - `POST /api/ble/pair` → 202 (presence op `ble_pair`); refused up front with
    409 `ble_disabled` (Bluetooth off), 409 `bonds_full` (4 bonds) or 503
    `ble_unavailable` (stack failed to start). The window closes early once one
    host has paired, when the vault locks, or when Bluetooth is turned off.
  - `DELETE /api/ble/bonds/{addr}` → 204 (disconnects it if connected)
  - `GET /api/state` → `host:{usb, ble, capsLock, output:"usb"|"ble"|null, bleTarget:{addr,name}|null, connecting:bool}`.
    `ble` = a Bluetooth host is connected right now; `output` = the kind of
    host a new action would use (null = none available); `bleTarget` = the
    host the armed action will type into; `connecting` = still waiting for it.
    `pending` gains `target` (`"usb"`, an address, or null).
  - `POST /api/type` with an unknown bond → 404 `not_found`; a bond while
    Bluetooth is off → 409 `ble_disabled`.
- New `Result.code` `no_host` (nothing to type into on Bluetooth or auto, or the
  Bluetooth host never connected; `no_usb` kept for a USB target).
- A BLE host counts as connected (`host.ble`) only when bonded, encrypted and
  subscribed to keyboard reports.
- Factory reset forgets all bonds. `DELETE /api/ble/bonds/{addr}` → 404 `not_found`
  for an unknown address; `addr` is `XX:XX:XX:XX:XX:XX` (identity address).

### 8.2 Home Wi-Fi (station mode)

Goal: optionally join the home network so Keyra opens at
`http://keyra.local` from any device on that network — no need to switch
Wi-Fi.

- Settings: `homeWifi:{enabled, ssid}` (password write-only, never returned),
  `apMode:"always"|"fallback"` (default `"always"`). `fallback` turns the device
  AP off while the home network is connected and brings it back if the home
  network is unavailable for 60 s (and at boot if it hasn't connected within
  30 s) — Keyra can never become unreachable.
- Joining/changing/disabling home Wi-Fi is a presence op (`home_wifi`), because
  it changes who can reach the device.
- API (session):
  - `GET /api/wifi/scan` → `{networks:[{ssid, rssi, secure, channel}]}` (sorted by rssi, hidden/empty SSIDs dropped; the scan may stall the AP for ~2 s)
  - `PUT /api/wifi/home` `{enabled, ssid?, password?}` → 202 presence op
  - `GET /api/state` → `net:{ap:{on, ssid, clients}, home:{enabled, connected, ssid, ip, rssi}|null, via:"ap"|"home"}` (`via` = how this request arrived).
- mDNS `keyra.local` + `_http._tcp` on both interfaces. The Host check accepts
  `keyra.local`, the AP IP and the current home IP. The catch-all DNS and the
  captive-probe answers apply only on the AP.
- **Trusted browsers (LAN safety).** On the device AP nothing changes. A
  browser reaching Keyra through the home network must be approved once:
  `POST /api/unlock` from `via:"home"` without a valid `kt` cookie →
  202 `{awaiting:"button", op:"trust_browser"}`; when the button is pressed the
  unlock completes on the next `POST /api/unlock` retry (same passphrase) and
  the response also sets `kt=<token>; HttpOnly; SameSite=Strict; Max-Age=31536000`.
  Up to 8 trusted browsers (name from User-Agent, created, lastSeen), stored
  hashed in NVS. API: `GET /api/trusted` → `{browsers:[{id, name, created, lastSeen, current}]}`,
  `DELETE /api/trusted/{id}` → 204.
- Time: when the home network is connected, SNTP (`pool.ntp.org`,
  `time.google.com`) sets the clock; the client `X-Keyra-Time` stays as a
  fallback. `state.timeValid` then stays true without a phone.
- AP and home network share one radio: when the home network is on another
  channel the AP follows it (phones on the AP reconnect once). Documented in
  the UI next to the setting.
- Implementation notes (v1.1): only WPA2/WPA3-personal networks are joined
  (password 8–63 printable ASCII); the scan lists open networks with
  `secure:false` and drops ones Keyra can never join (WEP, WPA1, enterprise).
  In `fallback` the AP turns off 15 s after the home link is up (so the phone
  that made the change sees the result). Joins retry with backoff 2 s → 5 min,
  reset on success; a join with no result after 20 s counts as failed. The
  `202` of a `trust_browser` unlock already sets a session-only `kt` cookie; the
  press makes it trusted and the retried unlock renews it with `Max-Age`. A ninth
  trusted browser replaces the least recently used one. `PUT /api/wifi/home`
  reuses the stored password when re-enabling the same `ssid`. Changing
  `apMode` (`PUT /api/settings`) needs no press; `homeWifi` there is refused.
- Both interfaces get an IPv6 link-local address so mDNS answers the AAAA
  question for `keyra.local` immediately (the mdns component sends no negative
  reply, and Apple/Windows resolvers otherwise wait ~5 s on it — long enough
  for phones on the home network to time out). `via` treats a native IPv6
  request as AP only when its local address is the AP's link-local address.

---

## 9. v1.2 — Generator, typing any text, and Keyra Companion

### 9.1 Password generator (hardware randomness)

Where it lives: a **Generate** button on the main vault screen (top bar, always
one tap away) opening a generator sheet; the same generator component appears
inside Add/Edit next to the password field. Rationale: when you sign up or
change a password on a website you need the new password *before* it is saved
in Keyra, so generation must not be buried inside "Add account".

- `POST /api/generate` (session) `{length:8..128, lower, upper, digits, symbols:bool, minDigits?, minSymbols?, avoidAmbiguous?:bool, symbolSet?:string}`
  → `{password, entropyBits}`. Generated **on the device** from the ESP32-S3
  hardware RNG (`esp_fill_random` with the radio on), uniform per character by
  rejection sampling, class minimums satisfied by rejecting whole candidates
  (no positional bias), never logged, never stored until the user saves it.
  At least one class must be enabled; minimums must fit in the length.
  Ambiguous set: `0 O o 1 l I | \` ' "`.
- Sheet actions on the generated password:
  - **Type it** → `POST /api/type {text}` (§9.2); variant **Type twice**
    (`{text, repeat:2, separator:"tab"}`) for "confirm password" fields.
  - **Save** → new account (prefilled password; title/URL/username fields) or
    **update an existing account** (old password moves to that entry's history).
  - **Copy**, **Regenerate**; entropy + strength label; last settings remembered
    per browser.

- Implementation notes (v1.2): every enabled class appears at least once
  (`minDigits`/`minSymbols` raise that floor; 0 and 1 mean the same). Default
  symbols `!@#$%^&*-_=+?`; `symbolSet` = 1–32 distinct ASCII punctuation
  characters. `length`, `lower`, `upper`, `digits` and `symbols` are required.
  Settings whose candidates would meet the minimums less than once in 1,000
  draws are refused (400 "minimums are too high"); `entropyBits` is exact
  (floor of log2 of the number of possible passwords). The web app keeps the
  settings in `localStorage["keyra.gen"]`.

### 9.2 Type any text (remote keyboard)

`POST /api/type {text, repeat?:1|2, separator?:"tab"|"enter"}` (session) arms a
pending action exactly like an entry action: one-shot, 60 s, button press
required, Caps Lock wrap, keys always released. `text` ≤ 256 printable
characters for the active layout (control characters rejected), wiped from RAM
after typing/cancel/expiry. Pending shows `{kind:"type", what:"text", title:null}`.
The UI offers it in the generator and as "Type text…" in the vault menu.

Implementation notes (v1.2): `text` cannot be combined with `id`, `what`,
`test` or `submit`; `target` works as for entry actions. `last.title` is also
null for free text.

### 9.3 Password history

Each entry keeps up to 10 previous passwords `{password, changedAt}` inside the
encrypted entry (no plaintext metadata on flash). `PUT /api/entries/{id}` with a
new `password` pushes the old one. `GET /api/entries/{id}` returns `history`.

Implementation notes (v1.2): only an update that changes the password adds to
the history (`changedAt` = the update time, 0 when the device has no clock);
the vault ignores any `history` a client sends. Entry plaintext format 2
carries it (format 1 is still read and rewritten on the next write). Backup
files are version 2 with `history` per entry; version 1 files still import.

### 9.4 Keyra Companion (browser extension) — DEFERRED

> Not being built. Kept here as a design note; work resumes only when the
> owner asks for it (branch `feat/companion` holds an unfinished start).


A Manifest V3 extension (`extension/`, Chrome/Edge/Firefox; Safari via
`xcrun safari-web-extension-converter`). Needs Keyra reachable from the computer
(home Wi-Fi mode, or the computer on Keyra's own Wi-Fi).

- **Pairing:** in the extension, enter `keyra.local` (or IP) → `POST /api/ext/pair {name}`
  → 202 presence op `ext_pair` → after the press the extension receives a
  bearer token (shown once, stored in `chrome.storage.local`; device keeps a
  SHA-256 hash; up to 8; listed/revoked in Settings → Companion). Requests
  carry `Authorization: Bearer <token>`; CORS allows `chrome-extension://*`,
  `moz-extension://*`, `safari-web-extension://*` origins **only** with a valid
  token. The vault must be unlocked (else 401 `locked` → the extension links
  to the web app).
- **Save prompt (Apple-style):** the content script watches login/sign-up form
  submissions (password fields, including new-password + confirm), and shows a
  small in-page card: "Save to Keyra?" — **Save** / **Not now** / **Never for
  this site**. Save → `POST /api/ext/save {url, username, password}`: creates
  an entry, or if one matches (same host + username) offers **Update
  password** (old → history). Never-list is stored in the extension only.
- **Fill by typing:** a small Keyra badge in username/password fields; click →
  list of matching entries for the page's host (`POST /api/ext/match {host}` →
  `{entries:[{id,title,username}]}`, no secrets) → pick one → Keyra arms
  username/password/both → the user presses the button → Keyra types. The
  extension never receives stored passwords.
- Privacy: only the hostname is sent for matching; full URL only on Save.

## 10. v1.3 — Keyboard layouts and input languages

A keyboard sends key *positions*; the host's active layout and input language
decide which character appears. Keyra therefore has to know enough about each
host to pick the right key presses.

### 10.1 Layout per output

Settings `layoutUsb` and `layoutBle` (default `us`) name the keyboard layout of
the computer on each output, from the table in
`firmware/components/keyra_hid/layouts/layouts.txt` (US, UK, German, French,
Spanish, Italian — Windows and Mac variants — Dvorak, Colemak, Arabic).
`GET /api/keyboard` → `{layouts:[{id,name,platform,experimental,probe,chars}], usb, ble}`;
`chars` is every character `POST /api/type {text}` accepts on an output set to that layout
(the app checks typed text against it before sending).
Text a layout cannot type is refused (`unsupported_char`), never typed wrong.

### 10.2 Layout-proof generator

`POST /api/generate {layoutSafe:true, layouts?:[id…]}` draws only characters
typed by the same single key press on every chosen layout (default: the two
outputs' layouts).

### 10.3 Layout Doctor

`POST /api/type {probe:true}` arms a probe that types Shift-level keys only (no
Enter); the user compares what appeared with `GET /api/keyboard`'s `probe`
strings to find the computer's layout.

### 10.4 Auto-type sequences

Entries may carry `sequence` (grammar and limits in
`keyra_vault/include/keyra/sequence.hpp`: field tokens, Tab/Enter/Space,
`{DELAY n}`, `{PRESS}`; no modifier or shortcut tokens). `POST /api/type
{id, what:"sequence"}` types it; each `{PRESS}` re-arms the action for its next
part. Settings `bothSequence` replaces the built-in "Both" order.

### 10.5 Host system and input language

The layout alone is not enough: a computer switched to Arabic types Arabic
letters for every key, whatever layout Keyra assumes. Keyra keeps the
**operating system** of each host and gets text past the input language:

| System | What Keyra does | User effort |
|---|---|---|
| `windows` | Printable ASCII goes out as **Alt + keypad decimal code** (Num Lock turned on and restored like Caps Lock). Windows inserts the same character in every input language. Other characters use the layout table. | none |
| `mac`, `ios` | When the user marks the host as "in another language now", Keyra presses **Ctrl+Space** (switch to the previous input source) before typing and again after, also when typing failed. | one tap, remembered per host in the browser |
| `android` | Android keeps the physical-keyboard layout per keyboard, apart from the on-screen language: set "Keyra" to English (US) once. | once |
| `linux`, `""` | Plain key presses. | — |

- Storage: settings `osUsb` (`""`, `mac`, `ios`, `windows`, `android`, `linux`)
  and `osBle` (`"AA:BB:CC:DD:EE:FF=mac;…"`, one item per bond; dropped when
  the bond is forgotten).
- API: `GET /api/settings` / `PUT /api/settings {osUsb}`;
  `GET /api/ble` bonds carry `os`; `PUT /api/ble/bonds/{addr} {os}` → 204
  (404 unknown bond); `GET /api/state` `host.usbOs` (with a session).
  `POST /api/type` accepts `switchLang: boolean` (ignored unless the target is
  `mac`/`ios`).
- The web app guesses a newly paired host's system from its name (iPhone, iPad,
  MacBook, Galaxy, DESKTOP-…) and lets the user change it in Settings.
- Ctrl+Space toggles; it cannot select English directly. That is why the
  phone asks rather than guesses: no keyboard can read the host's language.

---

## 11. v1.4 — Passkeys and security key (USB FIDO2/U2F)

Design, key formats and honest limits: [FIDO.md](FIDO.md). Contract points:

- **USB.** Both builds are composite HID devices: interface 0 = boot keyboard
  (EP 0x81), interface 1 = FIDO (usage page `0xF1D0`, usage 1, 64-byte input
  and output reports, EP OUT 0x04 / IN 0x84, 5 ms). The dev build adds the CDC
  console as interfaces 2-3. `bcdDevice` 0x0110 (release) / 0x0111 (dev).
- **Component** `keyra_fido`: CTAPHID, CTAP 2.0 (`U2F_V2`, `FIDO_2_0`; ES256;
  rk/up/uv), U2F, the `fido` task. Plain-C++ core under `src/core`, host-tested;
  `keyra_hid` only carries the reports (`hid::setFidoReceiver`, `hid::fidoSend`).
- **Button.** While a FIDO request waits (`fido::awaitingTouch()`), the actions
  task routes presses there before the pending-action machine: short = approve,
  long = refuse. The LED shows `Led::Fido`. Requests need the vault unlocked
  (they wait ≤ 30 s for it) and a press within 30 s.
- **Vault.** Passkey records `f/<id>.bin` (AES-256-GCM with the DEK, AAD
  `"keyra/f/v1/<id>"`, ≤ 50, ≤ 1 KiB each) and `fido.bin` (the wrapping keys:
  v1 a salt, v2 an encrypted list of ≤ 4 keys; `keyra_vault/src/core/vault_core.hpp`);
  `vault::passkey*` in `keyra/vault.hpp`.
- **Backups** ([PASSKEY-BACKUP.md](research/PASSKEY-BACKUP.md)). Backup format v3
  (`backup_format.hpp`) carries the wrap keys, the passkey records and the
  signature counter while the setting `passkeysInBackup` is on (default on;
  turning it on again needs a press, op `passkeys_backup_on`). Restoring is not
  affected by the setting. `merge` adds the keys and records not already present;
  `replace` makes them the backup's in the same staged commit as the entries,
  and leaves local passkeys alone when the backup has none. Both refuse with
  409 `passkeys_full` before any change (and before the press) when the result
  would pass 4 keys or 50 passkeys. The restore raises the signature counter to
  at least the backup's + 1000. Credentials keep BE = BS = 0.
- **API** (session):
  - `GET /api/fido` → `{passkeys:[{id, rpId, userName, displayName, created}], max:50}`
    (newest first; `created` unix seconds, 0 = unknown)
  - `DELETE /api/fido/{id}` → 202 `{awaiting:"button", op:"delete_passkey", expiresIn, cancel}`
    / 404 `not_found`; the press removes it (only if still unlocked and still there) (§12.3)

## 12. v1.5 — Stronger protection and recovery

### 12.1 Vault meta v2

`meta.bin` holds a list of wraps of the data key (DEK): the passphrase wrap
and, when made, the recovery-key wrap (`keyra_vault/src/core/vault_core.hpp`).
Version 1 files migrate on the next unlock.

### 12.2 Recovery key and kit

- A 20-byte key from the hardware RNG wraps a second copy of the DEK.
  `GET /api/recovery` → `{enabled, created}`; `POST /api/recovery` → the key
  (shown once, then wiped on the device); `DELETE /api/recovery` → 204. Both
  changes need a press (§12.3).
- Forgotten passphrase: `POST /api/unlock/recovery {key (40 hex), next}` (no
  session, throttled like a wrong passphrase) sets a new passphrase and
  unlocks. The recovery key stays valid.
- The web app prints a recovery kit (the key in Crockford base32 with a typo
  check, and a QR code) and can split the key into Shamir shares
  (`shamir-secret-sharing`, pinned; splitting and combining happen in the
  browser only).

### 12.3 Secrets only after a press ("blind phone")

With `protectReveal` on (default), `GET /api/entries/{id}` returns no password,
2FA secret or old passwords (`revealed:false`, `hasPassword`, `hasTotp`,
history dates). `POST /api/entries/{id}/reveal` → 202 press → the session that
asked may read secrets for 60 s (`state.graceMs`). Downloading a backup and
changing the recovery key need the same press. Turning `protectReveal` off
needs a press; turning it on applies at once. Deleting an account
(`delete_entry`) or a passkey (`delete_passkey`) always needs a press,
whatever `protectReveal` says; the press runs the deletion itself. Typing never needs it: the
button press that types is already presence.

### 12.4 Auto-lock when the computer goes away

`lockOnUsb` (default on): the vault locks about 1 s after the USB host it was
used with is unplugged or suspended (never on charger-only power).
`lockOnBle` (default off): it locks when the Bluetooth host Keyra typed into
drops the link by itself. A USB action is bound to the USB connection it was
armed on; if that computer goes away before the press, the action fails.

### 12.5 Backup reminder

`settings.lastBackupAt` (unix seconds, 0 = never) is set on every backup
download; the vault screen nudges when it is older than 30 days.

### 12.5a Whose press it is

Everything waiting for the button belongs to the session that armed it (type
actions and presence ops alike). Another session's request gets 409 `busy`
instead of replacing it, so a second browser or a stolen session cannot swap
what the user is about to approve; the same session may replace its own item
(which then ends as `cancelled`). Ops armed without a session (setup, factory
reset, trusting a browser) belong to nobody: they never replace anything and
nothing replaces them. The cancel token comes back with the arming answer.

A press grants what it was asked for, to that session only: `reveal` opens
60 s of reading secrets; `backup` allows one backup download and `recovery`
one recovery-key change — each used up by that request, never by a reveal
grace.

Deliberate exception: `GET /api/entries/{id}/totp` (the current 2FA code)
needs a session but no press. A code lives 30 s and is useless without the
password, which does need one; asking for a press every 30 s would make the
account screen unusable.

### 12.6 Withdrawing a press request

Every 202 `{awaiting:"button"}` answer carries `cancel`, a 128-bit random
token made when the op was armed. `POST /api/presence/cancel {op, cancel}` →
204 drops the waiting op only when both match (constant-time); otherwise 409
`not_cancelled`, and nothing changes. Its result becomes `cancelled` and a
later press runs nothing. No session or CSRF token is needed (setup and
factory reset have none) — the token is the authorisation: without it a
stranger on Keyra's Wi‑Fi could cancel a user's setup and arm their own in
its place, to be confirmed by the user's press. The app keeps the token in
sessionStorage, so the same tab can still withdraw its op after a reload.

### 12.7 Why joining the home network failed

`state.net.home.error`: `""` (none yet), `"wrong_password"` (4-way handshake
or auth failure), `"not_found"`, or `"failed"`. Set when an attempt fails,
cleared when the network connects or is reconfigured.

## 13. Password health

Settings → **Password health** lists accounts whose password is reused,
could be stronger, or has not changed for over a year. Keyra works it out
itself while unlocked; the phone receives entry ids and flags, never a
password (`keyra_api/src/health.cpp`, host-tested).

- `GET /api/health` (session) →
  `{checked, clock, weak:[{id, level}], reused:[[id…]], old:[{id, since}]}`.
  `checked` = entries with a password (notes-only entries are skipped).
- **Weak**: strength level 1 or 2 by the same estimate as the app's meter
  (`web/src/lib/strength.ts`: character pool × length, −8 bits per run of 3
  equal characters or 3-step sequence, a short common-password list forces
  level 1). Both copies are pinned to the same examples in their tests.
- **Reused**: groups of ≥ 2 entries with byte-identical passwords (case
  matters). Groups say which accounts share a password, not what it is.
- **Old**: the current password was set more than 365 days ago — the
  `changedAt` of the newest history item, else the entry's `created`. Needs a
  valid clock (`clock: false` → nothing is old); unknown dates are never old.

### 13.1 Change every password

For when a backup, the phone or the passphrase may have leaked. `POST
/api/health/rotate {on}` (session) starts it (`settings.rotateSince` = now;
409 `no_time` without a clock) or ends it (0), and answers like `GET
/api/health`, which then carries `rotate: {since, pending:[id…]}`: entries
with a password whose current one was set before `since` (unknown dates
count as not changed). Nothing per entry is stored — changing a password
moves the old one into its history with the change time, and that is what
takes it off the list. Starting and ending are logged (`rotate_started`,
`rotate_ended`). The app asks before ending while accounts are left.

## 14. Firmware updates over the network

Settings → **Firmware update**. Keyra installs only images signed with the key
its own firmware was signed with, never an older version, and only after a
press of its button; a new image that fails its first boot is rolled back.

- **Signing.** Secure Boot V2 RSA-3072 signature blocks, checked by the running
  firmware (`CONFIG_SECURE_SIGNED_APPS_NO_SECURE_BOOT`,
  `CONFIG_SECURE_SIGNED_ON_UPDATE_NO_SECURE_BOOT`). No eFuses are burned: this
  stops a network attacker (or a stolen session) from installing other
  firmware, not someone holding the board with a USB cable. The trusted key is
  the one in the running image's signature block, so the first signed image
  goes on by USB; unsigned builds (CI, `idf.py build`) cannot update over the
  network at all. Builds are unsigned; `tools/sign_release.sh` signs with the
  owner's key (`~/.keyra/keyra-signing-key.pem` or `$KEYRA_SIGNING_KEY`), which
  never enters the repository. Losing the key means updates by USB only.
- **Versions.** `PROJECT_VER` (`MAJOR.MINOR.PATCH`); an image older than the
  running one is refused (`downgrade`), the same version may be reinstalled.
  The image must be a Keyra app (`project_name` "keyra").
- **Sources.** (a) `POST /api/update` with the image as the body (≤ one app
  partition, 3 MiB), streamed into the idle OTA partition. (b) `POST
  /api/update/check` → `{current, latest, newer, size, notes}` from the latest
  GitHub release of `CONFIG_KEYRA_UPDATE_REPO` (default `hasanalaaa/keyra`,
  asset `keyra-firmware.bin`; needs home Wi‑Fi, else 409 `offline`); `POST
  /api/update/download` → 202 and Keyra fetches that asset itself over HTTPS
  (certificate bundle, redirects followed only to `https://`). The client
  never names a URL. GitHub's hourly limit on unauthenticated calls (HTTP 403
  or 429) is reported as `rate_limited`; a cut or oversized answer as
  `network`.
- **Staging.** Either way the image is checked (`esp_ota_end`: image and
  signature; then name and version) and becomes *staged*: written to the idle
  partition but not bootable. `state.update` (sessions only) =
  `{phase: receiving|staged|restarting|failed, source: upload|github, done,
  total, version, error}`. Errors: `bad_signature`, `bad_image`, `downgrade`,
  `too_large`, `offline`, `network`, `no_release`, `rate_limited`, `busy`,
  `flash_failed`.
- **Install.** `POST /api/update/apply` → 202 (with the staged `version`),
  presence op `update`; the press installs only that exact image (a staging
  generation is captured at apply; if another image was staged since, the
  press does nothing), sets the boot partition, and Keyra restarts ~2 s later,
  or once the button is released (the vault is locked by the restart). From
  the press until the restart the phase is `restarting`: uploads and
  downloads answer `busy` and apply `not_staged`, since the idle partition is
  now the boot one.
- **Probation.** The new image runs on probation
  (`CONFIG_BOOTLOADER_APP_ROLLBACK_ENABLE`). After the API is up, a one-shot
  timer decides ~15 s later: if the vault mounted (`Ok`/`NotInitialized`) the
  image is kept; a vault it cannot read (`Corrupt` or any other vault error)
  sends it back to the previous image. A storage (flash/mount) error counts as
  healthy, since going back cannot fix the hardware. A crash or reset before
  the timer fires reboots still unconfirmed and the bootloader rolls back. If
  there is no previous image to roll back to, the image is kept rather than
  reboot-looping.
- **Releases.** `tools/release.sh` builds the release profile, signs it and
  publishes `v<version>` with `keyra-firmware.bin` plus the files for a first
  USB install; the notes are the version's CHANGELOG section.

## 15. Activity log

Settings → **Activity** lists what happened on this Keyra, newest first:
unlocks (and how many wrong passphrases or recovery keys were tried before
each), locks and why, typing (account title, USB or Bluetooth), passwords
shown after a press, backups and restores, passphrase and recovery-key
changes, Bluetooth pairing opened or a device forgotten, trusted browsers
removed, accounts deleted. Never a password, a code or typed text.

- Stored in the vault as `activity.bin`, AES-256-GCM with the DEK (AAD
  `keyra/activity/v1`), so it can only be read — and only grows — while
  unlocked. Events that happen while locked are not written; wrong guesses are
  counted by the vault's persisted unlock-failure counter and logged with the
  next successful unlock. Not part of backups; a new setup or factory reset
  starts it empty.
- The last 200 events (oldest dropped; the record is capped at 16 KiB).
  Encoding: `keyra_api/src/activity.cpp` (host-tested).
- `GET /api/activity` (session) →
  `{events:[{kind, at, id?, n?, detail, title?}], max}`; `at` unix seconds
  (0 = clock unknown). Kinds: `unlock` (detail 0 passphrase, 1 recovery key),
  `failed_unlocks` (n), `lock` (detail 0 manual, 1 idle, 2 USB gone, 3
  Bluetooth host gone, 4 Keyra's button), `typed` / `text_typed` (detail 0
  USB, 1 Bluetooth), `revealed`, `backup`, `restore` (n; detail 1 = replace),
  `passphrase`, `recovery_created`, `recovery_removed`, `ble_pairing`,
  `ble_forgot`, `trusted_removed`, `entry_deleted`; `unknown` for a kind this
  firmware does not name.
- There is no endpoint to clear it: a borrowed or stolen session cannot hide
  what it did.
- `POST /api/unlock` and `/api/unlock/recovery` answer
  `{csrf, failedAttempts}`; the app warns once when it is above 0.

## 16. Delete after typing

An entry can delete itself after its password has been typed a set number of
times (one-time recovery codes, temporary passwords). `burnAfter` (0–99, 0 =
keep) is part of the entry: entry format 4 adds it as one byte after the
sequence (formats 1–3 still read, as 0), backups carry it when non-zero, and
`GET/POST/PUT /api/entries…` read and write it.

- Counted when Keyra types the password: `password`, `both`, or the last part
  of a sequence. Username, 2FA code, test and free text do not count.
- On the use that brings it to zero the entry file is removed and the
  activity log records `entry_burned` (id, title). Nothing is asked first —
  that was decided when the count was set; the account screen says how many
  uses are left.
- Typing still needs the press; a cancelled or failed action uses nothing.

