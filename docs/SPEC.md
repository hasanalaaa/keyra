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
| GET `/api/state` | none | `{device:{name,version,model,mac}, initialized, unlocked, session:bool, autoLockMin, host:{usb:bool, capsLock:bool}, pending:Pending\|null, last:Result\|null, presence:{awaiting:bool, op:string\|null, expiresIn:ms, result:{op, ok:bool, code:"done"\|"failed"\|"expired"\|"cancelled", at:ms_ago}\|null}, timeValid:bool}` — polled ~1 s while something is pending, else ~5 s |
| POST `/api/setup` | none, only if !initialized | `{passphrase, wifiPassword, deviceName?}` → 202 `{awaiting:"button", expiresIn}`; completes when the button is pressed (watch `state.presence` / `state.initialized`). passphrase 10–128 chars; wifiPassword 8–63 printable ASCII and ≠ `keyra1234`. The client then calls unlock. AP restarts with the new password ~3 s after commit. |
| POST `/api/unlock` | none | `{passphrase}` → 200 `{csrf}` / 401 `{error:"wrong", retryAfterMs}` / 429 `{error:"rate_limited", retryAfterMs}` |
| POST `/api/lock` | session | → 204 |
| GET `/api/entries` | session | → `{entries:[{id,title,url,username,favorite,hasPassword,hasTotp,updated,lastUsed}]}` (no secrets) |
| GET `/api/entries/{id}` | session | → full entry incl. `password`, `totp`, `notes` |
| POST `/api/entries` | session | entry (no id) → 201 `{id}` |
| PUT `/api/entries/{id}` | session | partial entry → 200 `{id}` |
| DELETE `/api/entries/{id}` | session | → 204 |
| POST `/api/entries/import` | session | `{entries:[…≤50]}` → `{added, skipped}` (duplicate = same title+username+url) |
| GET `/api/entries/{id}/totp` | session | → `{code, period, remaining}` / 409 `no_time` / 404 |
| POST `/api/type` | session | `{id, what:"username"\|"password"\|"both"\|"totp", submit?:bool}` or `{test:true}` → 202 `{pending}`; replaces any existing pending action |
| POST `/api/type/cancel` | session | → 204 |
| GET `/api/settings` | session | → `{deviceName, wifiSsid, autoLockMin, keyDelayMs, bothSeparator:"tab"\|"enter", submitAfterBoth:bool, ledBrightness}` |
| PUT `/api/settings` | session | partial of the above (+ optional `wifiPassword`) → 200 settings. Changing `wifiSsid` or `wifiPassword` requires presence → 202 `{awaiting:"button"}` |
| POST `/api/passphrase` | session | `{current, next}` → 204 / 401 wrong |
| POST `/api/backup` | session | `{passphrase}` (≥12 chars) → 200 `application/json` attachment `keyra-backup-YYYYMMDD.json` |
| POST `/api/restore` | session | `{passphrase, backup:<object>, mode:"merge"\|"replace"}` → `{added, updated}`; `replace` requires presence (202) |
| POST `/api/factory-reset` | none (must work when passphrase is forgotten) | → 202 `{awaiting:"button"}`; on button press: wipe vault + settings, restart into setup |

`Pending` = `{kind:"type", id, title, what, submit, expiresIn}` (expires after
**60 s**). `Result` = `{ok:bool, code:"typed"|"cancelled"|"expired"|"no_usb"|"unsupported_char"|"failed", at:ms_ago, title?, what?}`.

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
`GET /api/keyboard` → `{layouts:[{id,name,platform,experimental,probe}], usb, ble}`.
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
  `"keyra/f/v1/<id>"`, ≤ 50, ≤ 1 KiB each) and `fido.bin` (wrapping-key salt);
  `vault::passkey*` in `keyra/vault.hpp`. Not in backups.
- **API** (session):
  - `GET /api/fido` → `{passkeys:[{id, rpId, userName, displayName, created}], max:50}`
    (newest first; `created` unix seconds, 0 = unknown)
  - `DELETE /api/fido/{id}` → 204 / 404 `not_found`

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
needs a press; turning it on applies at once. Typing never needs it: the
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
