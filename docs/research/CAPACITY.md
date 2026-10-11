# Capacity: thousands of accounts (measurements and design note)

Written 2026-10-11 against 0.3.1 (3ba821c). Goal: at least 5000 accounts on
the reference board (ESP32-S3 N16R8: 16 MB flash, 8 MB octal PSRAM) with a
wide margin, without getting slower or weaker. Phase 1 measures what 0.3.1
does; phase 2 is the design. Nothing here changes a stored format or the
partition table; the parts that needed such a change are a proposal for
review.

## How it was measured

`firmware/test/capacity/` is a bench, not a test. It links the real vault
core (`Vault`, entry codec, backup format) and the real littlefs 2.11 (disk
2.1) that esp_littlefs 1.22.3 ships, configured as `build/sdkconfig` and
esp_littlefs set it up: block 4096, read/prog 128, cache 512, lookahead 128,
block_cycles 512, `inline_max` left at its default (so 512), and one 8-byte
mtime attribute per file (`CONFIG_LITTLEFS_USE_MTIME`). Its `Storage` makes the
same littlefs calls esp_littlefs makes for our `LittleFsStorage`: `fopen("rb")`
is `lfs_file_opencfg`, `fstat` is a fresh `lfs_stat(path)`, `fopen("wb")` opens
and syncs at once, `unlink` is `lfs_stat` + `lfs_remove`. The flash is RAM that
counts every read call, byte read, program and erase. Entries are written
exactly as `Vault::persist` + `writeAtomic` write them (codec, AES-256-GCM,
AAD, `.tmp` + rename) but without the `kMaxEntries` check, so the bench can go
past 1000; each run then unlocks the result with the real `Vault`, which
proves the files are what the firmware reads.

```
cmake -S firmware/test/capacity -B /tmp/cap -DCMAKE_BUILD_TYPE=Release
cmake --build /tmp/cap && /tmp/cap/keyra_capacity all   # ~30 min; or capacity|unlock|ram|backup|ops
```

Counts are exact. Device **times** are a model of those counts, not
measurements (the bench never touched the device):

| | fast | slow | source |
|---|---|---|---|
| flash read | 20 MB/s + 5 µs/call | 8 MB/s + 25 µs/call | DIO at 80 MHz (`build/sdkconfig`) is 20 MB/s raw; each `esp_partition_read` is a separate SPI1 transaction with the caches suspended |
| program 128 B | 0.15 ms | 0.8 ms | NOR datasheets (W25Q128JV tPP 0.4 ms typ., 3 ms max) |
| erase 4 KiB | 30 ms | 100 ms | W25Q128JV tSE 45 ms typ. |
| AES-GCM per entry | 40 µs | 150 µs | estimate: PSA key import + HW AES; IDF's own floor is 43 MB/s AES-CBC (`idf_performance_target.h`) |

One unlock on the device with the owner's real vault (the log has the time
between "unlock" and the 200) would calibrate the model; that is the first
thing to measure before acting on the absolute seconds below. The ratios do
not depend on the model.

