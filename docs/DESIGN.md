# Keyra — Design System & Screen Spec (v1)

Audience: the frontend engineer (Preact + TypeScript + plain CSS, one inlined file, ≈150 KB gzip total
including fonts) and the owner (image prompts in §7). Companion to `SPEC.md` (§1 is the experience).
Every number below is final unless marked `ASSUMPTION`. Contrast ratios were **computed** (WCAG 2.x
relative luminance), not eyeballed. Font size was **measured** by actually subsetting the font.

Contents: 0 Direction · 1 Brand · 2 Tokens · 3 Typography · 4 Components · 5 Screens · 6 Accessibility ·
7 Image prompts · 8 References

---

## 0. Direction — "Quiet Glass"

One direction: **Apple-Passwords simplicity, iOS 26 structure, glass only where it earns its place, one
electric-blue accent that is literally the device's LED.**

1. **Ease = Apple Passwords.** Grouped inset lists, big type, one obvious next action per screen, sheets
   for detail. Users already know this grammar; nothing to learn.
2. **Glass is chrome, never content.** iOS 26 Liquid Glass drew heavy criticism for legibility (contrast
   measured as low as 1.5:1) and Apple itself added Tinted / Reduce Transparency. 2026 design press calls
   it: "if an effect competes with the content, it dies." So: glass (backdrop blur) only on floating layers
   (top bar + search, sheet header, the "Ready" pill over the list); every row and every secret sits on an opaque surface;
   every glass token has a solid fallback and a computed worst-case contrast.
3. **One hue, owned.** Keyra Blue `#0B57F0` (light) / `#6C9CFF` (dark) equals the physical LED colour. The
   pulsing ring in the app and the pulsing LED on the board are the same 1.6 s breath, so the screen and
   the object feel like one product (Nothing's Glyph idea; Linear owns purple, Raycast owns red — Keyra owns
   LED-blue).
4. **Purposeful spring motion.** Springs (Material 3 Expressive, Apple) via CSS `linear()`; every animation
   states a fact ("ready", "typed", "expired"), nothing decorative; full reduced-motion path.
5. **Arabic-first craft within 150 KB.** One embedded geometric Arabic+Latin variable font (Readex Pro,
   OFL, 29.5 KB measured), logical CSS only, Arabic sized 6 % up with 1.7 line-height, Western digits
   everywhere so on-screen codes match what Keyra types. No icon font, no image assets, no animation lib.

---

## 1. Brand

### 1.1 Name treatment
- Latin: **Keyra** — Readex Pro 600, tracking −0.02 em, capital K only. Never all-caps, never "KEYRA".
- Arabic: **كيرا** — Readex Pro 600, no tracking, no tashkeel.
- Lockup (mark + wordmark): mark height = cap-height × 2.2; gap = 0.5 × mark width. Mark on the start side
  (right in RTL) — the only place the logo "follows" direction; the mark itself never mirrors.
- Voice in UI: calm, brief, second person, warm. No exclamation marks except onboarding completion.
  Technical words only when needed ("USB", "Wi-Fi"). Never blame the user.

### 1.2 Personality (3 words, each with a rule)
- **Calm** — one accent, lots of air, no gradients on content surfaces.
- **Tangible** — the UI points at the object: "press Keyra's button", ring = LED.
- **Precise** — monospace for secrets, tabular rhythm, exact radii; nothing approximate.

### 1.3 Logo mark (exact SVG, 332 bytes)
Concept: a **key whose bow is a ring**. The ring is the brand motif — it becomes the countdown ring, the
LED pulse and the progress ring. Geometry on a 64-grid: bow circle r 9 at (32,22); shaft x=32 from y 31 to
50; two teeth. Stroke 6, round caps. Glyph bounding box 20–44 × 10–53 (optically centred in the tile).

```svg
<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 64 64" width="64" height="64"><rect width="64" height="64" rx="16" fill="#0B57F0"/><g fill="none" stroke="#fff" stroke-width="6" stroke-linecap="round" stroke-linejoin="round"><circle cx="32" cy="22" r="9"/><path d="M32 31v19M32 41h9M32 50h6"/></g></svg>
```
- **Glyph-only** (in-app, 20–72 px, inherits colour): same `<g>` with `stroke="currentColor"`, viewBox
  `0 0 64 64`, no `<rect>`. Used as the key glyph in Ready state, empty states, locked screen.
- Tile corner radius = 25 % of side (16/64) — iOS squircle approximation; exports are full-bleed squares
  (the OS masks them).
- Clear space = 0.25 × tile side. Min size 20 px.
- Colour: tile `#0B57F0`, glyph `#FFFFFF` (5.78:1). Dark variant: tile `#080A10`, glyph `#6C9CFF`.

### 1.4 App icon concept (1024 × 1024, full-bleed, no pre-rounded corners)
Background: linear gradient 160° from `#4B86FF` (top-left) to `#0A47D8` (bottom-right). Glyph: the white key
above, height 52 % of the canvas (≈ 530 px), optically centred (shift up 10 px), stroke weight 11.5 % of
glyph height, soft drop shadow `rgba(0,20,90,.35)` 24 px blur, y+14. Faint top inner highlight: white
14 % → 0 over the top 40 %. No text. Optional dark icon (iOS 18+): `#080A10` field, glyph `#6C9CFF`, 8 % blue
glow behind the bow. Ship `icon-192.png`, `icon-512.png`, `apple-touch-icon.png` (180) from the owner's
generated art (§7a); until then, rasterise the SVG above.

### 1.5 PWA/meta
`theme-color`: `#F3F4F8` (light) / `#080A10` (dark) via two `<meta … media="(prefers-color-scheme:…)">`.
Manifest: `name:"Keyra"`, `short_name:"Keyra"`, `display:"standalone"`, `background_color:"#F3F4F8"`,
`theme_color:"#F3F4F8"`, `start_url:"/"`. `apple-mobile-web-app-capable=yes`,
`apple-mobile-web-app-status-bar-style=default`, `apple-mobile-web-app-title=Keyra`. Viewport:
`width=device-width,initial-scale=1,viewport-fit=cover`.

---

## 2. Tokens

Rules: only logical properties; colours only via tokens; no raw hex in components. Theme resolution:
`data-theme` attribute on `<html>` = `auto | light | dark` (persist in `localStorage["keyra.theme"]`, default
`auto`). `auto` follows `prefers-color-scheme`. **Do not use `light-dark()`** (breaks iOS < 17.5; a broken
token set would blank the whole UI). The dark block is therefore written twice (media + attribute) — generate
it from one object in a tiny Vite plugin or just paste; gzip makes the duplicate cost ≈ 0.3 KB.

### 2.1 Colour tokens — contrast table (computed)

Text pairs (all ≥ 4.5:1). "On" = the token is used as text colour on that background.

| Token (light) | Hex | On `--surface` #FFF | On `--bg` #F3F4F8 | On `--fill` #ECEEF4 |
|---|---|---|---|---|
| `--text` | `#0B0F19` | 19.15 | 17.42 | 16.51 |
| `--text-2` | `#454C5E` | 8.58 | 7.80 | 7.39 |
| `--text-3` (placeholder, captions) | `#5E6679` | 5.75 | 5.23 | 4.96 |
| `--accent` (links, tinted btn text) | `#0B57F0` | 5.78 | 5.26 | — |
| `--ok` | `#0A7A3D` | 5.43 | 4.94 | — |
| `--warn` | `#8F5200` | 6.22 | 5.66 | — |
| `--err` | `#C1251C` | 5.92 | 5.38 | — |

