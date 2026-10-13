# Keyra — Design System & Screen Spec (v1)

Audience: the frontend engineer (Preact + TypeScript + plain CSS, hashed files embedded in the firmware: ≤ 130 KB
gzip to open the vault list, ≤ 400 KB in all, fonts included — Appendix A) and the owner (image prompts in §7). Companion to `SPEC.md` (§1 is the experience).
Every number below is final unless marked `ASSUMPTION`. Contrast ratios were **computed** (WCAG 2.x
relative luminance), not eyeballed. Font size was **measured** by actually subsetting the font.

Contents: 0 Direction · 1 Brand · 2 Tokens · 3 Typography · 4 Components · 5 Screens · 6 Accessibility ·
7 Image prompts · 8 References

---

## 0. Direction — "Graphite & Ivory"

**`docs/brand/BRAND.md` is the source of truth** for the logo, palette, type and motion; this file applies it to the
web app. The research behind it (Linear, Vercel, Raycast, Apple, Teenage Engineering, Nothing, Opal) is in
`docs/research/design-references-2026.md`; where its tokens differ from BRAND.md, BRAND.md wins.

1. **Monochrome, warm.** Ink on ivory paper (light), ivory on graphite (dark). Neutrals carry a little warmth
   (Linear's "warmer gray", but never muddy). No gradients on content, no tinted icon tiles.
2. **Blue means one thing: Keyra is waiting for your press.** It is the LED's colour (firmware `led_anim.cpp`,
   `Pattern::Pending`, 1200 ms breath). Allowed only on: the Contour while waiting (§4.11), the "Ready" pill's dot,
   the `.chip-signal` in Ready, and the sequence part the next press types. Never on buttons, links, icons, focus
   rings, switches or sliders. Primary actions are ink (light) / ivory (dark).
3. **One line.** The mark is a shield drawn as one continuous line with a gap where the press closes it. Lines carry
   the identity: hairlines instead of boxes (`box-shadow: 0 0 0 1px var(--hair)`), outlined icons with round caps,
   the Contour as the press indicator.
4. **Short, meaningful motion.** Four durations (120/200/320/420 ms), an ease-out for things appearing, an iOS-sheet
   curve for things that slide, a symmetric curve for breathing. Nothing loops except the waiting breath. Full
   reduced-motion path (§6).
5. **Arabic-first craft within 130 KB to the list.** One embedded Arabic+Latin variable font (Readex Pro, 29.5 KB),
   logical CSS only, Arabic 6 % larger and never tracked, Western digits everywhere so codes match what Keyra types.
   No icon font, no images, no animation library: the mark, grain and icons are inline SVG/CSS.

---

## 1. Brand

### 1.1 Name treatment
- Latin: **Keyra** — Readex Pro 500, tracking −0.02 em, capital K only. Never all-caps.
- Arabic UI keeps the Latin name inside sentences (`<bdi>`); the Arabic lockup (**كيرا**) is for brand art only
  (`docs/brand/logo/logo-ar.svg`).
- Voice in UI: calm, brief, second person, warm. No exclamation marks except onboarding completion.
  Technical words only when needed ("USB", "Wi-Fi"). Never blame the user. Arabic says «كلمة المرور / كلمات المرور»
  everywhere (never «كلمة السر»).

### 1.2 Personality (3 words, each with a rule)
- **Calm** — one signal colour with one meaning, lots of air, hairlines over boxes.
- **Tangible** — the UI points at the object: "press Keyra's button"; the Contour on screen breathes with the LED.
- **Precise** — monospace for secrets and receipts, exact radii, numbers set large on the Security dashboard.

### 1.3 Logo — the Contour
Defined in BRAND.md §2 (geometry, stroke 5 / bold 8, files in `docs/brand/logo/`). In the app:
- `Contour` (`web/src/components/Icon.tsx`): bare mark in `currentColor`, viewBox `14 14 72 72`; under 48 px it switches
  to the bold variant (stroke 8, wider gap). Used for the rail, empty states, the locked screen, monograms without a
  letter, the Welcome hero (drawing itself once per session).
- `ContourTile`: the graphite app-icon tile (gradient, hairline rim, ivory line) for Unlock, the splash and the
  Device tab's identity card.
- The mark **never mirrors** in RTL (no `.i-dir`), like the countdown ring.
- Icons, favicon and manifest are generated from BRAND.md's sources (`npm run icons`); do not redraw them here.

### 1.4 PWA/meta
`theme-color`: `#F4F3EF` (light) / `#0B0C0E` (dark) via two `<meta … media>` tags. Manifest colours graphite
`#0B0C0E`. `apple-mobile-web-app-capable=yes`, `apple-mobile-web-app-title=Keyra`, viewport
`width=device-width,initial-scale=1,viewport-fit=cover`.

---

## 2. Tokens

Rules: only logical properties; colours only via tokens; no raw hex in components. Theme resolution:
`data-theme` attribute on `<html>` = `auto | light | dark` (persist in `localStorage["keyra.theme"]`, default
`auto`). `auto` follows `prefers-color-scheme`. **Do not use `light-dark()`** (breaks iOS < 17.5; a broken
token set would blank the whole UI). The dark block is therefore written twice (media + attribute) — generate
it from one object in a tiny Vite plugin or just paste; gzip makes the duplicate cost ≈ 0.3 KB.

### 2.1 Colour tokens — contrast table (computed)

Values are BRAND.md §3; the code is `web/src/styles/tokens.css` (the only place a hex value may appear, apart from
the Contour tile and the Device identity card, which are graphite in both themes). All ratios computed with the WCAG
2.x relative-luminance formula. Text ≥ 4.5 : 1, UI boundaries ≥ 3 : 1.

| Light (ivory paper) | Hex | On `--bg` #F4F3EF | On `--surface` #FFF | On `--surface-2` #EFEEE9 | On `--fill` #E9E8E3 |
|---|---|---|---|---|---|
| `--ink` | `#111214` | 16.88 | 18.74 | 16.13 | 15.28 |
| `--ink-2` | `#4A4C52` | 7.73 | 8.58 | 7.39 | 7.00 |
| `--ink-3` (captions, inactive tabs) | `#66686F` | 5.01 | 5.56 | 4.79 | 4.53 |
| `--hair-strong` (field outlines, switch off) | `#7E7C76` | 3.76 | 4.17 | — | — |
| `--signal` (waiting only) | `#1F5BEF` | 4.98 | 5.52 | 4.76 | — |
| `--ok` / `--err` / `--warn` | `#0A7A3D` / `#C1251C` / `#8F5200` | 4.89 / 5.33 / 5.60 | 5.43 / 5.92 / 6.22 | — | — |

Other light pairs: `--on-ink` #F4F3EF on ink 16.88 (primary button) · `--signal` on `--signal-soft` #E8EEFD 4.76 ·
ok on `--ok-soft` #E4F1E8 4.67 · err on `--err-soft` #FBE7E4 4.97 · warn on `--warn-soft` #F8EDDA 5.37 ·
white on `--err` (danger confirm) 5.92 · toast ivory #F2F2EF on ink 16.71 (ok icon #4ADE80 10.76, err #FF7A70 7.38) ·
focus ring = ink 16.88 on bg.

| Dark (graphite) | Hex | On `--bg` #0B0C0E | On `--surface` #141518 | On `--surface-2` #1B1C20 | On `--fill` #222328 |
|---|---|---|---|---|---|
| `--ink` | `#F2F2EF` | 17.45 | 16.28 | 15.18 | 13.98 |
| `--ink-2` | `#A9ABB2` | 8.53 | 7.96 | 7.42 | 6.84 |
| `--ink-3` | `#8A8C93` | 5.83 | 5.44 | 5.07 | 4.67 |
| `--hair-strong` | `#6E7076` | 3.95 | 3.69 | 3.44 | — |
| `--signal` | `#6C9CFF` | 7.30 | 6.81 | 6.35 | — |
| `--ok` / `--err` / `--warn` | `#4ADE80` / `#FF7A70` / `#FBBF4A` | — | 10.48 / 7.19 / 11.00 | — | — |

Other dark pairs: `--on-ink` #0B0C0E on ivory 17.45 (primary button) · signal on `--signal-soft` #16203A 6.02 ·
ok on #10261A 9.17 · err on #33171A 6.47 · warn on #2D2410 9.23 · `--on-err` #0B0C0E on err 7.71 · segmented thumb
#33343A under ivory text 11.06 · toast inverts to ivory with graphite text 17.45 (ok #0A7A3D 4.85, err #C1251C 5.28).

Monograms (muted earth tones, white letter): `#8C4A2F 6.70 · #7D5A1C 6.27 · #5B6A2C 5.93 · #2F6A4E 6.38 · #2D6366 6.80 ·
#4A5160 7.96 · #5F4C7C 7.47 · #7C3D5E 7.82 · #8C3A35 7.58 · #45503F 8.49 · #6E5542 6.91 · #3A3B40 11.17`. No blue.

### 2.2 Tokens in code
`web/src/styles/tokens.css` holds the colour, radius (`--r-xs 6 · sm 10 · md 14 · lg 20 · xl 28`), spacing (4-pt),
type and motion tokens. The dark block is written twice (`@media (prefers-color-scheme:dark)` on
`:root:not([data-theme="light"])`, and `:root[data-theme="dark"]`) because `light-dark()` breaks iOS < 17.5; keep the
two copies identical. `prefers-contrast: more` maps `--hair` to `--hair-strong` and `--ink-3` to `--ink-2`.

Motion tokens (BRAND §5, research §2.6):
```css
--d-1:120ms; --d-2:200ms; --d-3:320ms; --d-4:420ms; --d-draw:900ms;
--d-breath:1200ms;                      /* = firmware Pattern::Pending (led_anim.cpp); change both together */
--ease-out:cubic-bezier(.2,.8,.2,1);    /* things appearing */
--ease-slide:cubic-bezier(.32,.72,0,1); /* sheets, segmented thumb */
--ease-state:cubic-bezier(.4,0,.6,1);   /* breathing, colour */
--ease-in:cubic-bezier(.4,0,1,1);       /* exits */
--ease-spring:linear(…);                /* only the switch knob; plain ease-out where linear() is missing */
```
Reduced motion shortens every duration to 120 ms and zeroes `--d-draw` (§6).

### 2.3 Materials
| Material | Where | Recipe |
|---|---|---|
| **Paper** | every surface | solid `--bg` / `--surface` / `--surface-2` levels; elevation is read from lightness |
| **Hairline** | cards, rows, sheets, chips | `box-shadow:0 0 0 1px var(--hair)`; separators inset after a row's icon (`inset-inline-start:56px`) |
| **Grain** (the one extra material) | Welcome, Setup, Unlock only (`.paper`) | inline SVG `feTurbulence`, opacity .06, fixed; off with reduced transparency/high contrast; never under scrolling lists |
| **Glass** | top bar and phone tab bar only (content scrolls under them) | `--glass-bg` + `backdrop-filter: blur(20px)`; solid fallback |
| **Graphite object** | Contour tile, Device identity card | graphite `#141518` in both themes, ivory line/text |