Entry shapes (sizes are bytes of the entry codec's plaintext; a file adds 29):

| shape | what | plaintext | file |
|---|---|---|---|
| small | title 14, URL 32, user 24, password 20 | 136 | 165 |
| typical | + TOTP URI 103, 120-byte note, 2 old passwords | 445 | 474 |
| notes600 | typical with a 600-byte note and 3 old passwords | 955 | 984 |
| max | every field at its limit, 10 old passwords, sequence | 6673 | 6702 |

## Phase 1: what 0.3.1 does

### 1. Storage

Entries written until the first write fails:

| shape | vault 1.875 MB (480 blocks) | 8 MB (2048) | 9.875 MB (2528) |
|---|---|---|---|
| small | 3196 | 13687 | 15759 |
| typical | 1373 | 5959 | 7025 |
| notes600 | **446** | 1916 | 2396 |
| max | **230** | 990 | 1230 |

With inline files turned off (`inline_max = -1`) small and typical both stop
at 445 / 1915: one 4 KiB block per file.

What this says:

- **Inline files** (≤ 512 bytes, i.e. plaintext ≤ 483) live inside the
  directory's metadata; littlefs splits a metadata pair once it is half full,
  so it keeps about 1 KiB of entries per 4 KiB block: ~6.7 small or ~2.9
  typical entries per block. A file one byte over 512 gets a whole block, so
  a single 600-byte note makes an entry cost 4 KiB.
- **`kMaxEntries = 1000` is not reachable today** with notes: the vault
  partition fills at 446 entries with 600-byte notes and at 230 entries at
  the field limits. A full filesystem shows up as `storage` (500), not as
  `full` (507), because `put` only counts entries.
- 5000 typical entries need the 8 MB tail; 5000 max-size entries (33 MB) fit
  nowhere on this board, so "5000 accounts" means typical ones.

### 2. Memory

While unlocked the vault keeps every entry's plaintext (`slots_`): 16 bytes of
`Slot` plus the codec bytes, allocated through `mem::alloc` (PSRAM first).
`Entry` copies are a different matter: on Xtensa `sizeof(Entry)` = 224,
`OldPassword` = 32, `std::string` = 24 with 15 characters inline (checked with
`xtensa-esp32s3-elf-g++`), and every longer string is its own `malloc`.
With `CONFIG_SPIRAM_MALLOC_ALWAYSINTERNAL=16384` a block under 16 KiB goes to
**internal RAM first**, so a `vault::list()` copy of the whole vault lands
mostly in the same ~300 KB that Wi-Fi, BLE and lwIP need.

Per entry (device layout, 4-byte rounding + ~8 bytes heap header):

| shape | slot (PSRAM) | `list()` copy | …of it in <16 KiB blocks |
|---|---|---|---|
| small | 160 | 336 | 112 |
| typical | 472 | 768 | 544 |
| notes600 | 980 | 1312 | 1088 |
| max | 6700 | 7280 | 7056 |

Totals, slots / one `list()` copy on top:

| shape | 1000 | 5000 | 10000 |
|---|---|---|---|
| small | 0.16 / 0.34 MB | 0.80 / 1.68 MB | 1.60 / 3.36 MB |
| typical | 0.47 / 0.77 MB | 2.36 / 3.84 MB | 4.72 / 7.68 MB |
| notes600 | 0.98 / 1.31 MB | 4.90 / 6.56 MB | 9.80 / 13.12 MB |

(The bench measured the slot sizes through `mem::alloc`: 466 KB for 1000
typical, 2329 KB for 5000.) Even 1000 typical entries put ~540 KB of small
blocks through `malloc` on every `GET /api/entries`, more internal RAM than
the board has free, so the rest spills to PSRAM only after internal RAM is
exhausted. The slots themselves are fine up to ~5000 typical; 10000 typical
plus one list copy does not fit in 8 MB.

### 3. Time

**Unlock** decrypts every entry (`loadEntries`). What it costs is not the
crypto but the lookups: littlefs finds a file by walking its directory's
chain of metadata pairs from the start, reading each block it passes, and
`LittleFsStorage::read` did two lookups per file (open, then `fstat`, which
esp_littlefs answers with `lfs_stat(path)`). So an unlock reads O(n²) bytes:

| shape | n | 0.3.1 (open + fstat) | read via open only | device model (0.3.1) |
|---|---|---|---|---|
| typical | 100 | 12.6 MB | 6.7 MB | 0.8–2.5 s |
| typical | 500 | 285 MB | 145 MB | 18–54 s |
| typical | 1000 | 956 MB | 483 MB | 60–178 s |
| small | 1000 | 390 MB | 200 MB | 25–75 s |
| small | 5000 | 9.6 GB | 4.8 GB | 10–30 min |
| typical | 5000 | 24.1 GB | 12.1 GB | 25–75 min |
| notes600 | 400 | 22 MB | 13 MB | 1.6–5 s |
| notes600 | 2000 | 490 MB | 254 MB | 31–95 s |

The GCM count is always n + 1; on the host the whole 1000-typical unlock is
3.1 s of CPU, all of it inside littlefs. Inline entries make it worse
(fewer entries per metadata block, longer chains). The PBKDF2 (~1.2 s,
calibrated at setup) comes on top. This is the bottleneck: even within
today's limit an unlock with ~1000 short entries takes about a minute, and
nothing short of a different layout fixes the n².

One **write** (a create or an edit: `.tmp` + rename) also walks the directory:
228 KB read at 1000 small entries, 1 MB at 5000, 3.8 MB at 10000
(0.1–0.7 s); a remove at 10000 read 11 KB. Writes stay usable; unlock does not.

**Listing** (`GET /api/entries`) was already streamed in 1 KiB chunks, but it
first took a `vault::list()` copy of everything (passwords, notes and history
included; see §2). The JSON is ~276 bytes per typical entry: 1.4 MB for
5000. Search happens in the web app over that list; the device has no search
endpoint. `GET /api/health` needs every password and still takes a full
`list()` copy; `POST /api/agent/match` too.

**Backup** (`exportBackup` through the real vault):

| shape | n | backup file | peak `mem::alloc` during export |
|---|---|---|---|
| small | 1000 | 0.36 MB | 0.59 MB |
| small | 5000 | 1.81 MB | 2.36 MB |
| typical | 1000 | 0.85 MB | 1.18 MB |
| typical | 5000 | 4.25 MB | **9.44 MB** |
| notes600 | 1000 | 1.57 MB | 2.36 MB |

The peak excludes the base64 output string (+ the file size again) and the
slots. A restore is capped at `kMaxRestoreBodyPsram` = 2 MiB of request body
and holds the body, its cJSON tree, a re-printed copy, the decoded ciphertext,
the plaintext and a parsed tree at once (~5–6× the file). So: 5000 typical
entries cannot be backed up (PSRAM runs out) and anything over ~2400 typical
entries cannot be restored; `readBackup` also refuses more than `kMaxEntries`.
The **CSV import** sends ≤ 50 entries per request; each request took a full
`list()` copy for its duplicate check (now `forEach`, below) and then writes
each entry (one directory walk per write).

### 4. Other limits that break with thousands

- **Watchdog.** Unlock and restore run on the httpd task. With
  `CONFIG_ESP_TASK_WDT_TIMEOUT_S=5` watching both idle tasks, a minute-long
  unlock keeps one core's idle task from running: the task watchdog fires and
  logs (panic is off, so no reboot). During that time the web server answers
  nobody else.
- **HTTP.** `recv_wait_timeout`/`send_wait_timeout` are 5 s per socket
  operation, fine for the list; the restore body has its own deadline (10 s +
  1 s per 32 KiB).
- **O(n) lookups in RAM.** `Vault::find` scans `slots_` (16 bytes per entry, in
  PSRAM): fine per call, but the merge restore compares every incoming entry
  with every existing one (`taken`, `sameAccount`), O(n·m).
- **Web.** `web/src/screens/Vault.tsx` renders every account (grouped by
  letter, no virtualisation) and searches the whole list in the browser. 5000
  rows is heavy on a phone; that belongs to the web work.
- **Wear** is not a problem at these sizes: littlefs levels writes
  (block_cycles 512) and one write programs a handful of pages.

## Phase 2: design

### Storage location: the verdict

Recommended: **a second littlefs in the unused upper 8 MB (0x800000–0xFFFFFF),
registered at run time, with entries packed into bucket files, filled by a
one-time migration that commits with one atomic write.** Details and the
alternatives follow.

**Can the tail be used without touching the partition table?** Yes, with one
catch. `esp_partition_register_external()` (ESP-IDF 6.0.2,
`components/esp_partition/partition.c:469`) accepts the main chip
(`flash_chip == NULL` means `esp_flash_default_chip`). It refuses only (a) an
overlap with a known partition on the same chip (`ESP_ERR_INVALID_ARG`) and
(b) `offset + size > esp_flash_get_size(chip)` (`ESP_ERR_INVALID_SIZE`). The
catch is (b): `esp_flash_get_size` returns the size in the **bootloader's
image header**, not the chip's (`spi_flash/esp_flash_spi_init.c:584-593`:
"Detected size larger than the size in the binary image header … Using the
size in the binary image header"), and Keyra builds with
`CONFIG_ESPTOOLPY_FLASHSIZE_8MB` so one image serves 8 and 16 MB boards. Every
`esp_flash_read/write/erase` checks that size too (`esp_flash_api.c`). The
bootloader is never updated over the air, so its header stays 8 MB on the
owner's board. Two ways out:

1. At run time, when `esp_flash_get_physical_size()` reports ≥ 16 MB, raise
   `esp_flash_default_chip->size` to it before registering the region. The
   field is public (`esp_flash_t::size`, "the available size"); nothing else
   is mapped above 8 MB and the 16 MB tail needs no 4-byte addressing. It is
   a deliberate departure from IDF's default and must live in one function
   with a comment; on an 8 MB board it does nothing.
2. `CONFIG_SPI_FLASH_SIZE_OVERRIDE` with a 16 MB app header
   (`esp_system/port/cpu_start.c:916`). Clean, but the same image would then
   refuse to boot on 8 MB boards (`ESP_ERR_FLASH_SIZE_NOT_MATCH`), i.e. two
   builds. Not worth it.

**Migration** (only on a board whose physical flash is ≥ 16 MB):

- Starts only when the running image is confirmed: after `decideBoot` called
  `esp_ota_mark_app_valid_cancel_rollback()` (`handlers_update.cpp:325`) and
  `esp_ota_get_state_partition()` says `ESP_OTA_IMG_VALID`, so the automatic
  rollback can never land on a firmware that cannot see the moved data.
- Copies ciphertext, so it needs no passphrase and runs while locked, under
  the vault mutex, as a `Vault` method over two `Storage`s.
- Steps: format the tail; copy every file (entries go into buckets, below;
  `meta.bin`, `f/`, `fido*.bin`, `activity.bin`, `tokens.bin`, `tags.bin`,
  `fidoctr.bin` as they are); read everything back and compare byte for byte;
  then write `vault.home` in the **tail** (`.tmp` + rename) — the commit.
- Boot rule: tail mounts and has `vault.home` → the tail is the vault;
  otherwise the old partition is, and a half-written tail is formatted on the
  next attempt. A power cut before the commit leaves the old vault untouched
  and authoritative; after it, the tail.
- After the commit the old partition is erased in full (a redo-able step at
  every boot while it still holds a `meta.bin`): stale ciphertext must not
  survive, since the DEK never changes and would still open entries the user
  deleted. A firmware older than the migration then sees an empty Keyra; going
  back below that version needs a restore from backup (decision below).

**Packing** (the n² fix). Keep each entry's sealed record exactly as today
(version | IV | ciphertext | tag, AAD `keyra/e/v1/<id>`) but store records in
a fixed number of bucket files, `b/<k>.bin` with k = id mod 128:
`u8 version | u32 count | count × (u32 len | record)`. An unlock opens 128
files and reads the data once: for 5000 typical entries ~2.4 MB, about
0.2–0.5 s of flash plus 5000 GCM opens (0.2–0.75 s), against 25–75 minutes
today. A write rewrites one bucket (~40 entries, ~19 KB at 5000 typical) with
`.tmp` + rename, so it stays atomic; that is ~5 erases (~0.2–0.5 s), the same
order as a write today at 5000 entries. Replace-restores stage whole buckets
(`b/<k>.new`) under the same `restore.commit` marker. On 8 MB boards the same
packing can be done inside the existing partition, bucket by bucket (write
the bucket, then remove the files it absorbed; an id found in both is the same
record), which also lifts that partition to an estimated ~3500 typical entries (not measured).

**Alternatives rejected:**

- *Grow `vault` by rewriting the partition table and `lfs_fs_grow`*: the table
  is one sector at 0x8000 that the bootloader needs; a power cut between its
  erase and write leaves a board that does not boot and can only be fixed over
  USB, which this product (OTA-only) does not have. IDF also aborts such a
  write by default (`CONFIG_SPI_FLASH_DANGEROUS_WRITE_ABORTS=y`).
- *Packing without the tail*: fixes the time, but 1.875 MB would hold an estimated ~3500 typical
  entries at best, short of 5000 with margin. Right for 8 MB boards only.
- *Tail without packing*: 5959 typical entries fit, but an unlock at 5000 would
  still read gigabytes. Moving and packing are one migration, done once.

### Memory

- With the tail and packing, keeping all plaintext in `slots_` is fine to
  ~5000 typical (2.4 MB of 8 MB PSRAM). Beyond that, keep only an index in RAM
  (id, title, URL, username, flags, times: ~120 bytes in PSRAM) and decrypt
  the rest from its bucket on `get` (one bucket read, a few ms). That also
  keeps passwords out of RAM until they are typed. Not needed for the 5000
  target; worth it for 10000.
- No caller should copy the whole vault: `list()` remains for health and
  agent match, which should move to `forEach` too (health needs each password
  only long enough to score it; companion::match needs id/title/url/username).
- `importBackup(merge)`: index existing entries in a hash map by id and by
  (title, username, url) instead of the O(n·m) scans.

### Time and the API

- `GET /api/entries` stays one JSON document (no client change). It is now
  built from summaries while the vault iterates and sent with a
  Content-Length. Paging (`?offset=&limit=`) is not needed for 5000 (1.4 MB,
  about a second over Wi-Fi) and would need the web app to change.
- Unlock should leave the httpd task: run it in the worker that already
  performs button-confirmed actions and answer 202, polling like the others,
  or at least feed the watchdog. With packing it is short enough that this is
  hygiene, not a fix.
- Backup must stream: the v3 envelope can be produced incrementally (PSA
  multipart AEAD, base64 in 3-byte groups, chunked response) without changing
  a byte of the file; restore can decrypt into staged buckets and commit only
  after the GCM tag checks out. Then the 2 MiB body cap and the 5–6× copies go,
  and `kMaxEntries` can rise. Until then, raising `kMaxEntries` would create
  vaults that cannot be backed up.

## Implemented now (no stored format, no partition change)

1. `vault::forEach(fn)`: visits entries in `list()` order, one decoded copy at
   a time, wiped after the callback. `GET /api/entries`,
   `GET /api/agent/entries` and the CSV import's duplicate check use it
   instead of `list()`; the list JSON is identical. Peak RAM for listing drops
   from the table in §2 (e.g. 3.84 MB, 2.7 MB of it small blocks, for 5000
   typical) to the response text (~1.4 MB, one large block that goes to
   PSRAM) plus one entry. Host test: `for_each_matches_list`.
2. `LittleFsStorage::read` uses `open`/`lseek`/`read` instead of
   `fopen`/`fstat`/`fread`: `lseek` asks littlefs for the open file's size
   without a second path lookup. Measured on the bench: unlock reads drop by
   47–50% (956 → 483 MB at 1000 typical entries). It halves the n², it does
   not remove it.

`kMaxEntries` stays 1000 and nothing was migrated.

## Decisions for review

1. Approve the tail + packing migration (and the `esp_flash_default_chip->size`
   raise it needs on 16 MB boards), or ask for an alternative.
2. Downgrades after the migration: accept "older than version X needs a
   restore from backup", or keep a copy for old firmware (costs the stale-
   ciphertext problem above).
3. Measure one unlock on the real device to calibrate the time model.
4. Order: streaming backup/restore before raising `kMaxEntries`; the new limit
   (proposal: 5000 on 16 MB boards, ~3000 on 8 MB) and a "storage full" answer
   (507 `full`) when the filesystem, not the count, runs out.
5. Web: a virtualised account list (another agent's area).
