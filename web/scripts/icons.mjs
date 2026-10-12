// Builds the icons the firmware serves from the logo sources in docs/brand/logo (BRAND.md).
// Run after changing those files: `npm run icons`. Renders with resvg (a devDependency), then
// quantizes with pngquant when it is on PATH.
import { Resvg } from '@resvg/resvg-js';
import { execFileSync } from 'node:child_process';
import { copyFileSync, readFileSync, writeFileSync } from 'node:fs';
import { fileURLToPath } from 'node:url';

const logo = (name) => fileURLToPath(new URL(`../../docs/brand/logo/${name}`, import.meta.url));
const out = (name) => fileURLToPath(new URL(`../public/${name}`, import.meta.url));

// The rounded tile for "any"; the full-bleed square where the platform cuts its own shape
// (Android maskable, iOS home screen).
const jobs = [['icon-192.png', 'icon.svg', 192], ['icon-512.png', 'icon-square.svg', 512], ['apple-touch-icon.png', 'icon-square.svg', 180]];
let quantize = true;
for (const [name, src, size] of jobs) {
  const png = new Resvg(readFileSync(logo(src), 'utf8'), { fitTo: { mode: 'width', value: size } }).render().asPng();
  if (quantize) {
    try {
      writeFileSync(out(name), execFileSync('pngquant', ['--quality=80-95', '--speed=1', '--strip', '-'], { input: png }));
      continue;
    } catch (e) {
      if (e.code !== 'ENOENT') throw e;
      quantize = false;
      console.warn('pngquant not found: writing unquantized PNGs');
    }
  }
  writeFileSync(out(name), png);
}
copyFileSync(logo('icon-small.svg'), out('favicon.svg'));
