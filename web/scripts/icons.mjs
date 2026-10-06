// Builds the home-screen icons the firmware serves from the owner's app icon art
// (docs/images/icon.png, 1024², opaque, full-bleed). Run after replacing that file: `npm run icons`.
// Downscales with resvg (already a devDependency), then quantizes with pngquant when it is on PATH.
import { Resvg } from '@resvg/resvg-js';
import { execFileSync } from 'node:child_process';
import { readFileSync, writeFileSync } from 'node:fs';
import { fileURLToPath } from 'node:url';

const src = fileURLToPath(new URL('../../docs/images/icon.png', import.meta.url));
const out = (name) => fileURLToPath(new URL(`../public/${name}`, import.meta.url));

const b64 = readFileSync(src).toString('base64');
const svg = `<svg xmlns="http://www.w3.org/2000/svg" xmlns:xlink="http://www.w3.org/1999/xlink" width="1024" height="1024">
<image width="1024" height="1024" xlink:href="data:image/png;base64,${b64}"/></svg>`;

let quantize = true;
for (const [name, size] of [['icon-512.png', 512], ['icon-192.png', 192], ['apple-touch-icon.png', 180]]) {
  const png = new Resvg(svg, { fitTo: { mode: 'width', value: size }, background: '#0B57F0' }).render().asPng();
  if (quantize) {
    try {
      writeFileSync(out(name), execFileSync('pngquant', ['--quality=70-90', '--speed=1', '--strip', '-'], { input: png }));
      continue;
    } catch (e) {
      if (e.code !== 'ENOENT') throw e;
      quantize = false;
      console.warn('pngquant not found: writing unquantized PNGs');
    }
  }
  writeFileSync(out(name), png);
}
