# NFC tap tags (design note)

Written 2026-10-09 for ROADMAP wave 5 "NTAG 424 tap-to-arm tags"
([FEATURES-2.md](FEATURES-2.md) H14/D11, [FEATURES.md](FEATURES.md) F42/N5).
Builds on access tokens ([TOKENS.md](TOKENS.md), SPEC §17). The contract is
SPEC §18; this note records why it looks the way it does.

## The idea

Keyra has no NFC hardware and will not get any (FEATURES R1/H15). A phone
already reads NFC tags on its own: an iPhone XS or newer reads an NDEF URL tag
in the background and shows a banner; tapping it opens the URL in Safari. So a
sticker on the desk or the monitor stand holds a URL on Keyra
(`http://keyra.local/t/…`). Opening it while the phone is on Keyra's network
(home Wi-Fi, or Keyra's own Wi-Fi) **arms one account for typing**, exactly like
`POST /api/type`, and the human presses Keyra's button. Tap, press, logged in.
The tag never shows or returns a secret; the only thing it can do is arm.

## Decisions

| Question | Decision | Why |
|---|---|---|
| Kinds | **Simple** (any NTAG213/215/216): `http://keyra.local/t/<id>/<secret>`, secret = 24 base32 chars (120 bits). **Secure** (NTAG 424 DNA with SUN/SDM): `http://keyra.local/t/<id>?p=<PICCData>&m=<SDMMAC>`. | A simple tag is writable from an iPhone with free apps (NFC Tools, NXP TagWriter) in a minute. A secure tag cannot be copied, but needs a 424 DNA tag and TagWriter's SDM setup. Offer both, say honestly what each protects. |
| Simple tag honesty | Anyone who reads or photographs the URL can arm **that one account** — never read it — and the press is still required. | A static URL is copyable by design (FEATURES F42). What it buys is speed; the button stays the security. |
| Secure tag check | Decrypt PICCData (AES-128, SDM meta-read key) → tag `0xC7`, UID, counter; derive the session MAC key from SV2 = `3CC3 0001 0080 ‖ UID ‖ counter(LE)` with AES-CMAC under the SDM file-read key; SDMMAC = the odd bytes of AES-CMAC(session key, empty input). Counter must be **greater than the last one seen** (replay). UID is bound on the first good tap (trust on first use) and checked after. | NXP AN12196 §3.3–3.4. Empty MAC input (`SDMMACInputOffset = SDMMACOffset`) is the simplest setting TagWriter offers and the one the AN12196 example uses. The keys are random per tag, so a valid MAC already proves the tag; binding the UID stops the same keys being written to a second tag later. |
| Crypto | AES-CMAC (RFC 4493) is written once in the host-tested core over a one-block AES-128 primitive; the firmware gives it PSA `AES-ECB`, host tests give it OpenSSL. | No dependence on `MBEDTLS_CMAC_C` being enabled; the CMAC itself is tested against RFC 4493 and the AN12196 / NXP SDM example (`EF963FF7…`/`94EED9EE65337086`, all-zero keys, UID `04DE5F1EACC040`, counter 61). |
| Keys | Keyra generates both 16-byte keys per tag with the hardware RNG and shows them **once** (hex) with the exact SDM settings and offsets for TagWriter. | The user never invents a key, and a lost key means "revoke, make a new one". |
| Storage | A sibling vault record `tags.bin` (AES-256-GCM with the DEK, AAD `keyra/tags/v1`), at most **16** tags. Per tag: id, name, kind, entry id, what, target, simple → SHA-256(secret); secure → both AES keys, UID (once bound), last counter; created, lastUsed. | Same pattern as `tokens.bin`; adding a kind to the token record would have meant a new token format for the Android work in flight. Keys are inside the DEK-sealed record: unreadable from flash, gone with a factory reset. Not in backups (a restored tag would accept old counters). |
| Locked vault | Tap page: "Unlock Keyra first". The API answers 401 `locked`. | Without the DEK Keyra cannot read the hashes or keys, and typing needs the vault open anyway. |
| GET vs POST | `GET /t/…` returns a tiny self-contained page (no external assets, Arabic or English from the browser's language) and **changes nothing**; its script POSTs `/api/tag/tap`, then polls `/api/tag/status` and shows "Press Keyra's button" → "Typed". | Link previewers (Messages, Slack, mail) fetch URLs with GET and do not run scripts: a preview must never arm anything or burn a secure tag's counter. |
| Arming | `POST /api/tag/tap` reuses the type path (`typereq::readTarget/entryRequest`, the pending machine): same 60 s, press, targets, 409 `busy`. Owner `tag:<id>`: a tag replaces only its own waiting item and no browser or token item. | One state machine, one button meaning (§12.5a). |
| Status | `POST /api/tag/status {id, ticket}` — the tap's answer carries a random `ticket`; status follows that request by serial. | The tap page has no session; a ticket keeps strangers from following someone else's tap (there is little to learn anyway). |
| Rate limit | 10 taps per 10 s per tag, plus one shared budget for unknown tags and wrong secrets/MACs (the token limiter, a separate instance). 429 `rate_limited`. | A looping script cannot flood the log or the LED; guessing stays cheap to refuse (and hopeless: 120 bits / AES-CMAC). |
| Creation | Settings → *NFC tags*: `POST /api/tags` → 202 press (`tag_create`) → the same call again within 60 s → 201 with the URL (simple) or the keys and URL template (secure), shown once. Revoke: `DELETE /api/tags/{id}`, no press. | Same as tokens: a stolen session cannot mint a tag silently; taking power away is never blocked. |
| Activity log | `tag_created`, `tag_revoked`, `tag_tapped` (entry + tag name + what), `tag_refused` (wrong secret/MAC, replay, coalesced). Typing itself is the usual `typed`. | Every use is visible; a flood of bad taps is one counted line. |
| Auto-lock | Taps are not user activity. | A tag left near a phone must not keep the vault open. |

## Threat model

- **Copied simple tag** (someone reads the sticker, sees the URL in Safari's
  history, or a shoulder-surfer photographs it): they can arm that one account
  while on Keyra's network. Nothing is revealed; the press types into whatever
  is focused on *your* computer, so they gain nothing unless they also control
  that field. Revoke the tag in Settings; make a secure tag if this matters.
- **Cloned secure tag**: the AES keys never leave the tag (they are not
  readable), so a clone produces no valid MAC. A recorded URL is refused once
  any newer tap was seen (and immediately if it was already used): replay
  answer 409 `replayed`. Residual: a URL read off the tag by someone else's
  phone *and never used by you* can be used once — that is inherent to SUN
  (the tag cannot know who reads it).
- **Link previews / prefetch**: GET changes nothing (see above).
- **LAN observer**: plain HTTP (SPEC §6). A captured simple-tag URL is a
  copied tag; a captured secure-tag URL is one replayable tap until the next
  real tap (then refused). Same honest limit as tokens.
- **Loopjacking-style approval hijack**: unchanged from TOKENS.md — one item at
  a time, nobody replaces another owner's item, the phone's pill and LED name
  the account and "requested by <tag name>".

## Not done here

- Writing the SDM configuration from Keyra's own page (Web NFC is Android
  Chrome only and needs HTTPS). iPhone can *read* tags in the background; it
  can write a simple URL with NFC Tools or TagWriter, and NXP TagWriter (iOS
  and Android) can configure SDM on a 424 DNA tag. Changing the tag's AES keys
  needs a tool that does `ChangeKey` (TagWriter's NTAG 424 DNA settings, or
  NXP's desktop/Android tools). **Not verified on a real tag yet.**
- An entry chosen per tap (a tag arms one fixed account by design).
