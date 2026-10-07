// Copies the single-file build into the firmware's embedded asset folder (SPEC §3, §5 Static).
import { copyFileSync, readFileSync, writeFileSync } from 'node:fs';
import { gzipSync } from 'node:zlib';
import { fileURLToPath } from 'node:url';

const dist = (f) => fileURLToPath(new URL(`../dist/${f}`, import.meta.url));
const www = (f) => fileURLToPath(new URL(`../../firmware/components/keyra_api/www/${f}`, import.meta.url));

const html = readFileSync(dist('index.html'));
const gz = gzipSync(html, { level: 9 });
writeFileSync(www('index.html.gz'), gz);

for (const f of ['manifest.webmanifest', 'icon-192.png', 'icon-512.png', 'apple-touch-icon.png', 'favicon.svg']) {
  copyFileSync(dist(f), www(f));
}

const kb = (n) => `${(n / 1024).toFixed(1)} KB`;
console.log(`index.html ${kb(html.length)} → index.html.gz ${kb(gz.length)} (budget 170 KB)`);
if (gz.length > 170 * 1024) {
  console.error('index.html.gz is over the 170 KB budget');
  process.exit(1);
}
