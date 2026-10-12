# Keyra — Brand

One source for the logo, colour, type and motion. `DESIGN.md` (web app), the Android theme, the extension and
the README follow this file. Contrast ratios are computed (WCAG 2.x), not eyeballed.

## 1. Idea

**Graphite & Ivory.** Keyra is an object you hold: a dark, quiet instrument with one button. The identity is
monochrome — ink, graphite and warm ivory paper — so the one colour it does use means something.

- **Blue means one thing:** Keyra is waiting for your press. It is the colour of the LED on the board
  (firmware `led_anim.cpp`, `Pattern::Pending`). Never use blue for decoration, links or primary buttons.
- **One line.** The mark is a shield drawn as a single continuous line. Lines, hairlines and strokes carry
  the identity everywhere: thin rules instead of boxes, outlined icons with round caps, the Contour as the
  press indicator.
- **Calm, tangible, precise.** Lots of air, real materials (graphite, paper, hairline light), exact radii,
  monospace for secrets.

## 2. Logo — the Contour

A shield drawn as one line with round caps and joins, open at the top-right edge: the line starts and ends
at the gap, the moment the press closes. It never mirrors in RTL.

Geometry (100-unit grid, the same everywhere):

```
M72.3 31.37 L75.5 33 V44 C75.5 60 64 73 50 80 C36 73 24.5 60 24.5 44 V33 L50 20 L63.8 27.04
```

- Stroke 5 (icon, ≥ 64 px). Stroke 8 and a wider gap (`a=62.5, b=73.5`) for lockups and anything under
  48 px (`*-bold.svg`, `icon-small.svg`). Both ends on the apex→corner edge `y = 20 + (x − 50)·13/25.5`.
- Bounds 22–78 × 17.5–82.5 at stroke 5: optically centred in its tile.

Files (`docs/brand/logo/`):

| File | Use |
|---|---|
| `icon.svg` | App icon: graphite tile (rx 22.5 %), top light, hairline rim, ivory line with a quiet shadow. |
| `icon-square.svg` | Full-bleed version for shapes the platform cuts (Android maskable, iOS). Mark at 82 %. |
| `icon-small.svg` | Flat, bold, for 16–48 px (favicon, extension toolbar). |
| `mark.svg` / `mark-white.svg` | Bare mark, stroke 5. `-bold` variants: stroke 8. |
| `logo.svg` / `logo-white.svg` | Lockup: mark + **Keyra**. |
| `logo-ar.svg` / `logo-ar-white.svg` | Lockup: **كيرا** + mark on the start (right) side. |

Lockup: wordmark Readex Pro 500, tracking −0.02 em, outlined; mark at 1.3× cap height, stroke 8, gap 30 units
at 100 px type. Clear space around any logo = the mark's width ÷ 2. Never recolour the mark blue, never
outline the tile, never add effects beyond those in `icon.svg`.

Generated from these sources: `web/public/{favicon.svg,icon-192.png,icon-512.png,apple-touch-icon.png}`
(`npm run icons` in `web/`), the extension icons, the Android adaptive icon (vector copy of the path).

## 3. Colour

Light — ivory paper:

| Token | Hex | Contrast | Role |
|---|---|---|---|
| bg | `#F4F3EF` | — | Warm ivory page |
| surface | `#FFFFFF` | — | Rows, cards |
| surface-2 | `#EFEEE9` | — | Grouped background, inputs |
| fill / fill-press | `#E9E8E3` / `#DFDED8` | — | Neutral controls |
| ink (text) | `#111214` | 16.9 : 1 on bg | Text, primary buttons |
| ink-2 | `#4A4C52` | 7.7 : 1 | Secondary text |
| ink-3 | `#66686F` | 5.0 : 1 (4.6 on fill) | Captions, placeholders |
| hairline | `rgba(17,18,20,.10)` | — | Dividers |
| signal | `#1F5BEF` | 5.0 : 1 | Waiting for the press. Only that. |
| ok / err / warn | `#0A7A3D` / `#C1251C` / `#8F5200` | 4.9 / 5.3 / 5.6 | States |

Dark — graphite:

| Token | Hex | Contrast | Role |
|---|---|---|---|
| bg | `#0B0C0E` | — | Graphite page |
| surface | `#141518` | — | Rows, cards |
| surface-2 | `#1B1C20` | — | Grouped background, inputs |
| fill / fill-press | `#222328` / `#2B2C32` | — | Neutral controls |
| ink | `#F2F2EF` | 17.5 : 1 | Text, primary buttons (ivory on graphite) |
| ink-2 | `#A9ABB2` | 8.5 : 1 | Secondary text |
| ink-3 | `#8A8C93` | 5.8 : 1 (5.1 on surface-2) | Captions |
| hairline | `rgba(255,255,255,.08)` | — | Dividers |
| signal | `#6C9CFF` | 7.3 : 1 | Waiting for the press |
| ok / err / warn | `#4ADE80` / `#FF7A70` / `#FBBF4A` | 11.2 / 7.7 / — | States |

Primary action = ink fill with paper text (light) / ivory fill with graphite text (dark). Danger = err.
Brand surfaces outside the app (README art, social cards, Android icon) are graphite with ivory.

## 4. Type

- **Readex Pro** (OFL) for Arabic and Latin, already embedded. Wordmark 500; headings 600; body 400.
- Display sizes tight (−0.02 em Latin, none in Arabic), Arabic 6 % larger with looser leading (DESIGN §3).
- Monospace (system `ui-monospace`) for secrets, codes, addresses and the "receipt" lines under actions.
- Western digits everywhere, so codes match what Keyra types.

## 5. Motion — the Contour

- **Waiting for the press:** the Contour's line in signal blue, the gap breathing at the LED's 1.2 s
  rhythm (same as the board).
- **Done:** the line closes the gap (stroke-dashoffset, ~420 ms, spring), turns ink/ivory, then a short
  receipt line appears in monospace ("Typed · USB").
- **Refused / expired:** the line retracts to its two ends in err colour; no shake.
- Durations 120 / 200 / 320 / 420 ms; ease-out `cubic-bezier(.2,.8,.2,1)`; springs via CSS `linear()`.
- `prefers-reduced-motion`: no drawing or breathing; states switch with a 120 ms fade.

## 6. Imagery

Real photographs of the real board, or clearly labelled renders. Dark graphite studio light, one soft key
light, ivory accents, the LED ring as the only colour. No neon, no glowing blue gradients, no circuit-board
clip-art, no padlocks.
