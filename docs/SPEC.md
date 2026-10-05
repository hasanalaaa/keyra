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
enum class Led { Off, Locked, Idle, Pending, Typing, Success, Error, AwaitPresence, Setup };
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
- Request bodies > 64 KiB → 413 (import uses batches).

| Method & path | Auth | Body → Response |
|---|---|---|
| GET `/api/state` | none | `{device:{name,version,model,mac}, initialized, unlocked, session:bool, autoLockMin, host:{usb:bool, capsLock:bool}, pending:Pending\|null, last:Result\|null, presence:{awaiting:bool, op:string\|null, expiresIn:ms}, timeValid:bool}` — polled ~1 s while something is pending, else ~5 s |
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
