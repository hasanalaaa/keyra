#!/usr/bin/env node
// Assembles the website into one static folder (default site/dist) for GitHub Pages or a local preview.
//
//   node site/build.mjs                                   no firmware: the installer shows the boards, disabled
//   node site/build.mjs --release DIR --version v0.4.4    DIR holds the release assets (gh release download)
//   node site/build.mjs ... --out DIR                     somewhere else than site/dist
//
// It copies the pages, the curated images, screenshots and the font, writes one ESP Web Tools manifest per
// board whose app is in the release (firmware/<id>.json, binaries under firmware/<version>/), and fills the
// board table, the board picker and the release version into both pages. GitHub release downloads send no
// CORS headers, which is why the binaries are served from the site itself.
import { cpSync, existsSync, mkdirSync, readFileSync, readdirSync, rmSync, writeFileSync } from 'node:fs';
import { createHash } from 'node:crypto';
import { dirname, join, resolve } from 'node:path';
import { fileURLToPath } from 'node:url';
import { parseArgs } from 'node:util';

const site = dirname(fileURLToPath(import.meta.url));
const root = resolve(site, '..');
const { values: args } = parseArgs({
  options: { release: { type: 'string' }, version: { type: 'string' }, out: { type: 'string', default: join(site, 'dist') } },
});
if (args.release && !/^v\d+\.\d+\.\d+([-.][0-9A-Za-z.-]+)?$/.test(args.version ?? '')) {
  throw new Error('--release needs --version vX.Y.Z (the release tag)');
}
const out = resolve(args.out);
const fail = (msg) => { throw new Error(msg); };

