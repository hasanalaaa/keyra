// Toolbar and store icons from the owner's app icon art (docs/images/icon.png, 1024², full-bleed),
// rounded like the logo tile (DESIGN §1.3: radius 25 %). Run after replacing that file: `npm run icons`.
import { Resvg } from '@resvg/resvg-js';
import { mkdirSync, readFileSync, writeFileSync } from 'node:fs';
import { fileURLToPath } from 'node:url';

const src = fileURLToPath(new URL('../../docs/images/icon.png', import.meta.url));
const out = fileURLToPath(new URL('../icons/', import.meta.url));
mkdirSync(out, { recursive: true });

const b64 = readFileSync(src).toString('base64');
const svg = `<svg xmlns="http://www.w3.org/2000/svg" xmlns:xlink="http://www.w3.org/1999/xlink" width="1024" height="1024">
<defs><clipPath id="c"><rect width="1024" height="1024" rx="232"/></clipPath></defs>
<image clip-path="url(#c)" width="1024" height="1024" xlink:href="data:image/png;base64,${b64}"/></svg>`;

for (const size of [16, 32, 48, 128]) {
  const png = new Resvg(svg, { fitTo: { mode: 'width', value: size } }).render().asPng();
  writeFileSync(`${out}icon-${size}.png`, png);
}
console.log('icons written to extension/icons/');