### 2.4 Layout tokens
Breakpoints: `<600` phone (gutter 16; 20 at ≥ 414) · `≥600` tablet (content max 560, centred) ·
`≥900` desktop two-pane (list 380 px | detail flexible, content max 1120, outer gutter 32). Use
`env(safe-area-inset-*)` on bars/sheets; use `100dvh`, never `100vh`.

---

## 3. Typography

**Decision: embed exactly one font — Readex Pro, subset, variable weight 400–700 — as one hashed `woff2` file
(`/assets/keyra-sans-<hash>.woff2`), preloaded by `index.html` and registered with `FontFace` (`web/src/lib/font.ts`).** Why not system fonts: Arabic system fonts differ wildly (SF Arabic / Segoe UI / Noto), so the same
screen would have 3 characters and different line metrics; the budget allows one font; Readex Pro (Lexend
family, OFL) is a geometric humanist with a **native-designed Arabic and a matching Latin** — one file covers
both scripts and harmonises with the ring/key geometry. System stack stays as fallback while loading.

**Measured:** Arabic letters (U+0620–064A, harakat, Arabic punctuation, Arabic-Indic digits, Persian پ چ ژ ک گ ی),
Basic Latin, quotes/dashes/bullet/ellipsis, all GSUB/GPOS shaping kept, hinting dropped → **29,524 bytes
woff2, 370 glyphs** (Latin-only would be 11.6 KB; not worth a second file). Served as is (woff2 is already
compressed) → 28.8 KB on the wire, once: its hashed name lets the browser keep it. Missing glyphs (✓ ● ✔) are **SVG icons**, never characters.
Kurdish Sorani extras (ڕ ڵ ۆ ێ ە) are not included — add U+0695, U+06B5, U+06C6, U+06CE, U+06D5 to the
`--unicodes` list only if a Kurdish UI is requested (≈ +1 KB). User data in those letters still renders via
fallback fonts.

Recipe (reproducible; run once, commit the output `web/src/fonts/keyra-sans.woff2`):
```sh
pip install fonttools brotli
curl -L -o ReadexPro.ttf "https://github.com/google/fonts/raw/main/ofl/readexpro/ReadexPro%5BHEXP%2Cwght%5D.ttf"
python - <<'E'
from fontTools.ttLib import TTFont; from fontTools.varLib import instancer
f=instancer.instantiateVariableFont(TTFont('ReadexPro.ttf'),{'HEXP':0,'wght':(400,700)}); f.save('rx.ttf')
E
pyftsubset rx.ttf --flavor=woff2 --no-hinting --desubroutinize --layout-features='*' \
 --unicodes="U+0020-007E,U+00A0,U+00B7,U+00D7,U+2013,U+2014,U+2018,U+2019,U+201C,U+201D,U+2022,U+2026,U+0600-0605,U+0609-060D,U+0620-064A,U+064B-0652,U+0640,U+0660-0669,U+066A-066C,U+067E,U+0686,U+0698,U+06A9,U+06AF,U+06CC,U+200C-200F" \
 --output-file=keyra-sans.woff2
```
```ts
new FontFace('Keyra Sans', `url(${fontUrl}) format('woff2')`, { weight: '400 700', style: 'normal', display: 'swap' })
```
Licence: Readex Pro is SIL OFL 1.1 — include the licence text in `README` credits (not in the bundle).

### 3.1 Stacks
- UI: `--font` (above). Mono (passwords, usernames when revealed, TOTP, SSID, IP): `--font-mono`. Mono is also
  how we get **tabular numerals** — Readex digits are proportional and the font has no `tnum`; do not rely on
  `font-variant-numeric`. (Fallback chain gives SF Mono / Menlo / Consolas / Roboto Mono; all have slashed or
  dotted zero.)
- Digits: **always Western 0–9**, in both languages (Keyra types ASCII; the screen must match the keystrokes).

### 3.2 Scale (rem; root = 16 px; **Arabic root = 106.25 % = 17 px** so every rem token grows 6 %)
Arabic needs ~10–15 % more optical size and generous leading (marks above/below); Latin uses tighter leading.

| Token | rem | px EN → AR | Weight | LH EN | LH AR | Use |
|---|---|---|---|---|---|---|
| `--fs-display` | 2.25 | 36 → 38 | 500 | 1.08 | 1.35 | Unlock/Welcome title (luxury reads lighter and larger) |
| `--fs-t1` | 1.75 | 28 → 29.8 | 600 | 1.15 | 1.40 | Tab page titles (Accounts, Type, Security, Device) |
| `--fs-t2` | 1.375 | 22 → 23.4 | 600 | 1.30 | 1.50 | Sheet titles, empty-state titles |
| `--fs-headline` | 1.0625 | 17 → 18 | 600 | 1.35 | 1.60 | Row title, button label, section label |
| `--fs-body` | 1 | 16 → 17 | 400 | 1.45 | 1.70 | Paragraphs, inputs |
| `--fs-callout` | .9375 | 15 → 16 | 400/500 | 1.40 | 1.65 | Row subtitle, helper text |
| `--fs-caption` | .8125 | 13 → 13.8 | 500 | 1.35 | 1.60 | Captions, chips, Arabic section headers |
| `--fs-micro` | .6875 | 11 (Latin only) | 500 | 1.3 | — | Latin section eyebrows (uppercase, +.06 em), tab labels |
| `--fs-code` | 1.125 | 18 → 19 | 500 mono | 1.5 | 1.5 | Revealed password/username |
| `--fs-totp` | 2.125 | 34 → 36 | 600 mono | 1.1 | 1.1 | 2FA code `482 913` |

Rules: Arabic never below 400 weight, never letter-spaced, never italic/faux-bold, never uppercase.
Latin tracking: display `−0.022em`, t1 `−0.02em`, t2 `−0.015em`, headline `−0.011em`, else 0 (`:lang(en)` only; Arabic never). Min text size 13 px (EN) /
13.8 px (AR). `font-synthesis:none`. `text-wrap:balance` on titles, `pretty` on paragraphs. `-webkit-text-size-adjust:100%`.
`font-feature-settings:"calt","liga","rlig"` default (Arabic contextual forms must stay on; never set
`font-variant-ligatures:none` or `letter-spacing` on Arabic).

Bidi: user content (titles, notes) → `dir="auto"`; credentials (username, password, URL, TOTP, SSID,
passphrase) → `dir="ltr"` with `text-align:start`… in an RTL page that is **left**-aligned inside the field —
intended (Apple Passwords does the same). Wrap Latin brand names inside Arabic sentences in `<bdi>`.
Lists: titles may mix scripts; keep the subtitle (username) `dir="ltr"` `unicode-bidi:plaintext`.

---

## 4. Components

Global interaction rules: `touch-action:manipulation`; `-webkit-tap-highlight-color:transparent`; UI chrome
`user-select:none`, revealed secrets `user-select:text`; press feedback ≤ 120 ms; hover styles only inside
`@media (hover:hover)`; all hit areas ≥ 44 × 44 (extend with `::after{inset:-Npx}` when the visual is smaller).
Focus ring (every focusable): `outline:2px solid var(--focus);outline-offset:2px` — `--focus` is ink, never blue;
`:focus-visible` only; never `outline:none` without replacement.