// ESP Web Tools, pinned; every module it loads is checked against these hashes (sha384 of the npm package
// esp-web-tools@10.4.0 dist/web, identical on unpkg). A changed file on the CDN refuses to load instead of
// flashing something else. To upgrade: npm pack the new version and recompute.
const EWT = 'https://unpkg.com/esp-web-tools@10.4.0/dist/web/';
const EWT_SRI = {
  'install-button.js': 'sha384-9XfyvAabgkISlB/Xb3OpshGKpZx0TOt5Mmu78SeiNU3so/p4o2s9m66vy6Utdnom',
  'index-Dt9w-IG1.js': 'sha384-qDnmUz4Vrcy5l9ZV4nrPjsyYSeFTtX4OJNYJEEeZixp894cbdDwh+jeO/d//X/6I',
  'install-dialog-im156JnI.js': 'sha384-wMJi9BXikLp2SPdWr7OYNwSRpoiEgrPRT3APdi1KY6iYiMRoYO8vD5vBrBScG5j+',
  'styles-sT2V1cOw.js': 'sha384-77x+BOw+Zw9kRRNMOF4W5I/luoc5EGHclPXlMwdlEpdWjh2qItAMrOMlzLJzVO9y',
  'esp32-DNPRK0Ay.js': 'sha384-1mT/sUZ9QQrZft8TM1YK7RfyIUggyewqL0w7GrOvQ0aHk1JPIsMsmZeUKcWGWX3G',
  'esp32c2-CQ3ns5Nm.js': 'sha384-AcOQBXWICST0TTlwGYbYA8cElKuXwrwW0L10NSF0YBe1PkndGs5RhsuDzTR/OZhi',
  'esp32c3-2tchr35W.js': 'sha384-JF+4Kgy21HYqVI0DI+S5SAUyxRrWHKN/yvQ2TzxFzvi3V4IzoyW0WXPscyrzIjwE',
  'esp32c5-CeCSizBL.js': 'sha384-LWYGviHcz1q8xMq+rBhgcOPSu9V6bB8VO5BMCIeTNIsIUtdzs2FCsTip6Tk37b1f',
  'esp32c6-DolpfL0e.js': 'sha384-IGupIjC1Vbm+EvBSu+rNvcYGQxssjhOw2APdj4cIGAdevBe86rTUYaO21v8RNywO',
  'esp32c61-C8HktcOt.js': 'sha384-ufZuP5/DSChHLVHLQ7MYes8HllWFxgbwL5y4C5gi9OXhaYHvdF6KEMOpv+iY/JeE',
  'esp32h2-L0n5WuSo.js': 'sha384-RU6vJZEaAJLviiv0938vlTobFJZHLEXoIG++VEHufT8z4evp3AeasaMzTemz2DMr',
  'esp32p4-MR0ikcda.js': 'sha384-xBu/wvVzwGAyBFMKgxeER3FJpFRbEy9ibHOmTdVa/k2B4khQPRq3pfmGZQYRCGpX',
  'esp32s2-w9SUkb7o.js': 'sha384-NNkQOYG+wsZHOXo+Yam3d6ks8/Bai0cN9MKjMU6U1PaxHjdOJC5ZqhDZcnQ2nL/N',
  'esp32s3-DQt_R3ZE.js': 'sha384-IcqNvrgnrdlIMX/N8Fb2+T/TGyZHbVAqp8iyoP9cH/E+VSlV6h3Uug3ykbyr1We6',
  'stub_flasher_32-BLbsWvxO.js': 'sha384-NwVOvN4wk0BwWgOx1vGDv+MQEVpiXoaexYjJg/MDYH4rXwzOFpTiPC4Ga+GHl1Z+',
  'stub_flasher_32c2-wLQhZItC.js': 'sha384-kWdvhLR37pGIIpanPvPbGanK2DxuRVrb0Dx4tOYWcL2SpD3OGuC04mKxDgKLF9nT',
  'stub_flasher_32c3-DmSvHQKL.js': 'sha384-CXQHSBDY3+uIPzPcDoj56lAdidofn2R9+kcq6RkXjgVSM/6ZizTnZY4/AJ0tUODp',
  'stub_flasher_32c5-D1WK4DyB.js': 'sha384-3RLmDq3JDolM+Vvwua7Kb5r99y3mw9ptmdAuGKoiXuiQWuSLfAqTtSDO0zryPZm2',
  'stub_flasher_32c6-ZuxjUVr4.js': 'sha384-195M9duTV1A72oX1BdPqPyF0KufvSvbkTOXlVLYg0eZSdx6DCcGwL38RSMxvsU3j',
  'stub_flasher_32c61-DeKkw9vN.js': 'sha384-ocwIY7vRGNYbB3pDHtXRXo9e1wPmTmJKEkN+xGCwIDSxrAcMxnqTeyzcfqYmC99I',
  'stub_flasher_32h2-CZ4EIL3w.js': 'sha384-Whdgv4c66UKEmSqtqwBnxgw56V2kaJd4NQ8gpghwzEuXDotvVxaDzcKQNyacCgNZ',
  'stub_flasher_32p4-CpHBYEwI.js': 'sha384-Ugb2U81Kg2CAS1mQ6avdnzyJ0c7ux8Qmyivx2E7RXmu1bFVZxk8JUENABFAX/YYU',
  'stub_flasher_32p4rc1-DyGqUAeZ.js': 'sha384-obLWbHzQko//5KcdaalAr28oxQcTdbFPIxFr56WrxPPhkCJiDF6mlwnYCtiYHNtz',
  'stub_flasher_32s2-CrsP1231.js': 'sha384-QKrtaJgMb+aG+s5+Kp6hO/mnIOYkYniXOuViJGg5flhCGHb4EEaeASBikr5hnhLp',
  'stub_flasher_32s3-CiJyd6Fk.js': 'sha384-RW8irjkoLiZ6a4u+YbIjMSDqrFlM01oByNK52WD4te9CvRiEh9+631+aOtT9w8uh',
  'stub_flasher_8266-CQFcqJ_a.js': 'sha384-xPe4ucyto336SN2lbHdv4M0V1RV8+oLpWoYiht75/O/r6N8sOYvIPbGne54oKxla',
};
const ewtTags = () => {
  const integrity = Object.fromEntries(Object.entries(EWT_SRI).map(([f, h]) => [EWT + f, h]));
  return `<script type="importmap">${JSON.stringify({ integrity })}</script>\n` +
    `<script type="module" src="${EWT}install-button.js" integrity="${EWT_SRI['install-button.js']}" crossorigin="anonymous"></script>`;
};