Other light pairs: white on `--accent` 5.78 · white on `--accent-press` `#0847C4` 7.75 · white on danger fill
`#C1251C` 5.92 · `--accent` on `--accent-soft` `#E7EEFE` 4.97 · `--ok` on `#E3F4EA` 4.76 · `--warn` on `#FDF0DC`
5.53 · `--err` on `#FDE8E6` 5.03 · border `--line-strong` `#7C8498` on surface 3.74 / on bg 3.41 (≥ 3:1 for
input boundaries) · focus ring `#0B57F0` on bg 5.26.

| Token (dark) | Hex | On `--surface` #12151D | On `--bg` #080A10 | On `--surface-2` #1B1F2A | On `--fill` #232837 |
|---|---|---|---|---|---|
| `--text` | `#F2F4F9` | 16.58 | 17.98 | 14.96 | 13.34 |
| `--text-2` | `#B1B8C8` | 9.18 | 9.95 | 8.28 | 7.38 |
| `--text-3` | `#8D95A8` | 6.08 | 6.59 | 5.48 | 4.89 |
| `--accent` | `#6C9CFF` | 6.81 | 7.38 | — | — |
| `--ok` | `#4ADE80` | 10.47 | 11.36 | — | — |
| `--warn` | `#FBBF4A` | 11.00 | 11.92 | — | — |
| `--err` | `#FF7A70` | 7.19 | 7.79 | — | — |

Other dark pairs: `--on-accent` `#050A18` on `#6C9CFF` 7.37 · white on danger fill `#D92D20` 4.83 · accent on
`#202B41` 5.28 · ok on `#14301F` 8.19 · warn on `#33260F` 8.88 · err on `#3A1815` 6.26 · `--line-strong`
`#6B7388` on surface 3.85 / bg 4.18 / surface-2 3.47 · focus ring `#9DBBFF` on surface 9.55.

**Glass worst case** (glass over pure black in light / pure white in dark — theoretical floor): light glass
`rgba(255,255,255,.82)` → `#D1D1D1`: `--text` 12.54, `--text-2` 5.62, `--text-3` **3.76 (fails)**; dark glass
`rgba(18,21,29,.84)` → `#383A41`: `--text` 10.32, `--text-2` 5.71, `--text-3` **3.78 (fails)**.
**Rule: on glass use only `--text` and `--text-2`; never `--text-3`, never `--accent` text smaller than 17 px bold.**

### 2.2 Paste-ready CSS

```css
:root{
  /* colour — light */
  --bg:#F3F4F8; --surface:#FFFFFF; --surface-2:#F7F8FB; --fill:#ECEEF4; --fill-press:#E2E5EE;
  --text:#0B0F19; --text-2:#454C5E; --text-3:#5E6679;
  --line:rgba(11,15,25,.10); --line-strong:#7C8498;
  --accent:#0B57F0; --accent-press:#0847C4; --accent-soft:#E7EEFE; --on-accent:#FFFFFF;
  --ok:#0A7A3D; --ok-soft:#E3F4EA; --warn:#8F5200; --warn-soft:#FDF0DC; --err:#C1251C; --err-soft:#FDE8E6;
  --danger-fill:#C1251C; --focus:#0B57F0;
  --pw-digit:#0B57F0; --pw-symbol:#C1251C;            /* coloured characters in revealed passwords */
  --ring-track:#DCE1EC; --led:#2F6BFF;                 /* --led is decorative only (halo), never text */
  --toast-bg:#12151D; --toast-text:#F2F4F9; --toast-link:#9DBBFF;   /* same in both themes */
  --scrim:rgba(8,10,16,.40);
  /* materials (glass) */
  --glass-bg:rgba(255,255,255,.82); --glass-edge:rgba(255,255,255,.70); --glass-solid:#FBFBFD;
  --glass-blur:20px; --glass-sat:1.6;
  /* shadows */
  --sh-1:0 1px 2px rgba(11,15,25,.06),0 0 0 1px rgba(11,15,25,.04);
  --sh-2:0 6px 20px rgba(11,15,25,.08),0 1px 3px rgba(11,15,25,.06);
  --sh-3:0 -10px 48px rgba(11,15,25,.18),0 2px 8px rgba(11,15,25,.08);   /* sheets/dialogs */
  --sh-glow:0 0 0 6px rgba(11,87,240,.14);                                /* focus/active halo */
  /* radii (concentric: inner = outer − padding) */
  --r-xs:8px; --r-sm:12px; --r-md:16px; --r-lg:20px; --r-xl:28px; --r-pill:999px;
  /* spacing (4-pt) */
  --s-0:0; --s-1:4px; --s-2:8px; --s-3:12px; --s-4:16px; --s-5:20px; --s-6:24px; --s-8:32px; --s-10:40px;
  --s-14:56px; --s-18:72px;
  --gutter:16px; --tap:44px; --bar-h:56px;
  /* motion */
  --d-fast:120ms; --d-base:200ms; --d-slow:320ms; --d-sheet:420ms; --d-breath:1600ms;
  --ease-out:cubic-bezier(.2,.8,.2,1); --ease-in:cubic-bezier(.4,0,1,1); --ease-io:cubic-bezier(.4,0,.2,1);
  --ease-spring:cubic-bezier(.34,1.4,.5,1);      /* fallback for browsers without linear() */
  /* z-layers */
  --z-base:0; --z-sticky:10; --z-fab:20; --z-scrim:40; --z-sheet:50; --z-toast:60; --z-lock:100;
  /* type — see §3 */
  --font:"Keyra Sans",ui-sans-serif,system-ui,-apple-system,"Segoe UI","Noto Sans Arabic",Roboto,sans-serif;
  --font-mono:ui-monospace,"SF Mono",SFMono-Regular,Menlo,Consolas,"Roboto Mono",monospace;
  color-scheme:light;
}
@supports (transition-timing-function:linear(0,1)){
  :root{--ease-spring:linear(0,.062,.206,.383,.56,.717,.844,.938,1.002,1.04,1.059,1.063,1.059,1.049,1.038,1.027,1.017,1.009,1.003,1,.997,.996,1)}
}   /* damped spring ζ≈0.66, 6.3 % overshoot; use with duration ≥ 480ms */

/* dark.css body (below) is emitted twice — by a build step or by pasting:
   1) @media (prefers-color-scheme:dark){ :root:not([data-theme="light"]){ <body> } }
   2) :root[data-theme="dark"]{ <body> }                                                      */
```
```css
/* <body> — dark */
  color-scheme:dark;
  --bg:#080A10; --surface:#12151D; --surface-2:#1B1F2A; --fill:#232837; --fill-press:#2C3243;
  --text:#F2F4F9; --text-2:#B1B8C8; --text-3:#8D95A8;
  --line:rgba(255,255,255,.09); --line-strong:#6B7388;
  --accent:#6C9CFF; --accent-press:#8DB1FF; --accent-soft:#202B41; --on-accent:#050A18;
  --ok:#4ADE80; --ok-soft:#14301F; --warn:#FBBF4A; --warn-soft:#33260F; --err:#FF7A70; --err-soft:#3A1815;
  --danger-fill:#D92D20; --focus:#9DBBFF;
  --pw-digit:#6C9CFF; --pw-symbol:#FF7A70; --ring-track:#2A3042;
  --scrim:rgba(0,0,0,.58);
  --glass-bg:rgba(18,21,29,.84); --glass-edge:rgba(255,255,255,.10); --glass-solid:#1B1F2A;
  --sh-1:0 0 0 1px rgba(255,255,255,.06);
  --sh-2:0 8px 24px rgba(0,0,0,.45),0 0 0 1px rgba(255,255,255,.06);
  --sh-3:0 -12px 56px rgba(0,0,0,.65),0 0 0 1px rgba(255,255,255,.08);
  --sh-glow:0 0 0 6px rgba(108,156,255,.18);
```

