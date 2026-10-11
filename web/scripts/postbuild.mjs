// Copies the build into the firmware's embedded asset folder (SPEC §3, §5 Static) and enforces the
// byte budget (DESIGN Appendix A):
//   index.html.gz  the app shell (no-cache)
//   assets.pack    every hashed file under dist/assets/, served at /assets/<name> (immutable)
// assets.pack layout (little-endian), read by keyra_api's asset_pack.cpp:
//   "KYA1", then per file: u16 path length, path, u16 type length, Content-Type + NUL (counted in the
//   length), u8 flags (bit 0 = gzip), u32 size, body.
import { copyFileSync, readFileSync, readdirSync, writeFileSync } from 'node:fs';
import { gzipSync } from 'node:zlib';
import { fileURLToPath } from 'node:url';

const dist = (f) => fileURLToPath(new URL(`../dist/${f}`, import.meta.url));
const www = (f) => fileURLToPath(new URL(`../../firmware/components/keyra_api/www/${f}`, import.meta.url));

// Bytes over the air, gzip -9 where the device sends it gzipped.
const INITIAL_BUDGET = 130 * 1024; // what opening the app and showing the vault list downloads
const TOTAL_BUDGET = 400 * 1024; // everything embedded in the firmware for the web app

const TYPES = {
  '.js': 'text/javascript; charset=utf-8',
  '.css': 'text/css; charset=utf-8',
  '.woff2': 'font/woff2',
};

const html = readFileSync(dist('index.html'));
const shell = gzipSync(html, { level: 9 });
writeFileSync(www('index.html.gz'), shell);

const files = new Map(); // name → { body, gzip }
for (const name of readdirSync(dist('assets')).sort()) {
  const ext = name.slice(name.lastIndexOf('.'));
  if (!TYPES[ext]) throw new Error(`assets/${name}: no Content-Type for ${ext}`);
  if (!/^[A-Za-z0-9._-]+$/.test(name)) throw new Error(`assets/${name}: unexpected file name`);
  const raw = readFileSync(dist(`assets/${name}`));
  const gz = gzipSync(raw, { level: 9 });
  // woff2 is already compressed: gzip would only cost the device's time.
  files.set(name, gz.length < raw.length * 0.95 ? { body: gz, gzip: true, type: TYPES[ext] } : { body: raw, gzip: false, type: TYPES[ext] });
}

const parts = [Buffer.from('KYA1')];
for (const [name, f] of files) {
  const path = Buffer.from(`/assets/${name}`);
  const type = Buffer.from(`${f.type}\0`); // a C string the firmware hands to httpd as is
  const head = Buffer.alloc(2 + path.length + 2 + type.length + 1 + 4);
  let o = head.writeUInt16LE(path.length, 0);
  o += path.copy(head, o);
  o = head.writeUInt16LE(type.length, o);
  o += type.copy(head, o);
  o = head.writeUInt8(f.gzip ? 1 : 0, o);
  head.writeUInt32LE(f.body.length, o);
  parts.push(head, f.body);
}
writeFileSync(www('assets.pack'), Buffer.concat(parts));

for (const f of ['manifest.webmanifest', 'icon-192.png', 'icon-512.png', 'apple-touch-icon.png', 'favicon.svg', 'tap.html']) {
  copyFileSync(dist(f), www(f));
}

// The initial load: what index.html links (script, stylesheet, preloaded font) and every chunk those
// scripts import statically. Dynamic import()s are the lazily loaded screens.
const initial = new Set();
const visit = (name) => {
  if (initial.has(name)) return;
  if (!files.has(name)) throw new Error(`index.html or a chunk links assets/${name}, which the build did not produce`);
  initial.add(name);
  if (!name.endsWith('.js')) return;
  const code = readFileSync(dist(`assets/${name}`), 'utf8');
  for (const m of code.matchAll(/(?:\bfrom|\bimport)\s*["']\.\/([^"']+)["']/g)) visit(m[1]);
};
for (const m of html.toString().matchAll(/(?:src|href)="\/assets\/([^"]+)"/g)) visit(m[1]);

const sum = (names) => [...names].reduce((n, name) => n + files.get(name).body.length, shell.length);
const initialBytes = sum(initial);
const totalBytes = sum(files.keys());

const kb = (n) => `${(n / 1024).toFixed(1)} KB`;
const rows = [...files].sort((a, b) => b[1].body.length - a[1].body.length);
for (const [name, f] of rows) console.log(`  ${initial.has(name) ? '*' : ' '} ${kb(f.body.length).padStart(9)}  ${name}${f.gzip ? '' : ' (stored)'}`);
console.log(`  * ${kb(shell.length).padStart(9)}  index.html`);
console.log(`initial (*) ${kb(initialBytes)} (budget ${kb(INITIAL_BUDGET)}) · total ${kb(totalBytes)} (budget ${kb(TOTAL_BUDGET)})`);
if (initialBytes > INITIAL_BUDGET) {
  console.error('the initial load is over its budget');
  process.exit(1);
}
if (totalBytes > TOTAL_BUDGET) {
  console.error('the embedded web app is over its total budget');
  process.exit(1);
}