// ---------- Pages and assets ----------
rmSync(out, { recursive: true, force: true });
mkdirSync(out, { recursive: true });
for (const p of ['index.html', 'ar', 'assets']) cpSync(join(site, p), join(out, p), { recursive: true });
const copy = (from, to) => {
  const src = join(root, from);
  if (!existsSync(src)) fail(`missing ${from}`);
  mkdirSync(dirname(join(out, to)), { recursive: true });
  cpSync(src, join(out, to));
};
copy('web/src/fonts/keyra-sans.woff2', 'assets/keyra-sans.woff2');
copy('docs/brand/logo/icon-small.svg', 'favicon.svg');
copy('web/public/apple-touch-icon.png', 'apple-touch-icon.png');
copy('docs/brand/readme/social.png', 'img/social.png');
for (const n of ['hero-light', 'hero-dark', 'press', 'desk']) copy(`docs/images/${n}.jpg`, `img/${n}.jpg`);
for (const screen of ['vault', 'ready', 'typed', 'security']) {
  for (const v of ['', '-dark', '-en', '-en-dark']) copy(`web/screenshots/${screen}${v}.png`, `img/app/${screen}${v}.png`);
}
const previews = 'hardware/enclosure/previews';
if (existsSync(join(root, previews))) {
  for (const f of readdirSync(join(root, previews)).filter((f) => f.endsWith('.jpg'))) copy(`${previews}/${f}`, `img/enclosure/${f}`);
} else {
  console.warn(`warning: ${previews} is missing; the enclosure images will not load`);
}

// ---------- Boards ----------
// firmware/boards/boards.json lists the boards; a board is installable when its app is in the release.
const boardsPath = join(root, 'firmware/boards/boards.json');
const boards = existsSync(boardsPath) ? JSON.parse(readFileSync(boardsPath, 'utf8')) : [{
  id: 'devkitc1', name: 'ESP32-S3-DevKitC-1', vendor: 'Espressif',
  url: 'https://www.espressif.com/en/dev-board/esp32-s3-devkitc-1-en', flash_mb: 8, psram: 'optional',
  button: 'GPIO0 (BOOT)', led: 'WS2812 on GPIO38', usb: 'native USB port', tested: true, default: true, notes: 'Reference board.',
}];
if (boards.filter((b) => b.default).length !== 1) fail('boards.json: exactly one default board');
const appFile = (b) => (b.default ? 'keyra-firmware.bin' : `keyra-firmware-${b.id}.bin`);

