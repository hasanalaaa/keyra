// Rasterises the DESIGN.md §1.4 app icon (key glyph on a blue gradient) into the PNGs the
// firmware serves. Placeholder until the owner's generated art (DESIGN §7a) replaces it.
import { Resvg } from '@resvg/resvg-js';
import { writeFileSync } from 'node:fs';
import { fileURLToPath } from 'node:url';

const out = (name) => fileURLToPath(new URL(`../public/${name}`, import.meta.url));

// Glyph bbox on the 64-grid is 20–44 × 10–53 (centre 32, 31.5). Scale so it is 52 % of a
// 1024 canvas, optically centred 10 px up; stroke ≈ 11.5 % of the glyph height.
const scale = 532 / 43;
const tx = 512 - 32 * scale;
const ty = 502 - 31.5 * scale;
const stroke = (0.115 * 532) / scale;

const svg = `<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 1024 1024" width="1024" height="1024">
<defs>
  <linearGradient id="bg" x1="0.2" y1="0" x2="0.8" y2="1">
    <stop offset="0" stop-color="#4B86FF"/><stop offset="1" stop-color="#0A47D8"/>
  </linearGradient>
  <linearGradient id="hi" x1="0" y1="0" x2="0" y2="1">
    <stop offset="0" stop-color="#fff" stop-opacity=".14"/><stop offset=".4" stop-color="#fff" stop-opacity="0"/>
  </linearGradient>
  <filter id="sh" x="-30%" y="-30%" width="160%" height="160%">
    <feDropShadow dx="0" dy="14" stdDeviation="12" flood-color="#00145A" flood-opacity=".35"/>
  </filter>
</defs>
<rect width="1024" height="1024" fill="url(#bg)"/>
<rect width="1024" height="1024" fill="url(#hi)"/>
<g filter="url(#sh)" transform="translate(${tx} ${ty}) scale(${scale})" fill="none" stroke="#fff"
   stroke-width="${stroke}" stroke-linecap="round" stroke-linejoin="round">
  <circle cx="32" cy="22" r="9"/><path d="M32 31v19M32 41h9M32 50h6"/>
</g>
</svg>`;

for (const [name, size] of [['icon-512.png', 512], ['icon-192.png', 192], ['apple-touch-icon.png', 180]]) {
  const png = new Resvg(svg, { fitTo: { mode: 'width', value: size } }).render().asPng();
  writeFileSync(out(name), png);
}
