// Toolbar and store icons from the logo sources in docs/brand/logo (BRAND.md §2): the flat, bold
// icon-small.svg for 16 and 32 px, the app tile icon.svg for 48 and 128 px. Run after changing those
// files: `npm run icons`. Renders with resvg (a devDependency), then quantizes with pngquant when it is on PATH.
import { Resvg } from '@resvg/resvg-js';
import { execFileSync } from 'node:child_process';
import { mkdirSync, readFileSync, writeFileSync } from 'node:fs';
import { fileURLToPath } from 'node:url';

const logo = (name) => fileURLToPath(new URL(`../../docs/brand/logo/${name}`, import.meta.url));
const out = (name) => fileURLToPath(new URL(`../icons/${name}`, import.meta.url));
mkdirSync(out(''), { recursive: true });

const jobs = [[16, 'icon-small.svg'], [32, 'icon-small.svg'], [48, 'icon.svg'], [128, 'icon.svg']];
let quantize = true;
for (const [size, src] of jobs) {
  const png = new Resvg(readFileSync(logo(src), 'utf8'), { fitTo: { mode: 'width', value: size } }).render().asPng();
  const name = out(`icon-${size}.png`);
  if (quantize) {
    try {
      writeFileSync(name, execFileSync('pngquant', ['--quality=80-95', '--speed=1', '--strip', '-'], { input: png }));
      continue;
    } catch (e) {
      if (e.code !== 'ENOENT') throw e;
      quantize = false;
      console.warn('pngquant not found: writing unquantized PNGs');
    }
  }
  writeFileSync(name, png);
}
console.log('icons written to extension/icons/');