### 4.0 Icons
Inline SVG `<symbol>` sprite (`web/src/components/Icon.tsx`), 24 × 24 grid, stroke 1.75 px, round caps/joins (the
Contour's pen), `currentColor`, no fills except `star` (filled when on). Source: **Lucide** (ISC). Navigation and
settings rows use: `key-round` (Accounts), `keyboard` (Type), `shield-check` (Security), `cpu` (Device), `wifi`, `house`,
`monitor`, `bluetooth`, `smartphone`, `download`, `upload`, `import`, `bot`, `nfc`, `sun`, `moon`, `languages`, `refresh-cw`,
`info`, `trash-2`, `timer`, `lock`, `eye-off`, `pulse`, `activity`, `life-buoy`, `list-ordered`, `type`, `wand-sparkles`, `zap`,
`circle` / `circle-check` (checklist), `chevron-down` (target chip). In a settings row the icon sits in a 28 px paper
tile with a hairline (`.row-glyph`), ink-2; never a coloured tile. Direction-bearing icons (`chevron-left/right`) mirror
in RTL (`.i-dir`); all others never mirror. "Both" = `user` + `key-round` side by side (mirrors with reading order).

### 4.1 Buttons
Height/radius: **lg** 56 / 20 (action buttons), **md** 48 / 16 (default), **sm** 36 / 12 (hit area padded to 44).
Label `--fs-headline` 600. Icon 20–24 left of label (start side). Min width 96. Full-width on phones inside sheets.

| Variant | Rest | Hover (`hover:hover`) | Pressed | Disabled |
|---|---|---|---|---|
| **Primary** | bg `--ink`, text `--on-ink` (graphite button on paper; ivory button on graphite) | ink mixed 14 % toward bg | `scale(.97)` 120 ms | opacity .38 |
| **Tinted** | bg `--fill`, text `--ink` | `--fill-press` | `--fill-press`, scale .97 | opacity .38 |
| **Secondary** | transparent, text `--ink`, 1 px `--hair-strong` ring | `--fill` | `--fill`, scale .97 | opacity .38 |
| **Ghost** | transparent, text `--ink` | `--fill` | `--fill`, scale .98 | opacity .38 |
| **Danger** | bg `--err-soft`, text `--err`; **confirm** variant bg `--err`, text `--on-err` | 8 % darker | scale .97 | opacity .38 |

Links are ink with a 1 px underline in `--hair-strong` (`.link-btn`, `.link-line`), never blue.
Spinner: 20 px, 2 px stroke, 700 ms linear infinite rotate (static ring + "…" when reduced motion).
One primary button per screen region. Success flash on async completion: label crossfades to `check` icon 600 ms.

### 4.2 Action buttons (account sheet — the heart of daily use)
Order top → bottom: **Both** (primary, most common), then **Username | Password** (two columns), then **Code**
card (only if `hasTotp`). Gap 12.

- **Both:** the one ink action: full width, 68 px high, radius 20, bg `--ink`, text `--on-ink`. Start cluster `user`+`key-round`
  icons (24), label "Both" 17/600, sublabel 14 at 72 % "Username, then password".
- **Username / Password:** `--surface-2` tiles with a hairline, ink text, 80 px high, radius 20, icon 28 above label (centred, 6 px gap).
  Each tile has a 36 × 36 **copy** chip at the end-top corner (hit area 44): copies that field to the phone
  clipboard and shows toast "Copied". Tapping the tile body = type action. Chip visible always (no hover-only).
- **Code card:** surface-2, radius 20, padding 16. Start: code in `--fs-totp` mono, groups of 3 separated by a
  0.35 em gap (`482 913`), `dir=ltr`. Under it: caption "Renews in 14 s" + 28 px ring (see 4.12). End: tinted sm
  button "Type" and a copy chip. Tap code = copy. The code refreshes from `GET /totp` every `remaining`s +
  200 ms; last 5 s the digits turn `--warn`. `409 no_time` → card shows caption "Clock not set — reload the page".
- Press = Ready state (4.11) replaces the action area **in place** (height-animated 320 ms), not a new page.
- A secondary line under the buttons (callout, `--ink-3`): "Keyra types it after you press its button."

### 4.3 Account row
Min height 64; padding 12 16; layout: monogram (40 × 40, radius 12) · 12 gap · text column · trailing.
Title `--fs-headline` (1 line, ellipsis, `dir=auto`); subtitle `--fs-callout` `--ink-2` = username (`dir=ltr`, 1 line)
or, if empty, host of URL. Trailing: `star` 16 px `--ink-2` if favorite; `chevron-right` 16 px `--ink-3` (mirrors).
Rows sit in an **inset group card**: `--surface`, radius 20, `--sh-1`, rows separated by 1 px `--hair`
starting after the monogram (`margin-inline-start:68px`). States: pressed bg `--fill`; focus ring inside
(offset −2); hover (desktop) `--surface-2`; selected (desktop two-pane) bg `--surface-2`, a 2 px ink bar on the
start edge. Long-press: none (no hidden gestures). Swipe: none.
Skeleton row: monogram circle + two bars (60 % / 35 % width, 12 px tall, radius 6).

**Monogram (deterministic colour, no network icons):**
- Letter: first letter/digit of the trimmed title via `Array.from` (never `[0]`), Latin upper-cased; if an
  Arabic title begins with the article "ال" followed by a letter, use the letter after it ("البنك" → "ب");
  empty title → the Contour (bold).
- Colour: `i = FNV-1a32(NFKC(title.trim().toLowerCase())) mod 12`; fill = `P[i]`; letter `#FFFFFF`, weight 500,
  centred; a top light `linear-gradient(180deg,rgba(255,255,255,.14),transparent 60%)`.
- `P` = muted earth tones, no blue (contrast in §2.1): `#8C4A2F #7D5A1C #5B6A2C #2F6A4E #2D6366 #4A5160 #5F4C7C #7C3D5E #8C3A35 #45503F #6E5542 #3A3B40`.
  Same palette in light and dark (white letter works on both).
- Sizes: 40 (row), 64 (sheet header), 28 (inline). Radius = 30 % of size.

### 4.4 Search field
Height 44, radius 14, bg `--fill`, no border (it is a text-input *affordance* by shape + icon). Start: `search`
20 px `--ink-3`; text `--fs-body` `--ink`; placeholder `--ink-3` (4.96:1 on fill). End: `x` clear button 44 × 44
visible when non-empty. Focus: bg `--surface`, 1.5 px ink ring. Sticky under the top bar
(`top:var(--bar-h)`, z 10) inside the same glass bar on phones. Search is client-side and instant:
normalise both sides (lower-case; strip tashkeel U+064B–0652 and tatweel U+0640; ا أ إ آ → ا; ى → ي; ة → ه;
Arabic-Indic digits → Western) and match title, url host, username; rank: prefix of title > word-prefix >
substring. Debounce 0 (≤ 500 entries). `inputmode="search"`, `enterkeyhint="search"`, `autocomplete=off`.
Empty result → empty state 4.13.

### 4.5 Segmented control
Track: bg `--fill`, radius 14, padding 3, height 44 (items ≥ 44 high). Thumb: `--surface`, radius 11, `--sh-1`,
text `--ink` 600; inactive items `--ink-2` 500. Thumb moves with `transform:translateX(calc(var(--i) * 100% * var(--dir)))`
where `--dir:1` (LTR) / `-1` (RTL set on `[dir=rtl]`), transition `--d-slow` `--ease-spring`. Items in DOM order,
which mirrors automatically in RTL (first option on the right). Max 4 items; labels ≤ 12 chars.
`role="radiogroup"`, arrows move, Home/End jump. Disabled opacity .4.

### 4.6 Inputs (text, secret, copy)
Label above (never beside), `--fs-caption` 600 `--ink-2`, margin-bottom 6. Field: height 52, radius 14,
bg `--surface`, 1 px `--hair-strong` border (≥ 3.4:1), padding-inline 14, text `--fs-body`. Focus: 2 px ink ring. Error: border `--err`, helper below with `triangle-alert` 16 px + text `--err`
(never colour alone). Helper `--fs-callout` `--ink-2`. Disabled: bg `--fill`, text `--ink-3`.
- **Secret field** (password, passphrase, backup passphrase): `type=password`; end slot has `eye`/`eye-off`
  toggle (44 × 44, `aria-pressed`, label "Show password"/"Hide password"). When revealed: `--font-mono`
  `--fs-code`; digits bold ink, symbols `--ink-3`, letters ink (weight and tone, not colour).
  Auto-hide after 30 s or on blur of the sheet. `autocomplete=new-password|current-password`, `spellcheck=false`,
  `autocapitalize=off`, `autocorrect=off`. Never put secrets in the URL or in `localStorage`.
- **Copy field** (read-only value with copy): same shell, end slot `copy` button; on success icon → `check`
  1.2 s and toast "Copied". Clipboard: `navigator.clipboard.writeText` (needs secure context — `http://keyra.local`
  is **not** secure, so it will be undefined): implement the `execCommand('copy')` fallback through a hidden
  `<textarea>` selected inside the user gesture. After copy, nothing auto-clears the clipboard (can't); show
  caption once per session: "Clipboard stays until you copy something else."
- **Counter/hint** for min length (passphrase 10, wifi 8–63, backup 12) as helper text only, e.g.
  "At least 10 characters (3 more)".

### 4.7 Password strength meter
4 segments, each 6 px high, radius 3, gap 4, full field width. Computed on-device in JS: `bits = len × log2(pool)`
where pool = 26 (a–z) + 26 (A–Z) + 10 (0–9) + 33 (symbols) for classes present (Arabic letters count as 36 once);
subtract 8 bits per repeated-char run ≥ 3 and 8 for sequences (`abc`, `123`); case-insensitive match against a
50-entry common list (`password`, `123456`, `qwerty`, `keyra1234`, `iloveyou`, …) forces "Weak".
| Bits | Segments filled | Colour | Label EN / AR |
|---|---|---|---|
| < 36 | 1 | `--err` | Weak / ضعيفة |
| 36–59 | 2 | `--warn` | Fair / مقبولة |
| 60–79 | 3 | `--ink` | Good / جيدة |
| ≥ 80 | 4 | `--ok` | Strong / قوية |
Label (caption 600, same colour as the segments — all four pass 4.5:1) sits on the end side; announce changes
with `aria-live="polite"` only when the label changes. Unfilled segments `--ring-track`. Fill animates 200 ms.

### 4.8 Generator (SPEC §9.1: the Type tab's "Generate password" card, and inline in Add/Edit)
Passwords come from the device (`POST /api/generate`, hardware RNG); the browser only mirrors the rules to show
exact entropy at once and to offer only settings the device accepts. Preview card (surface-2, radius 16, padding 16):
password in mono 22 px, coloured characters (4.6), wraps at any character, `dir=ltr`, tap = copy; dims while a new one
loads. Under it: strength meter (4.7) driven by the exact entropy, label "Strong · 118 bits". Settings card: **length**
slider 8–128 (default 20) with a 64 px number field; switches "Lowercase a–z", "Uppercase A–Z", "Numbers 0–9",
"Symbols !@#$" (all on; the last one on cannot be turned off); under Numbers/Symbols a −/+ stepper "Minimum numbers" /
"Minimum symbols" (1 up to the largest value the device accepts); "Avoid look-alikes (0 O o 1 l I |)" (on). Settings
are remembered per browser (`localStorage["keyra.gen"]`, never the password).
- **Generate sheet:** preview, meter, secondary sm "New one" (`refresh-cw`) + "Copy"; primary full "Type it"
  (`keyboard`); tinted "Type twice" + secondary "Save" (`download`); then the settings card. Type it / twice → the Ready
  card (4.11) replaces the sheet body (chip "New password" / "New password · twice"), and the same password is back
  after "Typed". Save → two rows "New account" (opens Add with the password filled in; not via URL or storage) /
  "Update an account" (search + list → alert "Replace the password of “{title}”?" · "The old one stays in its password
  history." → toast "Saved. The old password is in its history." → that account opens).
- **Inline (Add/Edit):** the "Create password" ghost button opens the same preview + settings in a surface-2 panel under
  the password field, footer secondary "New one" + primary "Use this password".
- **Type text…** (SPEC §9.2, the first card of the Type tab): mono textarea (≤ 256, counter), warning
  listing characters Keyra can't type, switch "Type it twice" + segmented Tab | Enter, primary "Type it" → Ready
  (chip "Text"); the text is cleared after "Typed".
- **Password history** (SPEC §9.3): account sheet group under the details, rows "Used until {date}" + masked value,
  eye and copy; footer "Keyra keeps the last 10 passwords."

### 4.9 Sheets & dialogs
- **Phone (< 900): bottom sheet.** Body `--surface` opaque, top radius 28, `--sh-3`, max-height `calc(100dvh - 12px - env(safe-area-inset-top))`,
  padding-bottom `max(16px, env(safe-area-inset-bottom))`. Header: grabber 36 × 4 `--hair-strong` @ .5 at top 8;
  sticky solid header (hairline under it) 56 px (title centred 600; close `x` on the end side; optional primary text button on the
  start side, e.g. "Save"). Open: `translateY(100%)→0`, `--d-sheet` `--ease-spring`; scrim fade 200 ms. Close:
  `--d-base` `--ease-in`. Drag-to-dismiss from grabber/header (threshold 96 px or velocity > 0.5 px/ms), rubber-band
  above. Scrolls internally; background `inert` + `overflow:hidden`; Esc/back button/scrim tap closes (not for
  Ready state, which needs explicit Cancel, and not for unsaved Add/Edit — confirm "Discard changes?").
- **Desktop (≥ 900): centred dialog.** Width 480 (560 for Add/Edit, 640 for Import), radius 28, `--sh-3`, max-height
  86dvh, enter `scale(.96)→1` + opacity 200 ms `--ease-out`, scrim `--scrim`. Same header minus grabber.
  The **account sheet is not a dialog on desktop** — it is the right-hand detail pane (4.3 selected state).
- **Alert dialog** (confirmations: delete, factory reset, replace): always centred, width `min(320px, 100% − 48px)`,
  radius 28, padding 24, centred title (t2) + body (callout `--ink-2`) + stacked full-width 48 px buttons
  (primary action first; destructive uses Danger-confirm; Cancel last, Secondary). Factory reset is confirmed
  by the physical button (Ready component), not by typing a word; the dialog only explains.
- A11y: `role="dialog"`/`alertdialog`, `aria-modal`, labelled by title, focus moves to first control (alert: to
  Cancel), Tab trap, focus returns to the trigger on close.

### 4.10 Toasts
Bottom-centre, 16 px above the safe area (above the FAB: `bottom:calc(96px + env(safe-area-inset-bottom))` when FAB
visible), `--toast-bg` solid, text `--toast-text` 16.58:1, radius pill, padding 12 20, max-width 360, `--sh-2`,
icon 20 + text callout 600, optional action in `--toast-link` (9.55:1). Enter: `translateY(16px)+opacity` 280 ms
spring; exit 160 ms. Duration 2400 ms (action: 5000; error: 5000, with `triangle-alert`). One at a time (replace).
`role="status"` / errors `role="alert"`. Tap or swipe down dismisses. Reduced motion: opacity only.

### 4.11 Signature: the **Ready** state — the Contour as the press indicator
Appears in the account sheet in place of the action area when `state.pending` is non-null; the same component
(`web/src/components/Ready.tsx`, different copy) serves every presence prompt: onboarding step 3, Wi-Fi change,
restore-replace, factory reset, reveal, pairing, trust this browser.

**Anatomy** (card: `--surface` + hairline, radius 28, padding 24 20, centred column, max-width 380):
1. **Countdown ring** 176 px (viewBox 200, r 92): a 1.5 px hairline track and a `--hair-strong` arc that drains
   **clockwise** over `expiresIn` in both directions (a clock is never mirrored); one CSS animation with a negative
   `animation-delay`, re-keyed when a poll re-syncs the deadline. Last 10 s: arc and countdown `--warn`.
2. **The Contour** at 120 px (viewBox `14 14 72 72`, stroke 5) inside the ring. Three paths, `pathLength=100`, CSS only:
   | State | What the line does | Colour |
   |---|---|---|
   | waiting (`press-wait`) | the line's ends draw back and return (`stroke-dasharray 100 0 ↔ 94 6`, offset 0 ↔ −3) while a 7 px "light" in the gap brightens 6 % ↔ 42 % — the LED's dark→bright→dark, **1200 ms** (`--d-breath`) | `--signal` |
   | typing (`press-typing`) | still; the gap light steady (the LED is solid blue) | `--signal` |
   | done (`press-done`) | the gap segment draws shut (dashoffset 100 → 0, 420 ms ease-out) and the line settles from signal to ink (320 ms) | `--ink` |
   | refused / expired / failed (`press-off`, ErrorCard) | the line draws back from the gap to 19–81 % of its length (420 ms); the reason's icon sits inside the shield; no shake | `--err` |
3. **Title** (t2): "Ready — press Keyra's button". **Body** (callout `--ink-2`, ≤ 2 lines).
   **Chip** `.chip-signal` "Password · GitHub" — the only blue chip.
4. **Countdown** in mono 15 px `--ink-2`, `0:42`, `aria-hidden`; an `aria-live="polite"` line announces 30, 10, 5 s.
5. **Cancel** (ghost) and the footer caption "Or hold the button for 2 seconds to cancel."
6. **Receipt** (typed only): "Typed · `USB · 14:02`" — the word in the UI font, where and when in mono LTR, in a small
   paper tag with a hairline; it fades up after the gap closes. Where = the armed action's target (USB or the
   Bluetooth device's name), kept from the last poll before the device cleared it.

**State machine** (UI ← `GET /api/state` poll 1 s while `pending`/`presence.awaiting`):
| State | Trigger | Visual |
|---|---|---|
| `ready` | `pending≠null` | waiting Contour, ring draining, countdown |
| `ready-warning` | `expiresIn ≤ 10 s` | ring + countdown `--warn` (the Contour keeps breathing: the LED does) |
| `typing` | `pending` just became null and Δ < 1.5 s | steady blue Contour, title "Typing…" |
| `typed` | `last.ok && code="typed"` | gap closes, ink, receipt, `role=status` "Typed"; 1.9 s dwell, then the actions return |
| `expired` / `no_usb` / `unsupported_char` / `failed` / `cancelled` | `last.code` | error card (4.14) with the retracted Contour |
`last.at` ≤ 6000 ms counts as "this action's result"; older results are ignored.

**Reduced motion:** no drawing, breathing or retracting; the waiting state stays legible as a static blue line with a
lit gap, done as a closed ink shield, refused as the retracted err line; states switch with a 120 ms fade.

#### 4.11.1 Firmware ↔ UI sync (ask the firmware owner to match)
| UI state | `io::Led` | Pattern |
|---|---|---|
| ready / awaiting presence (setup, Wi-Fi change, restore-replace, factory reset) | `Pending` / `AwaitPresence` | blue (`AwaitPresence`: violet), sinusoidal breathe 10 % ↔ 100 %, period **1200 ms** (`led_anim.cpp`) = `--d-breath` |
| typing | `Typing` | solid blue, 100 % |
| typed | `Success` | green `#22C55E` 100 % for 700 ms then off |
| expired / no_usb / failed | `Error` | red `#EF4444` three blinks, 150 ms on / 150 ms off |
| cancelled | `Idle` | off |
| Bluetooth pairing window (Device → Bluetooth) | `Pairing` | cyan, sinusoidal breathe, period 1200 ms, up to 120 s |
| a website waits for the security-key press (docs/FIDO.md) | `Fido` | white, two 120 ms blinks per second (no breathe, so it never looks like a typing prompt), up to 30 s |
Brightness follows Device → Light. If the firmware differs, change the UI constants `--d-breath` and
colours — the structure stays.

### 4.12 Mini ring (TOTP, countdowns inline)
28 × 28, `r=11 stroke-width=3.5`; same drain logic over `period` (30/60). Colour `--ink`, last 5 s `--warn`.

### 4.13 Empty states
Layout: centred, max-width 320: a 96 px paper disc with a hairline holding the Contour (44 px, ink-2) · title t2 · body
callout `--ink-2` · CTA primary md. No orbiting dots, no illustrations.
| Where | Title / body / CTA |
|---|---|
| Empty vault | "Nothing here yet" / "Add your first account, or import from Apple Passwords, Chrome, Bitwarden or 1Password." / "Add account" + ghost "Import" |
| No search results | "No matches" / "Nothing for “{q}”. Check the spelling or try fewer letters." / ghost "Clear search" |
| No favourites | section simply hidden |

### 4.14 Error & notice patterns
- **Inline notice** (banner inside sheet/screen): radius 16, padding 12 14, bg `--warn-soft`/`--err-soft`, or `info`: `--surface-2` with a hairline and ink-2 text,
  icon 20 + text callout (text colour = matching token ≥ 4.5:1 on its soft bg), optional trailing text button.
- **Connection lost banner** (sticky below top bar, warn): "Lost connection to Keyra. Check that your phone is on
  the Keyra-XXXX Wi-Fi." + spinner "Reconnecting…". Clears itself when `/api/state` answers.
- **Action error card** — same footprint as Ready, ring replaced by a static 176 px circle in `--err-soft`/`--warn-soft`
  with a 56 px icon; title, body, one primary (retry/fix) and one ghost (dismiss). Specific ones:
  | Code | Icon | Tone | Title | Body | Primary / ghost |
  |---|---|---|---|---|---|
  | `no_usb` | `usb` | warn | "Keyra isn't plugged in" | "Plug Keyra into your computer's USB port, then try again." | "Try again" / "Copy instead" |
  | `expired` | `clock` | warn | "That took too long" | "Keyra waited 60 seconds. Tap again when you're at the login field." | "Try again" / "Close" |
  | `unsupported_char` | `triangle-alert` | err | "Keyra can't type this one" | "It has a character outside the standard keyboard (like é or ع). Copy it here, or change the password." | "Copy password" / "Edit account" |
  | `failed` | `triangle-alert` | err | "Couldn't type it" | "The computer didn't accept the keystrokes. Click the login field and try again." | "Try again" / "Close" |
  | `cancelled` | — | none | (no card; toast "Cancelled") | | |
- **Wrong passphrase** (Unlock): field border `--err`, shake 8 px × 3 over 320 ms (reduced motion: none), helper
  "That passphrase isn't right." After the 5th wrong try the 429 `retryAfterMs` shows a live countdown and disables
  the button: "Too many tries. Wait 0:32."
- **Validation** appears on blur or submit, never while typing the first time (except strength meter and counters).

### 4.15 Other primitives
- **Section header:** `--fs-caption` 500 `--ink-3`; Latin uses the 11 px uppercase eyebrow with .06 em tracking, Arabic never (no case). Sticky A–Z letter headers: top = bar height, bg `--bg`.
- **Settings row:** min 52, padding 0 16, label headline-ish 16/17 body, value `--ink-2` at end, `chevron-right` mirrored.
  Group card same as account rows. Footer note under a group: callout `--ink-3`.
- **Switch:** 51 × 31, knob 27; on = `--ink` track with an `--on-ink` knob; off = `--fill-press` track + 1 px `--hair-strong`. Knob travel 20 px toward the
  **end** side when on (mirrors in RTL via logical `inset-inline-start`). `role="switch"`, whole row is the hit target.
- **Slider:** native `input[type=range]` restyled: track 4 px `--fill-press`, filled part `--ink`, thumb 26 px `--knob` with `--sh-2`
  and 1 px `--hair-strong`; native RTL reversal is correct, do not fight it. Step labels not shown; value shown at end.
- **Chip/status pill:** height 28, padding 0 10, radius pill, caption 600. Variants ok / err / neutral / ink (outlined) / **signal** (Ready only: "Password · GitHub" while Keyra waits).
  **Typing-to chip** (Accounts top bar): see §5.4.
- **Progress bar** (import): 4 px, radius 2, ink, determinate; label "42 of 140".
- **Skeleton:** `--fill` blocks radius 8 with a 1.4 s shimmer (`linear-gradient(90deg,transparent,rgba(255,255,255,.5),transparent)` in light,
  `.06` in dark, translating); only shown if loading > 150 ms; reduced motion → static `--fill`.
- **Page transition (push/pop):** incoming `translateX(24px)→0` (mirrored in RTL) + opacity, 280 ms `--ease-out`; outgoing fades. Reduced motion: 120 ms crossfade.
- **Pull to refresh:** none (list is local to the device; refresh icon not needed — list reloads on focus/visibility).

---

## 5. Screens

Conventions: **M** = phone 375–430 px; **D** = desktop ≥ 900 px. Routes are hash-based (`web/src/lib/router.ts`) so Back
works and every screen is deep-linkable. Screen background `--bg`; Welcome, Setup and Unlock add the paper grain (§2.3).

### 5.0 Information architecture
Four destinations, a **bottom tab bar** on phones (glass, 60 px + safe area, icon + label, active = ink 600, inactive
`--ink-3`, no pill) and the **same four as a side rail** from 900 px (92 px, the Contour on top, active = paper tile
with a hairline). Tabs swap in place (`replace`); everything opened from a tab is pushed, so Back closes it.

| Tab | Route | Holds | Sub-screens (pushed) |
|---|---|---|---|
| **Accounts** | `#/` | target chip, search (accounts **and** actions), checklist, favorites / recent / A–Z, FAB | `#/a/:id`, `#/a/:id/edit`, `#/new`, `#/connect` |
| **Type** | `#/typing` | Type text (ink card), Generate, Type test; "Typing to" list (USB + paired devices); speed, between fields, Enter after both, order for "Both" | `#/type`, `#/generate`, `#/typing/test`, `#/typing/order` |
| **Security** | `#/security` | dashboard: password health (reused · weak · old), passkeys count, recovery kit status, latest activity; locking (auto-lock, passphrase, lock on unplug, lock now); showing secrets (reveal protection, passkeys in backups) | `#/security/health`, `/activity`, `/passkeys`, `/recovery`, `/autolock`, `/passphrase` |
| **Device** | `#/device` | identity card (graphite), name; Wi‑Fi (Keyra's Wi‑Fi, home Wi‑Fi, trusted browsers); Bluetooth (on/off, output, connect, paired devices); keyboard (USB computer system, layouts); backup (Back up now · Restore from a backup · Import); apps and tags; light; appearance; about (firmware update with a "Ready" badge when an update is staged, Add to Home Screen, model); erase | `#/backup`, `#/restore`, `#/import`, `#/device/wifi`, `/home`, `/trusted`, `/pair`, `/layout-usb`, `/layout-ble`, `/apps`, `/tags`, `/update`, `/erase` |

A sheet opens over the tab it was opened from (search → Backup keeps Accounts behind it); a deep link shows the sheet's
own tab. With no history (a deep link), closing falls back to that tab.

**Search finds actions** (`web/src/lib/actionIndex.ts`): a query of ≥ 2 characters matches the start of any word of an
action's label in **either** language or of its synonyms ("backup", "النسخ", "firmware", "بلوتوث"); whole-word matches
first; at most 4, shown as "Actions and settings" above the account results.

**Post-setup checklist** (Accounts, above the list): recovery kit (`GET /api/recovery`), first backup
(`settings.lastBackupAt`), a paired Bluetooth device, Add to Home Screen (phones). It waits until all four are known, hides
itself when done, and "×" hides it for good in `localStorage["keyra.checklist"]` (try/catch: blocked storage just keeps
offering it). While it shows, the stale-backup reminder (SPEC §12.5) stays hidden.
Copy: AR is primary and written in warm Modern Standard Arabic; `{x}` = variable; `<bdi>` around Latin names.
Arabic plurals for counts: `n=1` "حساب واحد", `n=2` "حسابان", `3–10` "{n} حسابات", `11+` "{n} حساباً", `0` "لا حسابات".
Language: `navigator.language` starts with `ar` → Arabic, else English; manual switch persists in
`localStorage["keyra.lang"]`; `<html lang dir>` updates live without reload.

### 5.1 Welcome (first run only, `initialized=false`)
**M:** top-end language button (ghost sm). The brand moment: the bare Contour (112 px, ink) **draws itself** once per tab
session (900 ms, from the gap round to the gap; `sessionStorage["keyra.drawn"]`), the wordmark fades up under it, then
the display title, the subtitle, and three steps as a spec list (mono `01 02 03`, hairlines between, no icon tiles).
Primary lg CTA, caption below. **D:** the same column, centred, on the grain.
Interaction: CTA → `#/setup/1`. Not shown again.
| Key | العربية | English |
|---|---|---|
| title | أهلاً بك في Keyra | Welcome to Keyra |
| subtitle | خزنتك الصغيرة التي تكتب كلمات المرور بدلاً عنك. | Your pocket vault that types your passwords for you. |
| step 1 | **وصّله** بمنفذ USB في أي كمبيوتر. | **Plug it** into any computer's USB port. |
| step 2 | **اختر حساباً** من هاتفك. | **Pick an account** on your phone. |
| step 3 | **اضغط الزر** وسيكتب Keyra كلمة المرور. | **Press the button** and Keyra types the password. |
| CTA | ابدأ الإعداد | Get started |
| caption | يستغرق حوالي دقيقة. | Takes about a minute. |

### 5.2 Onboarding (3 steps)
Shared chrome: back chevron (start), step dots (3 × 6 px, active 20 × 6 ink, order follows reading direction), title
t1, body callout `--ink-2`, content, sticky bottom primary lg (above keyboard via `visualViewport`). **D:** 440 px card.

**Step 1 — passphrase.** Two secret fields (passphrase, confirm), strength meter (4.7) under the first, length hint, a
`--warn-soft` notice. Continue enabled when ≥ 10 chars **and** equal. `autofocus` first field; Enter advances.
| Key | العربية | English |
|---|---|---|
| step label | الخطوة 1 من 3 | Step 1 of 3 |
| title | اختر عبارتك الرئيسية | Choose your master passphrase |
| body | هي المفتاح الوحيد لخزنتك. جملة طويلة تسهل تذكّرها أفضل من كلمة معقّدة. | It's the one key to your vault. A long sentence you can remember beats a tricky word. |
| label 1 / 2 | العبارة الرئيسية / أعد كتابتها | Passphrase / Type it again |
| hint | 10 أحرف على الأقل ({n} بعد) | At least 10 characters ({n} more) |
| mismatch | العبارتان غير متطابقتين. | The two don't match. |
| notice | لا يمكن استرجاعها إن نسيتها. الحل الوحيد هو مسح Keyra وبدء خزنة جديدة. | It can't be recovered. If you forget it, the only way out is to erase Keyra and start fresh. |
| CTA | متابعة | Continue |

**Step 2 — Wi-Fi password.** Prefilled secret field (revealed, mono) with a generated value (12 chars from
`ABCDEFGHJKLMNPQRSTUVWXYZabcdefghjkmnpqrstuvwxyz23456789`, shown as `xxxx-xxxx-xxxx` = 14 chars with hyphens), a "Suggest another"
secondary button and copy chip; user may overwrite. Validate 8–63 printable ASCII and ≠ `keyra1234`.
| Key | العربية | English |
|---|---|---|
| step label | الخطوة 2 من 3 | Step 2 of 3 |
| title | كلمة مرور جديدة لشبكة Wi‑Fi | A new Wi‑Fi password |
| body | كلمة المرور الأولى معروفة للجميع. اخترنا لك واحدة قوية، ولك أن تغيّرها. | The factory password is public. We picked a strong one for you — change it if you like. |
| label | كلمة مرور Wi‑Fi | Wi‑Fi password |
| hint | من 8 إلى 63 حرفاً، وليست keyra1234. | 8–63 characters, and not keyra1234. |
| suggest | اقتراح آخر | Suggest another |
| notice | سيطلب هاتفك الاتصال بالشبكة من جديد بهذه الكلمة. | Your phone will need to reconnect with this password. |
| CTA | متابعة | Continue |

**Step 3 — "press the button" live wait.** `POST /api/setup` is sent on entering the step; response `{awaiting:"button", expiresIn}`.
Show the **Ready component** full-width, centred (4.11, copy below), poll `/api/state` each second. Success when
`initialized=true`. Expiry → error card "Try again" re-POSTs. Back is disabled while awaiting (Cancel = abandon).
| Key | العربية | English |
|---|---|---|
| step label | الخطوة 3 من 3 | Step 3 of 3 |
| title | اضغط الزر على Keyra | Press the button on your Keyra |
| body | هكذا نتأكد أنك تحمله بيدك. الضوء الأزرق يومض الآن. | That proves you're holding it. Its light is pulsing blue. |
| expired | انتهت المهلة قبل أن نشعر بالضغطة. | We didn't feel a press in time. |
| retry | حاول مجدداً | Try again |

**Done + reconnect (the AP restarts ~3 s after commit; the phone will drop).** Success check (4.11 moment), then a card:
SSID and new Wi-Fi password (mono, copy chips), a live "Waiting…" row with spinner polling `/api/state` every 1.5 s.
When reachable again: call `/api/unlock` with the in-memory passphrase and enter the list; if the page was reloaded by the OS,
the user simply lands on Unlock. Never persist the passphrase.
| Key | العربية | English |
|---|---|---|
| title | اكتمل الإعداد! | You're all set! |
| body | تغيّرت شبكة Wi‑Fi الآن. اتصل بها بكلمة المرور الجديدة وسنكمل تلقائياً. | Keyra's Wi‑Fi just changed. Join it with the new password and we'll carry on automatically. |
| network / password | الشبكة / كلمة المرور | Network / Password |
| waiting | ننتظر عودتك إلى الشبكة… | Waiting for you to rejoin the network… |

### 5.3 Unlock
**M:** paper grain; the Contour tile 80 px (draws itself once per session); title display; one secret field (autofocus, `autocomplete=current-password`); primary lg "Unlock";
below: ghost "Forgot passphrase?". Language chip top-end. **D:** 440 px centred card. KDF takes ≈1.2 s on the device: button → loading
at once; after 700 ms a caption fades in (explains the wait). Success: sheet-less crossfade 200 ms into the list. Wrong: 4.14.
Forgot → alert dialog → **Ready (presence)** for factory reset (`POST /api/factory-reset`) → on success toast + the app returns to Welcome.
| Key | العربية | English |
|---|---|---|
| title | افتح خزنتك | Unlock your vault |
| label | العبارة الرئيسية | Master passphrase |
| button / loading | فتح / جارٍ الفتح… | Unlock / Unlocking… |
| slow caption | يستغرق هذا لحظة عمداً، لحماية خزنتك. | This takes a moment on purpose — it protects your vault. |
| wrong | هذه العبارة غير صحيحة. | That passphrase isn't right. |
| rate limit | محاولات كثيرة. انتظر {m:ss}. | Too many tries. Wait {m:ss}. |
| forgot | نسيت العبارة؟ | Forgot passphrase? |
| erase title | مسح Keyra؟ | Erase Keyra? |
| erase body | لا يمكن استرجاع العبارة. يمكنك مسح كل شيء وبدء خزنة جديدة؛ تُحذف جميع الحسابات. ولتأكيد أنك تحمل الجهاز، ستضغط زرّه. | Your passphrase can't be recovered. You can erase everything and start a new vault; all accounts are deleted. To prove you're holding the device, you'll press its button. |
| erase confirm / cancel | مسح والبدء من جديد / إلغاء | Erase and start over / Cancel |
| erase waiting title | اضغط الزر لتأكيد المسح | Press the button to confirm |

### 5.4 Vault list (home)
**M** (top → bottom): sticky **glass top bar** 56 px — start: the **"Typing to" chip** (dot: ok when that output is
connected, hollow otherwise · "Typing to" caption · `USB` or the Bluetooth device's name · `chevron-down`); end: Lock (`lock`, 44).
Tapping the chip opens a sheet with the same radio list as the Type tab (USB with its plug state, each paired device with
"Connected"/"Paired") and "Pair a new device" (→ `#/device/pair`). The pick is this browser's (`localStorage["keyra.target"]`,
the same value the account sheet's "Type into" segmented control uses); no new API.
Large title t1 "Accounts" + count callout `--ink-3`. Sticky **search field** (4.4) directly under (glass, same bar group; on scroll the large
title collapses by simply scrolling away). Sections as inset group cards: **Favorites** (all favorites), **Recently used**
(3 most recent non-favorites with `lastUsed>0`), then **All** grouped by first letter with sticky letter headers (script order = UI
language first: Arabic letters then Latin then `#` in AR; reverse in EN; letters of the same base compared after the normalisation of 4.4).
While searching: "Actions and settings" (§5.0) above one flat "Results" group; matches highlighted with a `--fill-press` mark.
Above the list (not searching): the post-setup checklist (§5.0) or, once it is done or hidden, the stale-backup reminder.
**FAB:** 56 px ink squircle (radius 18, `plus` 24 `--on-ink`), end side, 16 px above the tab bar; hides while scrolling down
> 80 px, returns on scroll up. Bottom padding of the list 96 px so the FAB never covers a row.
**D:** left pane 380 px (top bar chips + search + list), right pane = account detail (5.5) or, when nothing selected, a calm
placeholder (glyph 64 `--ink-3`, "Pick an account"). Add → centred dialog. Keyboard: `/` focuses search, ↑↓ moves selection, Enter opens, `n` new.
Interaction: row tap → `#/a/:id` (sheet). List refetches `GET /api/entries` on mount, after any write, and on `visibilitychange`.
Loading: 6 skeleton rows. Error: connection banner. Locked (401): route to Locked.
| Key | العربية | English |
|---|---|---|
| title | الحسابات | Accounts |
| count | plural rules above (e.g. 12 حساباً) | {n} accounts (1 account) |
| search placeholder | ابحث في حساباتك | Search your accounts |
| favorites / recent / all | المفضّلة / استُخدمت مؤخراً / الكل | Favorites / Recently used / All |
| results | النتائج | Results |
| USB on / off | موصول بالكمبيوتر / غير موصول | Plugged in / Not plugged in |
| a11y: add / lock | إضافة حساب / قفل | Add account / Lock |
| tabs | الحسابات · الكتابة · الأمان · الجهاز | Accounts · Type · Security · Device |
| typing-to chip | يكتب في · USB / غير متصل | Typing to · USB / Not connected |
| checklist | أكمل الإعداد · اكتمل {done} من {n} | Finish setting up · {done} of {n} done |
| search actions | الإجراءات والإعدادات | Actions and settings |
| detail placeholder | اختر حساباً لعرضه | Pick an account to open it |

### 5.5 Account sheet (detail)
**M:** bottom sheet 92 dvh max (4.9), grabber; header: monogram 64, title t2 (`dir=auto`, 2 lines max), host callout `--ink-2`; end: star toggle
(`aria-pressed`, 44) and Edit (`pencil`, 44). Then the **action area** (4.2), then an inset group "details": Username (copy), Password (masked
`••••••••••`, eye, copy), Website (copy), Notes (expandable 3 lines). Revealing a password: `GET /api/entries/{id}` is called on sheet open
(secrets live only in component state; cleared on close/lock). **D:** identical content as the right pane, no grabber/scrim.
Interactions: tile tap → `POST /api/type {id,what}` → Ready replaces actions in place. Starting another action while one is pending replaces it
(API does) with no confirmation. Closing the sheet during Ready asks nothing — the pending action stays alive until expiry (60 s), the list shows a
slim glass "Ready · GitHub" pill (tap returns to the sheet). Star toggles `PUT {favorite}` optimistically.
| Key | العربية | English |
|---|---|---|
| both / sub | الاثنان معاً / اسم المستخدم ثم كلمة المرور | Both / Username, then password |
| username / password | اسم المستخدم / كلمة المرور | Username / Password |
| code card | رمز التحقق · يتجدد خلال {n} ث | Verification code · renews in {n}s |
| code action | اكتب | Type |
| helper | يكتبها Keyra بعد أن تضغط زرّه. | Keyra types it after you press its button. |
| no password | لا توجد كلمة مرور محفوظة. | No password saved. |
| no clock | تعذّر حساب الرمز لأن الساعة غير مضبوطة. أعد تحميل الصفحة. | Can't make a code — the clock isn't set. Reload the page. |
| details labels | اسم المستخدم · كلمة المرور · الموقع · ملاحظات | Username · Password · Website · Notes |
| copy / copied | نسخ / تم النسخ | Copy / Copied |
| show / hide | إظهار / إخفاء | Show / Hide |
| edit / favorite | تعديل / مفضّلة | Edit / Favorite |
| **Ready** title | جاهز — اضغط زرّ Keyra | Ready — press Keyra's button |
| ready body | انقر على خانة الدخول في الكمبيوتر، ثم اضغط الزر. | Click the login field on your computer, then press the button. |
| ready chips | كلمة المرور · {title} / الاثنان · {title} / اسم المستخدم · {title} / رمز التحقق · {title} | Password · {title} / Both · {title} / Username · {title} / Code · {title} |
| ready target notice | Keyra غير متصل بعد — وصّله بمنفذ USB أو اربط جهازاً مقترناً عبر البلوتوث. / سيكتب عبر البلوتوث في {name} | Keyra isn't connected yet — plug it in, or connect a paired Bluetooth device. / Types via Bluetooth — {name} |
| ready footer | أو اضغط الزر مطوّلاً ثانيتين للإلغاء. | Or hold the button for 2 seconds to cancel. |
| cancel | إلغاء | Cancel |
| typing | جارٍ الكتابة… | Typing… |
| typed title / body | تمّت الكتابة | Typed |
| typed body | تم. Keyra جاهز للمرة التالية. | Done. Keyra is ready for the next one. |
| ready pill | جاهز · {title} | Ready · {title} |
| cancelled toast | أُلغيت العملية | Cancelled |
(Error cards: copy in 4.14 — add the AR column there: `no_usb` "Keyra غير موصول" / "وصّل Keyra بمنفذ USB في الكمبيوتر ثم حاول مجدداً." · `expired` "استغرق الأمر وقتاً طويلاً" / "انتظر Keyra 60 ثانية. اضغط من جديد حين تصل إلى خانة الدخول." · `unsupported_char` "لا يستطيع Keyra كتابة هذه" / "فيها رمز خارج لوحة المفاتيح القياسية (مثل é أو ع). انسخها من هنا، أو غيّر كلمة المرور." · `failed` "تعذّرت الكتابة" / "لم يقبل الكمبيوتر الضغطات. انقر على خانة الدخول وحاول مجدداً." · buttons "حاول مجدداً / انسخ بدلاً من ذلك / نسخ كلمة المرور / تعديل الحساب / إغلاق".)

### 5.6 Add / Edit account
**M:** bottom sheet full height (92 dvh). Header: close `x` (end), title (centre), "Save" text button (start; disabled until valid). Form, one column,
fields in order: Name (required) · Website · Username · Password (secret + strength + "Create password" ghost sm under it) · 2FA key · Notes
(textarea 4 rows, auto-grow to 8) · switch "Add to favorites". Edit mode adds a Danger button at the very bottom (separated by 32 px).
`autocomplete=off` on every field (Keyra is the manager, not the browser); `enterkeyhint=next` (last field `done`). **D:** centred dialog 560.
Validation: only Name is required; unsupported-character warning is **non-blocking** (4.14 notice under the password field, listing the
offending characters). Dirty + close → "Discard changes?" alert. Save → `POST/PUT`; success: sheet closes, toast "Saved", list re-fetches, row scrolls into view.
Vault `Full` → toast error "Keyra's storage is full." TOTP input accepts base32 (spaces tolerated) or `otpauth://`; invalid → "That doesn't look like a setup key."
Camera/QR scanning is **out of scope** (no secure context over `http://keyra.local`, so `getUserMedia` is unavailable) — helper text tells users to paste.
| Key | العربية | English |
|---|---|---|
| titles | حساب جديد / تعديل الحساب | New account / Edit account |
| name | الاسم — مثال: البريد الإلكتروني | Name — e.g. Email |
| website | الموقع (اختياري) — example.com | Website (optional) — example.com |
| username | اسم المستخدم | Username |
| password | كلمة المرور | Password |
| create pw | إنشاء كلمة مرور | Create password |
| 2FA | مفتاح رمز التحقق (اختياري) | Verification-code key (optional) |
| 2FA helper | الصق مفتاح الإعداد أو رابط otpauth:// الذي يعرضه الموقع. | Paste the setup key or otpauth:// link the website shows. |
| 2FA error | لا يبدو هذا مفتاح إعداد صالحاً. | That doesn't look like a setup key. |
| notes | ملاحظات | Notes |
| fav | أضِف إلى المفضّلة | Add to favorites |
| save | حفظ | Save |
| name required | أعطِ الحساب اسماً. | Give this account a name. |
| unsupported | فيها حرف لا يستطيع Keyra كتابته: «{c}». سيلزمك نسخها يدوياً. | Contains a character Keyra can't type: “{c}”. You'd have to copy it by hand. |
| full | مساحة Keyra ممتلئة. | Keyra's storage is full. |
| delete | حذف الحساب | Delete account |
| delete title / body | حذف «{title}»؟ / لا يمكن التراجع عن هذا. | Delete “{title}”? / This can't be undone. |
| delete confirm / cancel | حذف / إلغاء | Delete / Cancel |
| discard title | تجاهل التغييرات؟ | Discard changes? |
| discard / keep | تجاهل / متابعة التعديل | Discard / Keep editing |
| toasts | تم الحفظ · تم الحذف | Saved · Deleted |
| generator title | إنشاء كلمة مرور | Create password |
| length | الطول | Length |
| classes | أحرف كبيرة A–Z · أحرف صغيرة a–z · أرقام 0–9 · رموز !@#$ | Uppercase A–Z · Lowercase a–z · Numbers 0–9 · Symbols !@#$ |
| look-alikes | تجنّب الأحرف المتشابهة (0 O 1 l I) | Avoid look-alikes (0 O 1 l I) |
| new / use | واحدة جديدة / استخدم هذه | New one / Use this password |

### 5.7 Import
**M:** full-height sheet (via Device → Backup → Import from another app, search "import", or the empty-state ghost button). Step A: four source cards (2 × 2 grid, 96 px high, radius 20,
surface, name 17/600 + tiny file-type caption; Latin product names stay Latin in Arabic). Tap a card → Step B in the same sheet (push): numbered
instructions (≤ 4 lines, steps as callout list), a primary "Choose file" (`<input type=file accept=".csv,text/csv">`). Step C preview: summary card
with counts and a list of the first 5 titles; primary "Import {n}". Step D progress (progress bar, batches of 50 → `POST /api/entries/import`),
then result. **D:** dialog 640. Parsing is 100 % client-side: strip BOM, RFC-4180 quotes/CRLF, auto-detect the source by header names
(case-insensitive): Apple/Chrome `Title|name, URL|url, Username|username, Password|password, Notes|note, OTPAuth`; Bitwarden
`name, login_uri, login_username, login_password, login_totp, notes, favorite, type` (skip rows whose `type≠login`); 1Password
`Title, Url, Username, Password, OTPAuth, Favorite, Notes`. Title falls back to URL host; trim; drop all-empty rows; the device reports
duplicates (same title+username+url) as `skipped`. **ASSUMPTION:** exact export menu names change between app versions — verify when writing copy.
| Key | العربية | English |
|---|---|---|
| title | استيراد الحسابات | Import accounts |
| body | اختر التطبيق الذي تنقل منه. يُقرأ الملف على هاتفك فقط، ولا يغادر شبكة Keyra. | Pick where you're moving from. The file is read on your phone only and never leaves Keyra's network. |
| sources | Apple Passwords · Chrome · Bitwarden · 1Password | Apple Passwords · Chrome · Bitwarden · 1Password |
| Apple steps | 1. على Mac افتح تطبيق Passwords. 2. من القائمة: ملف ← تصدير كل كلمات السر… 3. احفظ ملف CSV. | 1. On your Mac, open Passwords. 2. File ▸ Export All Passwords… 3. Save the CSV file. |
| Chrome steps | 1. افتح chrome://password-manager/settings 2. اضغط «تنزيل الملف» بجانب «تصدير كلمات المرور». | 1. Open chrome://password-manager/settings 2. Next to “Export passwords”, click Download file. |
| Bitwarden steps | 1. في الخزنة على الويب: أدوات ← تصدير الخزنة. 2. اختر الصيغة .csv. | 1. In the web vault: Tools ▸ Export vault. 2. Choose the .csv format. |
| 1Password steps | 1. افتح التطبيق على الحاسوب. 2. ملف ← تصدير ← اختر الحساب ← CSV. | 1. Open the desktop app. 2. File ▸ Export ▸ choose the account ▸ CSV. |
| choose file | اختر الملف | Choose file |
| found | وجدنا {n} حساباً | Found {n} accounts |
| duplicates | سيُتخطّى {d} مكرّراً | {d} duplicates will be skipped |
| no password | {m} بلا كلمة مرور | {m} have no password |
| import | استيراد {n} | Import {n} |
| progress | جارٍ الاستيراد… {a} من {n} | Importing… {a} of {n} |
| done | تم استيراد {a} حساباً | Imported {a} accounts |
| delete file | احذف ملف CSV من هاتفك وحاسوبك الآن؛ فهو يحوي كلمات مرورك دون تشفير. | Delete the CSV from your phone and computer now — it holds your passwords unencrypted. |
| bad file | هذا الملف لا يشبه تصدير كلمات مرور. جرّب مصدراً آخر. | This doesn't look like a password export. Try another source. |

### 5.8 Backup & Restore
**M:** two separate screens from Device → Backup: **Back up now** (`#/backup`) and **Restore from a backup** (`#/restore`),
so the destructive one is never next to the routine one. **Backup:** explanatory callout; secret field (backup passphrase, ≥ 12, strength meter, **separate** from
the master passphrase — helper says so); primary "Download backup" → `POST /api/backup` → browser saves `keyra-backup-YYYYMMDD.json`
(iOS Safari asks "Download?" → Files). Use a `Blob` + `<a download>` click inside the gesture. **Restore:** file picker (`.json`), secret field, segmented
Merge | Replace (default Merge; helper text switches), primary "Restore". Replace → alert → presence Ready → result. Errors: wrong passphrase / damaged file.
| Key | العربية | English |
|---|---|---|
| title | النسخ الاحتياطي والاستعادة | Backup & restore |
| backup head | نسخة احتياطية | Backup |
| backup body | ملف مشفّر بعبارة خاصة به. احتفظ به في مكان آمن. | An encrypted file with its own passphrase. Keep it somewhere safe. |
| backup label | عبارة النسخة (12 حرفاً على الأقل) | Backup passphrase (12+ characters) |
| backup helper | تختلف عن عبارتك الرئيسية، ولا تستطيع Keyra استرجاعها. | Different from your master passphrase, and Keyra can't recover it. |
| backup button / toast | تنزيل النسخة / تم حفظ النسخة | Download backup / Backup saved |
| restore head | استعادة | Restore |
| choose | اختر ملف النسخة | Choose backup file |
| mode | دمج · استبدال | Merge · Replace |
| merge helper | تُضاف الحسابات الجديدة وتُحدَّث المتغيّرة. | New accounts are added; changed ones are updated. |
| replace helper | تُستبدل خزنتك الحالية بالكامل. يلزم ضغط زرّ Keyra. | Your current vault is replaced entirely. Needs a press of Keyra's button. |
| restore button | استعادة | Restore |
| result | أُضيف {a} وحُدِّث {u} | Added {a}, updated {u} |
| wrong | العبارة غير صحيحة أو الملف تالف. | Wrong passphrase, or the file is damaged. |

### 5.9 Settings (now split across Type, Security and Device — §5.0)
There is no Settings screen any more; its groups live in the tab they belong to (table below keeps the copy). Each tab is
a page with a large title; rows carry an icon tile (§4.0); sub-screens are routed sheets. Every
change saves immediately (`PUT /api/settings`, optimistic); no success toast; on error: revert + error toast "Couldn't save. Try again.". Wi-Fi name/password edits happen in a
sub-sheet ("Keyra's Wi‑Fi", Device) with an explicit sticky "Save" because they need presence → Ready → "Keyra's Wi-Fi will restart; rejoin it" (same reconnect card as 5.2).
Auto-lock opens a radio list sheet (1, 5, 15, 30, 60, 120 min). Typing speed segmented maps Slow/Normal/Fast → `keyDelayMs` 30/12/5
(**ASSUMPTION**; default 12 = firmware default). LED brightness slider 10–100 % → `ledBrightness` scaled to the firmware range (**ASSUMPTION** — confirm
range with firmware; UI never offers 0 so the "ready" light can't be turned off).
**Type test** (`{test:true}`): Ready component with the copy below. Language/theme apply instantly.
| Group | Row → control | العربية | English |
|---|---|---|---|
| tab titles | | الكتابة · الأمان · الجهاز | Type · Security · Device |
| Keyra | name (text) | اسم الجهاز | Device name |
| | Wi-Fi (sub-sheet) | شبكة Wi‑Fi | Wi‑Fi network |
| Security | Auto-lock (list) | القفل التلقائي — بعد {n} دقيقة | Auto-lock — after {n} min |
| | Change passphrase (sheet: current/new/confirm) | تغيير العبارة الرئيسية | Change master passphrase |
| | Lock now (button) | اقفل الآن | Lock now |
| Typing | Speed (segmented) | سرعة الكتابة: بطيئة · عادية · سريعة | Typing speed: Slow · Normal · Fast |
| | Between fields (segmented) | بين الخانتين: Tab · Enter | Between fields: Tab · Enter |
| | Submit after both (switch) | اضغط Enter بعد الاثنين | Press Enter after both |
| | Type test (button) | اختبار الكتابة | Type test |
| Bluetooth | On/off (switch) | لوحة مفاتيح بلوتوث | Bluetooth keyboard |
| | Type into (segmented) | الكتابة في: تلقائي · USB · بلوتوث | Type into: Auto · USB · Bluetooth |
| | Connect (segmented, footer explains the iOS on-screen keyboard) | الاتصال: عند الكتابة · دائماً | Connect: When typing · Always |
| Account sheet | Type into (segmented above the actions: USB · each paired device; remembered per browser) | الكتابة في | Type into |
| Ready | while `host.connecting` | جارٍ الاتصال بـ {name}… | Connecting to {name}… |
| | Paired devices (rows: name, "Connected"/"Last used {date}", forget) | متصل · آخر استخدام {date} · إلغاء الإقران | Connected · Last used {date} · Forget |
| | Pair a new device (sheet: Ready for the button → Ready "Now pick “{name}” in your phone or computer’s Bluetooth settings", 120 s ring → "Paired") | إقران جهاز جديد | Pair a new device |
| Light | LED brightness (slider) | سطوع ضوء Keyra | Keyra light brightness |
| Appearance | Language (segmented) | اللغة: تلقائي · العربية · English | Language: Auto · العربية · English |
| | Theme (segmented) | المظهر: تلقائي · فاتح · داكن | Appearance: Auto · Light · Dark |
| Data | Import / Backup | استيراد · نسخ احتياطي واستعادة | Import · Backup & restore |
| About | Home Screen / version / model | أضِف إلى الشاشة الرئيسية · الإصدار · الطراز | Add to Home Screen · Version · Model |
| Danger | Erase (danger row, `--err` text) | مسح Keyra وإعادة الضبط… | Erase Keyra and reset… |
| footnote speed | | إن أسقط الكمبيوتر بعض الأحرف فاختر «بطيئة». | If the computer drops characters, choose Slow. |
| footnote submit | | يسجّل الدخول فور كتابة الاثنين. | Signs you in right after typing both. |
| type test body | | انقر على أي خانة نصية في الكمبيوتر ثم اضغط زرّ Keyra؛ سيكتب سطراً قصيراً للتجربة. | Click any text field on your computer, then press Keyra's button. It will type a short test line. |
| passphrase changed | | تم تغيير العبارة. | Passphrase changed. |
| erase body | | تُحذف كل الحسابات والإعدادات من Keyra نهائياً ولا يمكن التراجع. تأكّد من وجود نسخة احتياطية. | All accounts and settings are permanently erased from Keyra. This can't be undone. Make sure you have a backup. |
| erase done | | سيبدأ Keyra من جديد. | Keyra is starting fresh. |
| save error | | تعذّر الحفظ. حاول مجدداً. | Couldn't save. Try again. |

### 5.10 Add-to-Home-Screen hint
Shown **once**, 1.2 s after the first successful unlock, only if not standalone (`navigator.standalone` false and
`matchMedia('(display-mode: standalone)')` false) and not dismissed. Bottom sheet (compact, no scrim blocking the list). iOS (UA `iPhone|iPad`): 3 numbered steps with the real
`share` icon rendered inline; others: 2 steps. "Not now" hides it for 7 days (`localStorage["keyra.a2hs"]=timestamp`); "Got it" hides forever. Also reachable from Device → About and the Accounts checklist.
**D:** never shown.
| Key | العربية | English |
|---|---|---|
| title | أضِف Keyra إلى الشاشة الرئيسية | Add Keyra to your Home Screen |
| body | افتحه بلمسة واحدة وبملء الشاشة، كأي تطبيق. | Open it in one tap, full screen, like any app. |
| iOS 1 / 2 / 3 | اضغط زرّ المشاركة [share] · اختر «إضافة إلى الشاشة الرئيسية» · اضغط «إضافة» | Tap the Share button [share] · Choose “Add to Home Screen” · Tap Add |
| Android 1 / 2 | افتح قائمة المتصفح ⋮ · اختر «إضافة إلى الشاشة الرئيسية» | Open the browser menu ⋮ · Choose “Add to Home screen” |
| buttons | فهمت / ليس الآن | Got it / Not now |

### 5.11 Locked
Reached by auto-lock, long press on the button, session expiry (401) or manual Lock. Same layout as Unlock but calmer: no grain, the bare Contour 72 px
`--ink-3`, title, reason line (callout `--ink-2`), primary "Unlock" which reveals the passphrase field inline (field + button) with focus.
In-flight Ready/forms are discarded; Add/Edit drafts are **not** saved anywhere (secrets never touch storage).
| Key | العربية | English |
|---|---|---|
| title | الخزنة مقفلة | Vault locked |
| reason idle | قُفلت بعد {n} دقيقة من عدم النشاط. | Locked after {n} minutes of inactivity. |
| reason button | قُفلت بزرّ Keyra. | Locked with Keyra's button. |
| reason manual | قفلتَ الخزنة. | You locked the vault. |
| reason session | انتهت الجلسة. افتح الخزنة من جديد. | Your session ended. Unlock again. |
| button | فتح | Unlock |

### 5.12 Global states
| State | Visual | AR | EN |
|---|---|---|---|
| Offline / unreachable | sticky warn banner (4.14) | انقطع الاتصال بـ Keyra. تأكد أن هاتفك على شبكة Keyra‑XXXX. | Lost connection to Keyra. Check your phone is on the Keyra‑XXXX Wi‑Fi. |
| Reconnecting | spinner in banner | نعيد الاتصال… | Reconnecting… |
| Device clock | none (silent; header `X-Keyra-Time`) | — | — |
| Generic error toast | 4.10 | حدث خطأ. حاول مجدداً. | Something went wrong. Try again. |

---

## 6. Accessibility

- **Contrast:** all text tokens ≥ 4.5:1 on their allowed backgrounds (tables in §2.1); UI boundaries (inputs, switch off-track, slider thumb) ≥ 3:1.
  Never convey state by colour alone (strength = label; errors = icon + text; ring warning = countdown text + colour).
- **Focus:** visible 2 px ring + 2 px offset on every focusable (`--focus` = ink: 16.9 : 1 light / 17.5 : 1 dark); order = DOM order = visual order in both directions;
  sheets/dialogs trap focus, set `inert` on the rest, restore focus on close; skip link "Skip to accounts" on desktop.
- **Targets:** ≥ 44 × 44 CSS px for every interactive element (visual smaller icons get padded hit areas); ≥ 8 px between adjacent targets.
- **Semantics:** list = `ul/li` with each row a single `<button>` (or `<a href="#/a/id">`); sheets `role=dialog aria-modal aria-labelledby`; switch `role=switch`;
  segmented `role=radiogroup`; progress `role=progressbar` with `aria-valuenow`; the Ready state `role=group aria-labelledby=title`, with the sr-only
  live line at 30/10/5 s and an assertive `role=status` for Typed/errors; toasts per 4.10; page title (`document.title`) updates per route; `lang` set on `<html>` and on
  inline foreign-script runs.
- **Reduced motion** (`@media (prefers-reduced-motion:reduce)` and an in-app override is **not** needed): every duration → 120 ms linear, `--d-draw` → 0; no translate/scale/shake/draw/breath. The Contour stays legible without
  motion (static blue line with a lit gap while waiting, closed ink when typed, retracted err when refused); the countdown arc is static and the
  time stays as text; the Welcome/Unlock draw-in is skipped; skeleton shimmer off; the "Ready" pill's dot is steady. Information-bearing motion (the countdown value) is kept as text.
- **Reduced transparency / high contrast:** `.glass` → solid and the paper grain off (2.3). `prefers-contrast:more`: `--hair` becomes `--hair-strong`, `--ink-3` → `--ink-2`.
- **Zoom/Dynamic Type:** root font-size stays `100%`/`106.25%` (never px on `html`); layouts verified at 200 % text and 320 px width without horizontal scroll;
  sheet content scrolls, buttons wrap labels (min-height, not height).
- **Touch & input:** `inputmode`, `enterkeyhint`, `autocomplete` per field; don't block paste; `autocapitalize=off` on credentials; no timeouts shorter than 60 s except the
  intentional pending-action expiry (announced, extendable by tapping again).
- **RTL rules.** Use only logical properties (`margin-inline`, `padding-inline`, `inset-inline`, `text-align:start`, `border-start-start-radius`; `float`/`left`/`right` banned);
  `<html dir>` drives everything. **Mirror:** layout order, chevrons/back arrows, slide transitions, segmented order, step-dot order, switch/slider direction (native), sheet
  header actions (Save on the start side stays "start"), `translateX` animations (multiply by `--dir`). **Never mirror:** the Contour (mark, tile, press indicator), check/clock/ring (the countdown is
  clockwise in both), digits and anything `dir=ltr` (passwords, usernames, URLs, SSIDs, codes, version numbers), the `share` icon, phone/USB metaphors, images.
  Numbers inside Arabic sentences stay Western digits. A static test: render every screen at 390 px with `dir=rtl` and `dir=ltr`; the pixel-mirror of one must equal the other
  except the never-mirror items.
- **Testing checklist:** keyboard-only pass of every flow; VoiceOver iOS (Arabic + English); TalkBack; `prefers-reduced-motion`, forced dark, 200 % zoom; Lighthouse a11y ≥ 95.

---

## 7. Imagery

Brand imagery follows BRAND.md §6 (real photographs of the board or labelled renders, graphite studio light, the LED as
the only colour; no neon, no blue gradients, no padlocks). The asset list (README banner, hero photo, press video, LED
states strip, screen mosaics, social card) and its sizes are in `docs/research/design-references-2026.md` §4.5. The
earlier ChatGPT prompts for a blue key icon are retired with the old mark.

---

## 8. References (consulted 2026-10-05)

Design language & materials
1. Create with Swift — Liquid Glass: Hierarchy, Harmony, Consistency — https://www.createwithswift.com/liquid-glass-redefining-design-through-hierarchy-harmony-and-consistency/
2. Apple Liquid Glass in iOS 26 (principles, adoption) — https://www.theusefulapps.com/news/exploring-apple-liquid-glass-ui-ios26
3. Infinum — "Liquid Glass: sleek, shiny and questionably accessible" — https://infinum.com/blog/apples-ios-26-liquid-glass-sleek-shiny-and-questionably-accessible/
4. Gulf News — iOS 26.1 "Tinted" control tones down Liquid Glass — https://gulfnews.com/technology/companies/apple-yields-tinted-control-in-ios-261-beta-4-tones-down-liquid-glass-after-backlash-1.500315176
5. tubik studio — 7 UI design trends of 2026 (purposeful motion, anti-glass) — https://tubikstudio.com/blog/ui-design-trends-2026/
6. Figma — Web design trends 2026 — https://www.figma.com/resource-library/web-design-trends/
7. Material 3 Expressive deep dive (Android Authority) — https://www.androidauthority.com/google-material-3-expressive-features-changes-availability-supported-devices-3556392/
8. Material Design — Motion (springs) — https://m3.material.io/styles/motion/overview/how-it-works
9. Josh W. Comeau — Springs and bounces in native CSS (`linear()`) — https://www.joshwcomeau.com/animation/linear-timing-function/
10. DEV — Liquid Glass on the web: 6 ways (backdrop-filter cost, fallbacks) — https://dev.to/devyatov/liquid-glass-on-the-web-6-ways-to-build-it-with-css-and-svg-3m07
11. 21st.dev — Liquid Glass in React: the look, the CSS and the bill — https://21st.dev/blog/liquid-glass-react-components

Password managers & product references
12. MacRumors — iOS 18 Passwords app guide — https://www.macrumors.com/guide/ios-18-passwords/
13. Apple Support — Use the Passwords app — https://support.apple.com/en-gb/120758
14. Bitwarden — August 2026 spotlight: new look — https://bitwarden.com/resources/august-2026-spotlight-bitwarden-is-getting-a-new-look/
15. Proton Pass spring/summer 2026 roadmap — https://proton.me/blog/pass-roadmap-spring-summer-2026
16. Engadget — 1Password 8 mobile redesign — https://www.engadget.com/1-password-8-android-ios-release-date-161538067.html
17. Linear design system breakdown — https://www.shadcn.io/design/linear · Vercel Geist — https://www.designsystems.one/design-systems/vercel-geist · Stripe/Linear/Vercel premium UI — https://mantlr.com/blog/stripe-linear-vercel-premium-ui
18. Nothing OS / Glyph (hardware ↔ UI light language) — https://www.techtimes.com/articles/313989/20260213/nothing-phone-features-nothing-os-design-how-this-android-smartphone-trends-ahead-competition.htm · https://design-milk.com/the-nothing-phone-3s-glyph-matrix-turns-notifications-into-pixel-art/
19. Revolut app motion study — https://60fps.design/apps/revolut
20. Arc → Dia (calm UI vs AI-first) — https://www.sigmabrowser.com/blog/dia-vs-arc-browser-in-2026-ai-workspaces-switching

Arabic / RTL
21. Voxire — Arabic RTL typography for web design 2026 (size +10–15 %, line-height ≥ 1.7, no letter-spacing) — https://voxire.com/blog/arabic-rtl-typography-web-design-2026/
22. Apple WWDC22 — Design for Arabic — https://developer.apple.com/videos/play/wwdc2022/110441/
23. Apple WWDC22 — Get it right (to left) — https://developer.apple.com/videos/play/wwdc2022/10107/
24. Readex Pro (Google Fonts, OFL; font used, subset measured at 29.5 KB) — https://fonts.google.com/specimen/Readex+Pro · source file https://github.com/google/fonts/tree/main/ofl/readexpro
25. Lucide icons (ISC) — https://lucide.dev

---

## Appendix A — Byte budget (gzip -9 bytes over the air; `web/scripts/postbuild.mjs` enforces both)

| Budget | Limit | Measured (0.3.1 → 0.4.3 → Graphite & Ivory redesign) | What it means |
|---|---|---|---|
| **Initial load** | **≤ 130 KB** (133,120 B) | 189.5 KB → 110.1 KB → **116.7 KB** | Everything a phone downloads to open the app, unlock and see the vault list: `index.html`, the entry script and the chunks it imports statically, the stylesheet and the preloaded font. |
| **Total** | **≤ 400 KB** (409,600 B) | 189.5 KB → 205.4 KB → **214.7 KB** | `index.html.gz` plus every file in `assets.pack`: what the firmware embeds for the web app (the PNG icons, manifest and tap page are extra, about 20 KB). |

Initial load, measured (after the redesign):
| Item | KB |
|---|---|
| Shared chunk (Preact, i18n AR+EN, API client, store, UI kit, Unlock, Accounts tab, account view, action index) | 49.6 |
| Entry chunk (app shell, tab bar/rail, router, windowed list, Ready/Contour) | 24.6 |
| CSS (tokens twice-themed, components, screens) | 12.8 |
| Font `keyra-sans.woff2` (stored, not gzipped) | 28.8 |
| `index.html` + loader runtime | 1.0 |
| **Total** | **116.7** |

Loaded on first use, then kept by the browser (hashed name, `Cache-Control: immutable`): onboarding (Setup),
add/edit, import, backup/restore, the Type, Security and Device tabs and each of their sheets (home Wi‑Fi, passkeys, password health, activity,
firmware update, trusted browsers, apps/tokens, NFC tags, keyboard layouts and Layout Doctor, typing order,
recovery kit with its QR encoder), the generator sheet, type text, pairing, the QR reader (`jsQR`, 45 KB) and
the Shamir library (2 KB). A load that fails says so with a retry; when Keyra answers but the file still will
not load (an old tab after a firmware update), the retry reloads the app, which keeps the session and the route.

Rules: a screen that the list or an account does not need goes behind `lazy()` (`web/src/components/Lazy.tsx`);
the initial budget is for the vault list, not a second font, a charting lib, an animation lib or a CSV lib.
History: 150 KB (one file), 170 KB (recovery kit, SPEC §12.2), 190 KB (health, activity, update, SPEC §13–§15),
then split into the two budgets above when the single file reached 189.5 KB.

## Appendix B — Assumptions the lead must confirm (flagged `ASSUMPTION` above)
1. **LED patterns** (4.11.1): Pending = blue breathe 1200 ms period (confirmed in `led_anim.cpp`); Success green 700 ms; Error red 3 blinks. Tell the firmware owner or change `--d-breath`.
2. **Typing speed** presets map to `keyDelayMs` 30 / 12 / 5; default 12 (firmware default).
3. **`ledBrightness`** range is unspecified in SPEC §5 — UI offers 10–100 % and scales to the firmware range; never 0.
4. **Presence-gated settings** (Wi-Fi change, restore-replace, factory reset) reuse the Ready component; the UI relies on `state.presence.{awaiting,op,expiresIn}`.
5. **Clipboard** is unavailable on `http://` (insecure context) — the `execCommand('copy')` fallback in 4.6 is mandatory, not optional.
6. **Import export-menu names** (5.7) were written from memory of current app versions; verify against the real apps before release.
7. **Countdown source of truth** = `expiresIn` from the API (60 s); the UI never assumes 60 when the API says otherwise.