// ---------- Firmware and manifests ----------
const tag = args.release ? args.version : null;
const available = new Set();
if (args.release) {
  const rel = resolve(args.release);
  const offsetsText = readFileSync(join(rel, 'flash-offsets.txt'), 'utf8');
  const parts = [...offsetsText.matchAll(/^\s*(0x[0-9a-f]+)\s+(\S+\.bin)\s*$/gim)].map((m) => ({ offset: Number(m[1]), file: m[2] }));
  if (!parts.some((p) => p.file === 'keyra-firmware.bin')) fail('flash-offsets.txt: no keyra-firmware.bin line');
  if (new Set(parts.map((p) => p.offset)).size !== parts.length) fail('flash-offsets.txt: duplicate offsets');

  const sums = existsSync(join(rel, 'SHA256SUMS'))
    ? Object.fromEntries(readFileSync(join(rel, 'SHA256SUMS'), 'utf8').trim().split('\n').map((l) => {
      const [hash, name] = l.trim().split(/\s+/);
      return [name.replace(/^\.\//, ''), hash];
    }))
    : null;
  if (!sums) console.warn('warning: no SHA256SUMS in the release; binaries not checked');
  const dir = join(out, 'firmware', tag);
  mkdirSync(dir, { recursive: true });
  const take = (file) => {
    const buf = readFileSync(join(rel, file));
    if (sums) {
      const want = sums[file] ?? fail(`SHA256SUMS has no ${file}`);
      if (createHash('sha256').update(buf).digest('hex') !== want) fail(`${file} does not match SHA256SUMS`);
    }
    writeFileSync(join(dir, file), buf);
  };
  for (const p of parts) if (p.file !== 'keyra-firmware.bin') take(p.file);
  if (sums) cpSync(join(rel, 'SHA256SUMS'), join(dir, 'SHA256SUMS'));

  for (const b of boards) {
    if (!existsSync(join(rel, appFile(b)))) continue;
    take(appFile(b));
    available.add(b.id);
    const manifest = {
      name: `Keyra for ${b.name}`,
      version: tag.slice(1),
      // Always a clean install: ESP Web Tools erases the flash first (the page warns before this).
      new_install_prompt_erase: false,
      new_install_improv_wait_time: 0,
      builds: [{
        chipFamily: 'ESP32-S3',
        parts: parts.map((p) => ({ path: `${tag}/${p.file === 'keyra-firmware.bin' ? appFile(b) : p.file}`, offset: p.offset })),
      }],
    };
    for (const p of manifest.builds[0].parts) if (!existsSync(join(out, 'firmware', p.path))) fail(`manifest ${b.id}: missing ${p.path}`);
    writeFileSync(join(out, 'firmware', `${b.id}.json`), JSON.stringify(manifest, null, 2) + '\n');
  }
  if (!available.size) fail('the release has no app for any board in boards.json');
}

// ---------- Fill the pages ----------
const esc = (s) => String(s).replace(/[&<>"]/g, (c) => ({ '&': '&amp;', '<': '&lt;', '>': '&gt;', '"': '&quot;' })[c]);
const T = {
  en: { tested: 'Tested on hardware', untested: 'Not yet tested', missing: 'Not in this release yet', none: 'No firmware in this preview build' },
  ar: { tested: 'مُجرَّب على العتاد', untested: 'لم يُجرَّب بعد', missing: 'ليس في هذا الإصدار بعد', none: 'لا برنامج في هذه المعاينة' },
};
const check = '<svg viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2.25" stroke-linecap="round" stroke-linejoin="round" aria-hidden="true"><path d="M5 12.5l4.5 4.5L19 7.5"/></svg>';
const tagHtml = (b, t) => (b.tested
  ? `<span class="tag tag-ok">${check}${t.tested}</span>`
  : `<span class="tag tag-muted">${t.untested}</span>`);
const firstAvailable = boards.find((b) => b.default && available.has(b.id)) ?? boards.find((b) => available.has(b.id));

const fill = (file, lang, prefix) => {
  const t = T[lang];
  const table = boards.map((b) => `            <tr><th scope="row"><a href="${esc(b.url)}">${esc(b.name)}</a><small>${esc(b.vendor)}</small></th>` +
    `<td><span class="ltr">${esc(b.flash_mb)} MB</span></td><td><bdi>${esc(b.psram)}</bdi></td><td><bdi>${esc(b.button)}</bdi></td><td><bdi>${esc(b.led)}</bdi></td><td>${tagHtml(b, t)}</td></tr>`).join('\n');
  const picker = boards.map((b) => {
    const ok = available.has(b.id);
    const meta = ok ? `<bdi>${esc(b.vendor)} · ${esc(b.led)}</bdi>` : (tag ? t.missing : t.none);
    return `          <label class="board-opt"><input type="radio" name="board" value="${b.id}" data-manifest="${prefix}firmware/${b.id}.json" data-name="${esc(b.name)}"` +
      `${b === firstAvailable ? ' checked' : ''}${ok ? '' : ' disabled'}><span class="board-main"><span class="board-name">${esc(b.name)}</span>` +
      `<span class="board-meta">${meta}</span></span>${tagHtml(b, t)}</label>`;
  }).join('\n');
  const path = join(out, file);
  let html = readFileSync(path, 'utf8');
  const swap = (marker, value) => {
    if (!html.includes(marker)) fail(`${file}: no ${marker}`);
    html = html.replaceAll(marker, value);
  };
  swap('<!-- @esp-web-tools -->', ewtTags());
  swap('<!-- @boards:table -->', table);
  swap('<!-- @boards:picker -->', picker);
  swap('{{version}}', tag ? tag.slice(1) : 'preview');
  swap('{{release_url}}', tag ? `https://github.com/hasanalaaa/keyra/releases/tag/${tag}` : 'https://github.com/hasanalaaa/keyra/releases/latest');
  if (/\{\{|<!-- @/.test(html)) fail(`${file}: unfilled placeholder`);
  writeFileSync(path, html);
};
fill('index.html', 'en', '');
fill('ar/index.html', 'ar', '../');

console.log(`built ${out}${tag ? ` with ${tag}: ${[...available].join(', ')}` : ' (no firmware)'}`);