### 2.3 Materials

| Material | Where (and only here) | Recipe |
|---|---|---|
| **Opaque** | rows, cards, inputs, sheet body, dialogs, lists | `background:var(--surface)` |
| **Glass bar** | sticky top bar + search group, sheet header, "Ready · title" pill over the list (things that sit over scrolling content) | see below |
| **Toast** | toasts | solid `--toast-bg` (never glass) |
| **Scrim** | behind sheets/dialogs | `var(--scrim)`, fade 200 ms |

```css
.glass{background:var(--glass-solid);}                              /* default = solid fallback */
@supports ((backdrop-filter:blur(1px)) or (-webkit-backdrop-filter:blur(1px))){
  .glass{background:var(--glass-bg);
    -webkit-backdrop-filter:blur(var(--glass-blur)) saturate(var(--glass-sat));
            backdrop-filter:blur(var(--glass-blur)) saturate(var(--glass-sat));
    box-shadow:inset 0 0 0 1px var(--glass-edge),var(--sh-1);}
}
@media (min-width:900px){ :root{--glass-blur:28px} }                    /* desktop GPUs; mobile stays ≤ 20 */
@media (prefers-reduced-transparency:reduce),(prefers-contrast:more){
  .glass{background:var(--glass-solid);-webkit-backdrop-filter:none;backdrop-filter:none;
         box-shadow:inset 0 0 0 1px var(--line),var(--sh-1);}
}
```
Budget: ≤ 3 glass elements on screen at once; never nest glass in glass; never animate `backdrop-filter`
(animate the element's opacity/transform instead). Weak-device guard: if the first 30 frames of a sheet
animation average > 24 ms, add `data-glass="off"` on `<html>` (CSS maps it to the solid recipe).

### 2.4 Layout tokens
Breakpoints: `<600` phone (gutter 16; 20 at ≥ 414) · `≥600` tablet (content max 560, centred) ·
`≥900` desktop two-pane (list 380 px | detail flexible, content max 1120, outer gutter 32). Use
`env(safe-area-inset-*)` on bars/sheets; use `100dvh`, never `100vh`.

---

## 3. Typography

**Decision: embed exactly one font — Readex Pro, subset, variable weight 400–700 — as base64 `woff2` inside
the CSS.** Why not system fonts: Arabic system fonts differ wildly (SF Arabic / Segoe UI / Noto), so the same
screen would have 3 characters and different line metrics; the budget allows one font; Readex Pro (Lexend
family, OFL) is a geometric humanist with a **native-designed Arabic and a matching Latin** — one file covers
both scripts and harmonises with the ring/key geometry. System stack stays as fallback while loading.

**Measured:** Arabic letters (U+0620–064A, harakat, Arabic punctuation, Arabic-Indic digits, Persian پ چ ژ ک گ ی),
Basic Latin, quotes/dashes/bullet/ellipsis, all GSUB/GPOS shaping kept, hinting dropped → **29,524 bytes
woff2, 370 glyphs** (Latin-only would be 11.6 KB; not worth a second file). Base64 inflates 1.33× in the HTML
but gzip recovers it → ≈ 30 KB on the wire. Missing glyphs (✓ ● ✔) are **SVG icons**, never characters.
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
```css
@font-face{font-family:"Keyra Sans";font-weight:400 700;font-style:normal;font-display:swap;
  src:url(data:font/woff2;base64,AAAA…) format("woff2")}
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
| `--fs-display` | 2.125 | 34 → 36 | 700 | 1.15 | 1.40 | Unlock/Ready/Welcome title, TOTP label never |
| `--fs-t1` | 1.75 | 28 → 29.8 | 700 | 1.20 | 1.40 | Screen titles (Settings, Accounts) |
| `--fs-t2` | 1.375 | 22 → 23.4 | 600 | 1.30 | 1.50 | Sheet titles, empty-state titles |
| `--fs-headline` | 1.0625 | 17 → 18 | 600 | 1.35 | 1.60 | Row title, button label, section label |
| `--fs-body` | 1 | 16 → 17 | 400 | 1.45 | 1.70 | Paragraphs, inputs |
| `--fs-callout` | .9375 | 15 → 16 | 400/500 | 1.40 | 1.65 | Row subtitle, helper text |
| `--fs-caption` | .8125 | 13 → 13.8 | 500 | 1.35 | 1.60 | Captions, chips, section headers |
| `--fs-code` | 1.125 | 18 → 19 | 500 mono | 1.5 | 1.5 | Revealed password/username |
| `--fs-totp` | 2.125 | 34 → 36 | 600 mono | 1.1 | 1.1 | 2FA code `482 913` |

Rules: Arabic never below 400 weight, never letter-spaced, never italic/faux-bold, never uppercase.
Latin display/t1 tracking `−0.02em`, headline `−0.01em`, else 0 (`:lang(en)` only). Min text size 13 px (EN) /
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
Focus ring (every focusable): `outline:2px solid var(--focus);outline-offset:2px` plus `box-shadow:var(--sh-glow)`
on filled controls; `:focus-visible` only; never `outline:none` without replacement.

### 4.0 Icons
Inline SVG `<symbol>` sprite, 24 × 24 grid, stroke 1.75 px, round caps/joins, `currentColor`, no fills except
`star` (filled when on). Source: **Lucide** (ISC licence, https://lucide.dev) — copy only these 26 (≈ 5 KB raw,
≈ 2.2 KB gzip): `user`, `key-round`, `shield-check`, `copy`, `check`, `eye`, `eye-off`, `star`, `plus`,
`search`, `settings`, `lock`, `usb`, `x`, `chevron-right`, `chevron-left`, `trash-2`, `pencil`, `download`,
`upload`, `wifi`, `wand-sparkles`, `triangle-alert`, `refresh-cw`, `clock`, `share` (iOS share glyph),
`circle-help`. Direction-bearing icons (`chevron-*`, back arrows) mirror in RTL (`[dir=rtl] .i-dir{transform:scaleX(-1)}`);
all others never mirror. Sizes: 20 (inline), 24 (buttons, rows), 28 (action buttons), 56/64 (glyph in states).
"Both" icon = `user` and `key-round` side by side at 22 px with a 4 px gap, order fixed (user then key,
reading order of the typing: username first) — in RTL the order **does** mirror (user on the right).

### 4.1 Buttons
Height/radius: **lg** 56 / 20 (action buttons), **md** 48 / 16 (default), **sm** 36 / 12 (hit area padded to 44).
Label `--fs-headline` 600. Icon 20–24 left of label (start side). Min width 96. Full-width on phones inside sheets.

| Variant | Rest | Hover (`hover:hover`) | Pressed | Disabled | Loading |
|---|---|---|---|---|---|
| **Primary** | bg `--accent`, text `--on-accent` | bg `--accent-press` (light) / `--accent-press` (dark: lighter) | bg `--accent-press`, `scale(.97)` 120 ms `--ease-out` | opacity .4, no shadow, `aria-disabled` | label → 20 px spinner, width locked, `aria-busy` |
| **Tinted** | bg `--accent-soft`, text `--accent` | bg mix 6 % darker | `--fill-press` bg, scale .97 | opacity .4 | as above |
| **Secondary** | bg `--fill`, text `--text` | `--fill-press` | `--fill-press`, scale .97 | opacity .4 | as above |
| **Ghost** | transparent, text `--accent` | bg `--accent-soft` | bg `--accent-soft`, scale .98 | opacity .4 | — |
| **Danger** | bg `--err-soft`, text `--err`; **confirm** variant bg `--danger-fill` text #FFF | darker 6 % | scale .97 | opacity .4 | — |

Spinner: 20 px, 2 px stroke, 700 ms linear infinite rotate (static ring + "…" when reduced motion).
One primary button per screen region. Success flash on async completion: label crossfades to `check` icon 600 ms.

### 4.2 Action buttons (account sheet — the heart of daily use)
Order top → bottom: **Both** (primary, most common), then **Username | Password** (two columns), then **Code**
card (only if `hasTotp`). Gap 12.

- **Both:** primary lg, full width, 64 px high, radius 20. Left cluster `user`+`key-round` icons (24), label
  "Both" 17/600, sublabel 14 `--on-accent` @ .9 opacity (contrast 4.97 light, 6.53 dark) "Username, then password".
- **Username / Password:** tinted, 72 px high, radius 20, icon 28 above label (centred, 6 px gap).
  Each tile has a 36 × 36 **copy** chip at the end-top corner (hit area 44): copies that field to the phone
  clipboard and shows toast "Copied". Tapping the tile body = type action. Chip visible always (no hover-only).
- **Code card:** surface-2, radius 20, padding 16. Start: code in `--fs-totp` mono, groups of 3 separated by a
  0.35 em gap (`482 913`), `dir=ltr`. Under it: caption "Renews in 14 s" + 28 px ring (see 4.12). End: tinted sm
  button "Type" and a copy chip. Tap code = copy. The code refreshes from `GET /totp` every `remaining`s +
  200 ms; last 5 s the digits turn `--warn`. `409 no_time` → card shows caption "Clock not set — reload the page".
- Press = Ready state (4.11) replaces the action area **in place** (height-animated 320 ms), not a new page.
- A secondary line under the buttons (callout, `--text-3`): "Keyra types it after you press its button."

### 4.3 Account row
Min height 64; padding 12 16; layout: monogram (40 × 40, radius 12) · 12 gap · text column · trailing.
Title `--fs-headline` (1 line, ellipsis, `dir=auto`); subtitle `--fs-callout` `--text-2` = username (`dir=ltr`, 1 line)
or, if empty, host of URL. Trailing: `star` 16 px `--accent` if favorite; `chevron-right` 16 px `--text-3` (mirrors).
Rows sit in an **inset group card**: `--surface`, radius 20, `--sh-1`, rows separated by 1 px `--line`
starting after the monogram (`margin-inline-start:68px`). States: pressed bg `--fill`; focus ring inside
(offset −2); hover (desktop) `--surface-2`; selected (desktop two-pane) bg `--accent-soft`, 3 px accent bar on the
start edge. Long-press: none (no hidden gestures). Swipe: none.
Skeleton row: monogram circle + two bars (60 % / 35 % width, 12 px tall, radius 6).

**Monogram (deterministic colour, no network icons):**
- Letter: first letter/digit of the trimmed title via `Array.from` (never `[0]`), Latin upper-cased; if an
  Arabic title begins with the article "ال" followed by a letter, use the letter after it ("البنك" → "ب");
  empty title → key glyph.
- Colour: `i = FNV-1a32(NFKC(title.trim().toLowerCase())) mod 12`; fill = `P[i]`; letter `#FFFFFF`, 18 px/700,
  centred; overlay `linear-gradient(180deg,rgba(255,255,255,.16),rgba(255,255,255,0))`.
- `P` = `#C2410C #B45309 #4D7C0F #15803D #0F766E #0E7490 #1D4ED8 #6D28D9 #A21CAF #BE185D #B91C1C #475569`
  (white-on-fill 5.18 · 5.02 · 4.99 · 5.02 · 5.47 · 5.36 · 6.70 · 7.10 · 6.32 · 6.04 · 6.47 · 7.58 — all ≥ 4.5).
  Same palette in light and dark (white letter works on both).
- Sizes: 40 (row), 64 (sheet header), 28 (inline). Radius = 30 % of size.

### 4.4 Search field
Height 44, radius 14, bg `--fill`, no border (it is a text-input *affordance* by shape + icon). Start: `search`
20 px `--text-3`; text `--fs-body` `--text`; placeholder `--text-3` (4.96:1 on fill). End: `x` clear button 44 × 44
visible when non-empty. Focus: bg `--surface`, 2 px `--accent` border, `--sh-glow`. Sticky under the top bar
(`top:var(--bar-h)`, z 10) inside the same glass bar on phones. Search is client-side and instant:
normalise both sides (lower-case; strip tashkeel U+064B–0652 and tatweel U+0640; ا أ إ آ → ا; ى → ي; ة → ه;
Arabic-Indic digits → Western) and match title, url host, username; rank: prefix of title > word-prefix >
substring. Debounce 0 (≤ 500 entries). `inputmode="search"`, `enterkeyhint="search"`, `autocomplete=off`.
Empty result → empty state 4.13.

### 4.5 Segmented control
Track: bg `--fill`, radius 14, padding 3, height 44 (items ≥ 44 high). Thumb: `--surface`, radius 11, `--sh-1`,
text `--text` 600; inactive items `--text-2` 500. Thumb moves with `transform:translateX(calc(var(--i) * 100% * var(--dir)))`
where `--dir:1` (LTR) / `-1` (RTL set on `[dir=rtl]`), transition `--d-slow` `--ease-spring`. Items in DOM order,
which mirrors automatically in RTL (first option on the right). Max 4 items; labels ≤ 12 chars.
`role="radiogroup"`, arrows move, Home/End jump. Disabled opacity .4.

### 4.6 Inputs (text, secret, copy)
Label above (never beside), `--fs-caption` 600 `--text-2`, margin-bottom 6. Field: height 52, radius 14,
bg `--surface`, 1 px `--line-strong` border (≥ 3.4:1), padding-inline 14, text `--fs-body`. Focus: border 2 px
`--accent` + `--sh-glow`. Error: border `--err`, helper below with `triangle-alert` 16 px + text `--err`
(never colour alone). Helper `--fs-callout` `--text-2`. Disabled: bg `--fill`, text `--text-3`.
- **Secret field** (password, passphrase, backup passphrase): `type=password`; end slot has `eye`/`eye-off`
  toggle (44 × 44, `aria-pressed`, label "Show password"/"Hide password"). When revealed: `--font-mono`
  `--fs-code`, characters coloured (digits `--pw-digit`, symbols `--pw-symbol`, letters `--text`) by
  overlaying a mirrored `<div aria-hidden>`; plain colour-free fallback is acceptable if it costs > 0.5 KB.
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
| 60–79 | 3 | `--accent` | Good / جيدة |
| ≥ 80 | 4 | `--ok` | Strong / قوية |
Label (caption 600, same colour as the segments — all four pass 4.5:1) sits on the end side; announce changes
with `aria-live="polite"` only when the label changes. Unfilled segments `--ring-track`. Fill animates 200 ms.

### 4.8 Generator (SPEC §9.1: vault top bar "Generate" sheet, and inline in Add/Edit)
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
- **Type text…** (SPEC §9.2, in the top bar's ⋯ menu with Import and Backup): mono textarea (≤ 256, counter), warning
  listing characters Keyra can't type, switch "Type it twice" + segmented Tab | Enter, primary "Type it" → Ready
  (chip "Text"); the text is cleared after "Typed".
- **Password history** (SPEC §9.3): account sheet group under the details, rows "Used until {date}" + masked value,
  eye and copy; footer "Keyra keeps the last 10 passwords."

### 4.9 Sheets & dialogs
- **Phone (< 900): bottom sheet.** Body `--surface` opaque, top radius 28, `--sh-3`, max-height `calc(100dvh - 12px - env(safe-area-inset-top))`,
  padding-bottom `max(16px, env(safe-area-inset-bottom))`. Header: grabber 36 × 5 radius 3 `--line-strong` at top 8;
  sticky glass header 56 px (title centred 600; close `x` on the end side; optional primary text button on the
  start side, e.g. "Save"). Open: `translateY(100%)→0`, `--d-sheet` `--ease-spring`; scrim fade 200 ms. Close:
  `--d-base` `--ease-in`. Drag-to-dismiss from grabber/header (threshold 96 px or velocity > 0.5 px/ms), rubber-band
  above. Scrolls internally; background `inert` + `overflow:hidden`; Esc/back button/scrim tap closes (not for
  Ready state, which needs explicit Cancel, and not for unsaved Add/Edit — confirm "Discard changes?").
- **Desktop (≥ 900): centred dialog.** Width 480 (560 for Add/Edit, 640 for Import), radius 28, `--sh-3`, max-height
  86dvh, enter `scale(.96)→1` + opacity 200 ms `--ease-out`, scrim `--scrim`. Same header minus grabber.
  The **account sheet is not a dialog on desktop** — it is the right-hand detail pane (4.3 selected state).
- **Alert dialog** (confirmations: delete, factory reset, replace): always centred, width `min(320px, 100% − 48px)`,
  radius 28, padding 24, centred title (t2) + body (callout `--text-2`) + stacked full-width 48 px buttons
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

### 4.11 Signature: the **Ready** state (physical ↔ digital)
Appears in the account sheet in place of the action area when `state.pending` is non-null; the same component
(with different copy) serves **presence prompts**: onboarding step 3, Wi-Fi change, Restore-replace, Factory reset.

**Anatomy (card: solid `--surface-2` + 1 px `--line`, radius 28, padding 24, centred column, max-width 360; nothing scrolls beneath it, so no glass):**
1. **Ring** 176 × 176 px (SVG viewBox 0 0 200 200): track `circle r=88 stroke=var(--ring-track) stroke-width=10`;
   progress `circle r=88 stroke=var(--accent) stroke-width=10 stroke-linecap=round`, `transform=rotate(-90 100 100)`,
   `stroke-dasharray=553`, `stroke-dashoffset=553×(1−remaining/60)` (full ring at 60 s, drains **clockwise**
   in both LTR and RTL — it is a clock; never mirrored). Drive it with one CSS animation of duration
   `expiresIn` ms, `linear`, started with a negative `animation-delay` for elapsed time; re-sync from each poll if
   drift > 400 ms.
2. **Halo:** a third circle `r=88 stroke=var(--led) stroke-width=10 opacity=0` that **breathes**:
   `@keyframes breathe{0%,100%{opacity:.10;transform:scale(1)}50%{opacity:.45;transform:scale(1.14)}}`
   `animation:breathe var(--d-breath) var(--ease-io) infinite` (transform-origin centre). Period **1600 ms** =
   the firmware's `Led::Pending` blue breathing period (see 4.11.1). Start the animation at poll arrival so phase
   is approximately aligned; exact sync is impossible and not required.
3. **Glyph:** the key glyph 64 px, `--accent`, centred; on its own a subtle 1.0→1.04 scale breathing in phase with the halo.
4. **Title** (t2 600): "Ready — press Keyra's button". **Body** (callout `--text-2`, ≤ 2 lines): the one-line
   instruction. **Chip** (caption 600, tinted, radius pill): `what · title` e.g. "Password · GitHub".
5. **Countdown text** (mono 600 18 px, `--text`): `0:42`, updates each second, `aria-hidden`; an `aria-live="polite"`
   sr-only line announces at 30, 10, 5 s.
6. **Cancel** (ghost md): `POST /api/type/cancel` for type actions; for presence ops it just abandons the wait
   (the device times out by itself). Footer caption `--text-3`: "Or hold the button for 2 seconds to cancel."

**State machine** (UI ← `GET /api/state` poll 1 s while `pending`/`presence.awaiting`):
| State | Trigger | Visual |
|---|---|---|
| `ready` | `pending≠null` | ring accent, halo breathing, countdown running |
| `ready-warning` | `expiresIn ≤ 10 s` | ring + countdown colour `--warn`, ring stops breathing and the countdown digits tick-pulse (scale 1.08, 200 ms, each second) |
| `typing` | `last` not yet set, `pending` just became null and Δ < 1.5 s | ring full, glyph replaced by 3 dots bouncing (staggered 120 ms), title "Typing…" |
| `typed` | `last.ok && code="typed"` | see below |
| `expired` / `no_usb` / `unsupported_char` / `failed` / `cancelled` | `last.code` | error card (4.14) |
`last.at` ≤ 6000 ms counts as "this action's result"; ignore older results (avoid stale toasts after reload).

**"Typed ✓" success moment (1.9 s):** at t=0 ring snaps to full and stroke → `--ok` (160 ms); glyph cross-morphs to a
check (stroke-dashoffset draw, 300 ms, path `M72 102 l20 20 l38 -44`, `stroke=--ok` width 10, round); ring scale
`1→1.06→1` 420 ms spring; halo does one outward ripple (opacity .45→0, scale 1→1.5, 600 ms, `--ok`); title
"Typed" (t2); body "Done. Keyra is ready for the next one."; light haptic
`navigator.vibrate?.(12)` (Android; iOS ignores). After 1900 ms the card collapses back to the action buttons
(320 ms) and the row's lastUsed updates. Screen-reader: `role="status"` "Typed". Reduced motion: no
morph/ripple/scale; ring colour change + check appear instantly, same 1.9 s dwell.

#### 4.11.1 Firmware ↔ UI sync (ask the firmware owner to match)
| UI state | `io::Led` | Pattern |
|---|---|---|
| ready / awaiting presence (setup, Wi-Fi change, restore-replace, factory reset) | `Pending` / `AwaitPresence` | blue `#2F6BFF`, sinusoidal breathe 10 % ↔ 100 %, period **1600 ms** |
| typing | `Typing` | solid blue, 100 % |
| typed | `Success` | green `#22C55E` 100 % for 700 ms then off |
| expired / no_usb / failed | `Error` | red `#EF4444` three blinks, 150 ms on / 150 ms off |
| cancelled | `Idle` | off |
| Bluetooth pairing window (Settings → Bluetooth) | `Pairing` | cyan, sinusoidal breathe, period 1200 ms, up to 120 s |
Brightness follows Settings → LED brightness. If the firmware differs, change the UI constants `--d-breath` and
colours — the structure stays.

### 4.12 Mini ring (TOTP, countdowns inline)
28 × 28, `r=11 stroke-width=3.5`; same drain logic over `period` (30/60). Colour `--accent`, last 5 s `--warn`.

### 4.13 Empty states
Layout: centred, max-width 320, glyph tile 96 px (`--accent-soft` circle, key glyph 40 px `--accent`, 3 orbit dots
r=3 `--accent` @ .35 opacity placed at 30°, 150°, 270° on a 124 px circle, slow 24 s rotate; static if reduced
motion) · title t2 · body callout `--text-2` · CTA primary md. Never illustrations over 1 KB.
| Where | Title / body / CTA |
|---|---|
| Empty vault | "Nothing here yet" / "Add your first account, or import from Apple Passwords, Chrome, Bitwarden or 1Password." / "Add account" + ghost "Import" |
| No search results | "No matches" / "Nothing for “{q}”. Check the spelling or try fewer letters." / ghost "Clear search" |
| No favourites | section simply hidden |

### 4.14 Error & notice patterns
- **Inline notice** (banner inside sheet/screen): radius 16, padding 12 14, bg `--warn-soft`/`--err-soft`/`--accent-soft`,
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
- **Section header:** `--fs-caption` 600 `--text-3`, padding 8 20 6, no uppercase (Arabic has none; keep EN consistent). Sticky A–Z letter headers: top = bar height, bg `--bg`.
- **Settings row:** min 52, padding 0 16, label headline-ish 16/17 body, value `--text-2` at end, `chevron-right` mirrored.
  Group card same as account rows. Footer note under a group: callout `--text-3`.
- **Switch:** 51 × 31, knob 27; on = `--accent` track; off = `--fill-press` track + 1 px `--line-strong`. Knob travel 20 px toward the
  **end** side when on (mirrors in RTL via logical `inset-inline-start`). `role="switch"`, whole row is the hit target.
- **Slider:** native `input[type=range]` restyled: track 6 px `--ring-track`, filled part `--accent`, thumb 28 px white with `--sh-2`
  and 1 px `--line-strong`; native RTL reversal is correct, do not fight it. Step labels not shown; value shown at end.
- **Chip/status pill:** height 28, padding 0 10, radius pill, caption 600. Variants ok/warn/neutral/accent (soft bg + token text).
  **USB chip** (top bar): `usb` icon + "Plugged in" (ok) / "Not plugged in" (neutral, `--text-2`). Pure information, not a button.
- **Progress bar** (import): 6 px, radius 3, accent, determinate; label "42 of 140".
- **Skeleton:** `--fill` blocks radius 8 with a 1.4 s shimmer (`linear-gradient(90deg,transparent,rgba(255,255,255,.5),transparent)` in light,
  `.06` in dark, translating); only shown if loading > 150 ms; reduced motion → static `--fill`.
- **Page transition (push/pop):** incoming `translateX(24px)→0` (mirrored in RTL) + opacity, 280 ms `--ease-out`; outgoing fades. Reduced motion: 120 ms crossfade.
- **Pull to refresh:** none (list is local to the device; refresh icon not needed — list reloads on focus/visibility).

---

## 5. Screens

Conventions: **M** = phone 375–430 px; **D** = desktop ≥ 900 px. Routes are hash-based (`#/welcome`, `#/setup/1..3`,
`#/unlock`, `#/` list, `#/a/:id`, `#/a/:id/edit`, `#/new`, `#/import`, `#/backup`, `#/settings`) so Back works and sheets
are deep-linkable. Screen background `--bg`; the Welcome, Setup and Unlock/Locked screens add a soft glow:
`radial-gradient(60% 40% at 50% -8%, rgba(11,87,240,.12), transparent)` (dark: `rgba(108,156,255,.14)`).
Copy: AR is primary and written in warm Modern Standard Arabic; `{x}` = variable; `<bdi>` around Latin names.
Arabic plurals for counts: `n=1` "حساب واحد", `n=2` "حسابان", `3–10` "{n} حسابات", `11+` "{n} حساباً", `0` "لا حسابات".
Language: `navigator.language` starts with `ar` → Arabic, else English; manual switch persists in
`localStorage["keyra.lang"]`; `<html lang dir>` updates live without reload.

### 5.1 Welcome (first run only, `initialized=false`)
**M:** top-end language button (ghost sm: "English" / "العربية"). Centre column, top 18 % empty: logo tile 88 px (radius 22,
`--sh-2`), title display, subtitle callout `--text-2` (max 280 px). Then 3 steps as an inset group card (icon in
40 px `--accent-soft` squircle + headline + callout): *Plug in* (`usb`), *Tap an account* (`user`), *Press the button*
(`key-round`). Bottom: primary lg full-width, caption below. **D:** same column centred in a 440 px card on a glow background.
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
Shared chrome: back chevron (start), step dots (3 × 8 px, active 24 × 8 pill `--accent`, order follows reading direction), title
t1, body callout `--text-2`, content, sticky bottom primary lg (above keyboard via `visualViewport`). **D:** 440 px card.

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
**M:** glow bg; logo tile 72 px; title display; one secret field (autofocus, `autocomplete=current-password`); primary lg "Unlock";
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
**M** (top → bottom): sticky **glass top bar** 56 px — start: USB status chip; end: Settings (`settings`, 44) and Lock (`lock`, 44).
Large title t1 "Accounts" + count callout `--text-3`. Sticky **search field** (4.4) directly under (glass, same bar group; on scroll the large
title collapses by simply scrolling away). Sections as inset group cards: **Favorites** (all favorites), **Recently used**
(3 most recent non-favorites with `lastUsed>0`), then **All** grouped by first letter with sticky letter headers (script order = UI
language first: Arabic letters then Latin then `#` in AR; reverse in EN; letters of the same base compared after the normalisation of 4.4).
While searching: only one flat "Results" group, matches highlighted with `--accent-soft` bg on the matched substring.
**FAB:** 56 px accent circle (`plus` 24 white), end-bottom 16 px + safe area, `--sh-2`, z 20; hides while scrolling down
> 80 px, returns on scroll up. Bottom padding of the list 96 px so the FAB never covers a row.
**D:** left pane 380 px (top bar chips + search + list), right pane = account detail (5.5) or, when nothing selected, a calm
placeholder (glyph 64 `--text-3`, "Pick an account"). Add → centred dialog. Keyboard: `/` focuses search, ↑↓ moves selection, Enter opens, `n` new.
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
| a11y: add / lock / settings | إضافة حساب / قفل / الإعدادات | Add account / Lock / Settings |
| detail placeholder | اختر حساباً لعرضه | Pick an account to open it |

### 5.5 Account sheet (detail)
**M:** bottom sheet 92 dvh max (4.9), grabber; header: monogram 64, title t2 (`dir=auto`, 2 lines max), host callout `--text-2`; end: star toggle
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
**M:** full-height sheet (via Settings → Data, or the empty-state ghost button). Step A: four source cards (2 × 2 grid, 96 px high, radius 20,
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
**M:** sheet from Settings → Data. Two group cards. **Backup:** explanatory callout; secret field (backup passphrase, ≥ 12, strength meter, **separate** from
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

### 5.9 Settings
**M:** full-screen push (back chevron + t1 title), inset group cards in this order; **D:** centred dialog 560 with the same groups. Every
change saves immediately (`PUT /api/settings`, optimistic); no success toast; on error: revert + error toast "Couldn't save. Try again.". Wi-Fi name/password edits happen in a
sub-sheet with an explicit "Save" because they need presence → Ready → "Keyra's Wi-Fi will restart; rejoin it" (same reconnect card as 5.2).
Auto-lock opens a radio list sheet (1, 5, 15, 30, 60, 120 min). Typing speed segmented maps Slow/Normal/Fast → `keyDelayMs` 30/12/5
(**ASSUMPTION**; default 12 = firmware default). LED brightness slider 10–100 % → `ledBrightness` scaled to the firmware range (**ASSUMPTION** — confirm
range with firmware; UI never offers 0 so the "ready" light can't be turned off).
**Type test** (`{test:true}`): Ready component with the copy below. Language/theme apply instantly.
| Group | Row → control | العربية | English |
|---|---|---|---|
| title | | الإعدادات | Settings |
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
`share` icon rendered inline; others: 2 steps. "Not now" hides it for 7 days (`localStorage["keyra.a2hs"]=timestamp`); "Got it" hides forever. Also reachable from Settings → About.
**D:** never shown.
| Key | العربية | English |
|---|---|---|
| title | أضِف Keyra إلى الشاشة الرئيسية | Add Keyra to your Home Screen |
| body | افتحه بلمسة واحدة وبملء الشاشة، كأي تطبيق. | Open it in one tap, full screen, like any app. |
| iOS 1 / 2 / 3 | اضغط زرّ المشاركة [share] · اختر «إضافة إلى الشاشة الرئيسية» · اضغط «إضافة» | Tap the Share button [share] · Choose “Add to Home Screen” · Tap Add |
| Android 1 / 2 | افتح قائمة المتصفح ⋮ · اختر «إضافة إلى الشاشة الرئيسية» | Open the browser menu ⋮ · Choose “Add to Home screen” |
| buttons | فهمت / ليس الآن | Got it / Not now |

### 5.11 Locked
Reached by auto-lock, long press on the button, session expiry (401) or manual Lock. Same layout as Unlock but calmer: no glow, glyph 72 px
`--text-3`, title, reason line (callout `--text-2`), primary "Unlock" which reveals the passphrase field inline (field + button) with focus.
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
- **Focus:** visible 2 px ring + 2 px offset on every focusable (`--focus`, ≥ 5.26:1 light / 9.55:1 dark); order = DOM order = visual order in both directions;
  sheets/dialogs trap focus, set `inert` on the rest, restore focus on close; skip link "Skip to accounts" on desktop.
- **Targets:** ≥ 44 × 44 CSS px for every interactive element (visual smaller icons get padded hit areas); ≥ 8 px between adjacent targets.
- **Semantics:** list = `ul/li` with each row a single `<button>` (or `<a href="#/a/id">`); sheets `role=dialog aria-modal aria-labelledby`; switch `role=switch`;
  segmented `role=radiogroup`; progress `role=progressbar` with `aria-valuenow`; the Ready state `role=group aria-labelledby=title`, with the sr-only
  live line at 30/10/5 s and an assertive `role=status` for Typed/errors; toasts per 4.10; page title (`document.title`) updates per route; `lang` set on `<html>` and on
  inline foreign-script runs.
- **Reduced motion** (`@media (prefers-reduced-motion:reduce)` and an in-app override is **not** needed): all springs → 120 ms linear; no translate/scale/shake/ripple/morph; halo breathing off
  (ring shows static accent; the physical LED still breathes and the copy says so); countdown ring steps once per second instead of animating; skeleton shimmer off; orbit dots static;
  page transitions = crossfade. Information-bearing motion (the countdown value) is kept as text.
- **Reduced transparency / high contrast:** `.glass` → solid (2.3). `prefers-contrast:more`: `--line` becomes `--line-strong`, `--text-3` → `--text-2`.
- **Zoom/Dynamic Type:** root font-size stays `100%`/`106.25%` (never px on `html`); layouts verified at 200 % text and 320 px width without horizontal scroll;
  sheet content scrolls, buttons wrap labels (min-height, not height).
- **Touch & input:** `inputmode`, `enterkeyhint`, `autocomplete` per field; don't block paste; `autocapitalize=off` on credentials; no timeouts shorter than 60 s except the
  intentional pending-action expiry (announced, extendable by tapping again).
- **RTL rules.** Use only logical properties (`margin-inline`, `padding-inline`, `inset-inline`, `text-align:start`, `border-start-start-radius`; `float`/`left`/`right` banned);
  `<html dir>` drives everything. **Mirror:** layout order, chevrons/back arrows, slide transitions, segmented order, step-dot order, switch/slider direction (native), sheet
  header actions (Save on the start side stays "start"), `translateX` animations (multiply by `--dir`). **Never mirror:** the logo and key glyph, check/clock/ring (the countdown is
  clockwise in both), digits and anything `dir=ltr` (passwords, usernames, URLs, SSIDs, codes, version numbers), the `share` icon, phone/USB metaphors, images.
  Numbers inside Arabic sentences stay Western digits. A static test: render every screen at 390 px with `dir=rtl` and `dir=ltr`; the pixel-mirror of one must equal the other
  except the never-mirror items.
- **Testing checklist:** keyboard-only pass of every flow; VoiceOver iOS (Arabic + English); TalkBack; `prefers-reduced-motion`, forced dark, 200 % zoom; Lighthouse a11y ≥ 95.

---

## 7. Image-generation prompts (paste into ChatGPT; save files exactly as named)

How to use: paste each prompt in a **new** chat message, attach nothing. ChatGPT's native sizes are 1024², 1536×1024 and 1024×1536;
where the final aspect differs (b, d) the prompt asks for a wide composition with a safe centre band and the lead crops/resizes
(`magick in.png -gravity center -crop 1536x614+0+0 -resize 1600x640 out.png`). If any text comes out garbled, answer with:
"Remove all text and lettering from the image completely; keep everything else identical." — real headings are added later in Figma/HTML.
Keep the same chat for all six so the product design stays consistent; start each with "Using the same Keyra product design:" after (c).

**Shared style block** (already inside every prompt below, do not edit): palette — Keyra Blue `#0B57F0`, light blue `#6C9CFF`, LED blue `#2F6BFF`,
ink black `#080A10`, panel `#12151D`, paper `#F3F4F8`, white `#FFFFFF`, success green `#4ADE80` (only for the "typed" moment). Look: calm,
premium, Apple-product-page restraint; soft studio light; matte materials; subtle frosted glass only on small accents; generous empty space.
Avoid: padlock/shield/hacker clichés, neon clutter, circuit-board textures, stock people, real brand logos (Apple, Google, Microsoft), watermarks.

### (a) App icon → `docs/images/icon.png` (1024 × 1024, square)
```
Design a 1024×1024 app icon, full-bleed square (no rounded corners, no border, no mockup, no shadow outside the canvas).
Background: a smooth diagonal gradient from #4B86FF at the top-left to #0A47D8 at the bottom-right, very subtle, no noise.
Centre: a single bold white key glyph made of simple geometric strokes with perfectly round line caps — a ring-shaped bow (a circle outline) at the top,
a straight vertical shaft descending from the ring, and two short rectangular teeth pointing to the right, the lower tooth slightly shorter.
The glyph is about 52% of the canvas height, optically centred, stroke thickness about 11.5% of the glyph height, pure white #FFFFFF.
Add a very soft drop shadow under the glyph (dark blue, 35% opacity, large blur) and a faint white highlight fading from the top edge over the top 40% of the canvas (14% opacity).
Flat, crisp vector look like a modern iOS app icon. ABSOLUTELY NO TEXT, letters, numbers or extra symbols anywhere.
```

### (b) README hero banner → `docs/images/hero.png` (final 1600 × 640; generate wide 1536×1024)
```
Wide cinematic hero banner for a product called Keyra, generated in landscape. IMPORTANT composition rule: keep every important element inside the horizontal
centre band that is 60% of the image height (the top and bottom 20% will be cropped away) and leave the left 22% and right 22% of the width calm and almost empty,
because a headline will be placed there later.
Scene: a deep ink background (#080A10) fading to #12151D with one soft blue glow (#2F6BFF at 25% opacity, very large radius) behind the product.
Centre-right: a small matte graphite USB security key (a slim rounded-rectangle device about the size of a lighter, colour #1B1F2A, USB-C plug at one end,
a single round translucent button on top with a gently glowing ring of blue light #2F6BFF, a tiny debossed key glyph — a circle with a vertical stem and two teeth — next to it),
plugged into the side of a thin silver laptop (generic, no logo) that is only partly visible at the edge. Centre-left of the device, a floating smartphone
(generic black glass phone, no logo) seen from a slight angle, its screen showing an abstract dark interface: a large blue ring #6C9CFF with a white key symbol inside
and a few soft grey rounded bars as placeholder rows — NO readable text on the screen, only abstract shapes.
Thin light-blue curved line (#6C9CFF, 1.5 px) linking phone, device and laptop to suggest "wireless setup, typed into your computer".
Style: realistic soft 3D product render, shallow depth of field, very soft reflections, premium and calm. NO text, NO letters, NO logos, NO watermark anywhere in the image.
```

### (c) Product render of the device → `docs/images/device.png` (1536 × 1024, landscape)
```
Studio product photograph-style 3D render of a tiny hardware password key named Keyra, on a seamless light background (#F3F4F8) with a soft long contact shadow, 3/4 front view slightly from above.
The device: a minimal slim capsule-shaped enclosure about 55 mm long, matte anodised graphite (#1B1F2A) with ultra-fine texture, softly rounded corners, one flush USB-C plug (silver) protruding from one end.
On top: ONE round pill-shaped translucent button, frosted milky-white glass, with a soft ring of electric blue light (#2F6BFF) glowing around it and a faint glow cast on the surface below.
Beside the button, a small debossed key symbol (a circle outline with a vertical stem and two small teeth to the right), and on the lower edge the single word "Keyra" debossed in a clean rounded geometric sans-serif,
spelled exactly K-e-y-r-a, small and subtle. No other text, no markings, no screws, no extra ports, no cable, no logo other than this.
Lighting: large softbox from top-left, gentle rim light, realistic materials, crisp but soft edges, 50 mm lens look, shallow depth of field.
Compose with the device filling about 60% of the width, centred, plenty of clean background around it. If the word "Keyra" cannot be spelled correctly, leave the surface blank instead of guessing.
```

### (d) Social preview → `docs/images/social.png` (final 1280 × 640; generate wide 1536×1024)
```
Social-sharing card background in landscape, generated wide. Composition rule: all important content inside the central 2:1 band (the top and bottom 17% of the image will be cropped);
keep a 10% margin from the left and right edges.
Left half: a large rounded-square app tile (squircle) coloured with a diagonal blue gradient #4B86FF → #0A47D8, containing a bold white key glyph (a ring-shaped bow, a vertical shaft, two small teeth to the right, round line caps), with a soft shadow.
Right half: the matte graphite (#1B1F2A) slim USB key with a glowing blue (#2F6BFF) ring button on top, tilted 12 degrees, floating slightly above a very soft blue glow; behind it a very faint, large, thin blue ring (#6C9CFF at 20% opacity) as a motif.
Background: deep ink #080A10 with a subtle radial glow of #0B57F0 at 15% opacity from the top-centre. Between the two halves leave empty space — a title will be placed there later.
Style: crisp, premium, minimal, soft 3D with matte materials. NO text, NO letters, NO numbers, NO watermark.
```

### (e) How-it-works illustration → `docs/images/how-it-works.png` (1536 × 1024, landscape; used full-width in README)
```
A clean three-step explainer illustration, landscape, three equal rounded panels side by side (radius large, panel colour #12151D on a #080A10 background, 1 px hairline border rgba(255,255,255,0.09), thin gaps between panels).
Style: soft matte 3D "clay" objects, calm, minimal, consistent lighting from the top-left, palette limited to #0B57F0, #6C9CFF, #2F6BFF, #F3F4F8, #12151D and a touch of success green #4ADE80 in panel 3 only.
Panel 1 — "Plug in": a small slim graphite USB key with a pulsing blue ring button plugged into the side of a thin silver laptop (generic, no logo); on the laptop screen a blank login form with an empty field and a cursor.
Panel 2 — "Pick": a generic black phone shown upright; its screen shows an abstract dark list of rounded rows with coloured square avatars, and a big blue pill button highlighted with a fingertip about to tap it. NO readable text on the screen — only abstract bars.
Panel 3 — "Press": a close view of a finger pressing the round glowing button of the Keyra key; above it the laptop's login field now filled with a row of dots (●●●●●●●●) and a small green check badge #4ADE80.
At the top-centre of each panel place only a small circled digit: 1, 2, 3 (white digit in a #0B57F0 circle). Use NO other text anywhere — no words, no captions, no labels, no logos. If any digit is malformed, regenerate with the digits removed.
Thin light-blue connector arrows (#6C9CFF) between the panels, pointing left to right.
```

### (f) Dark-mode phone mockup → `docs/images/phone-dark.png` (1024 × 1536, portrait)
```
Portrait product mockup: a single generic modern smartphone (black glass, thin bezels, no brand, no logo, no visible notch text) floating at a slight 8-degree angle, centred, on a deep ink background (#080A10) with a soft blue radial glow (#2F6BFF at 20% opacity) behind it and a gentle floor reflection.
The phone screen shows a dark-mode app (screen background #080A10, card surfaces #12151D): at the top a small centred rounded-square blue logo tile with a white key glyph;
in the middle a large circular progress ring (stroke 10 px, #6C9CFF, about 70% filled, clockwise) with a soft pulsing blue halo (#2F6BFF), a white-blue key glyph in its centre;
below the ring two short horizontal rounded bars in light grey (#B1B8C8 and #8D95A8) as placeholder title and subtitle, then a small pill chip (#202B41 fill) with a short bar inside, and a full-width ghost-style rounded button outline.
CRITICAL: the screen must contain NO readable text, NO letters, NO numbers — only abstract bars and shapes (real screenshots with real Arabic/English text will be composited later).
Style: photorealistic 3D render, crisp screen, subtle glass reflections, premium and calm. NO watermark.
```

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

## Appendix A — Byte budget (gzip, target ≤ 150 KB; estimate ≈ 95 KB)
| Item | KB |
|---|---|
| Preact + hooks (+ `preact/compat` **not** used) | 5 |
| App code (screens, API client, CSV parser, i18n strings AR+EN, generator, strength) | 40–50 |
| CSS (tokens twice-themed, components, RTL-free thanks to logical props) | 8–10 |
| Font `keyra-sans.woff2` (base64 inline; already compressed) | 30 |
| Icons sprite (26) + logo + manifest icons | 3 (+ PNG icons served separately, long cache) |
| **Total** | **≈ 86–98** |
Headroom ≈ 50 KB: do **not** spend it on a second font, a charting lib, an animation lib or a CSV lib (≈ 60 lines of code).

## Appendix B — Assumptions the lead must confirm (flagged `ASSUMPTION` above)
1. **LED patterns** (4.11.1): Pending = blue breathe 1600 ms period; Success green 700 ms; Error red 3 blinks. Tell the firmware owner or change `--d-breath`.
2. **Typing speed** presets map to `keyDelayMs` 30 / 12 / 5; default 12 (firmware default).
3. **`ledBrightness`** range is unspecified in SPEC §5 — UI offers 10–100 % and scales to the firmware range; never 0.
4. **Presence-gated settings** (Wi-Fi change, restore-replace, factory reset) reuse the Ready component; the UI relies on `state.presence.{awaiting,op,expiresIn}`.
5. **Clipboard** is unavailable on `http://` (insecure context) — the `execCommand('copy')` fallback in 4.6 is mandatory, not optional.
6. **Import export-menu names** (5.7) were written from memory of current app versions; verify against the real apps before release.
7. **Countdown source of truth** = `expiresIn` from the API (60 s); the UI never assumes 60 when the API says otherwise.
