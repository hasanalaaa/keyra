// Builds the extension for each browser: dist/chrome (Chrome, Edge, Brave, Opera, Vivaldi) and
// dist/firefox. Same code, one manifest per browser. `node build.mjs --e2e` also writes
// dist/chrome-e2e, whose manifest already grants the test hosts (no permission prompt in a test).
import { build } from 'esbuild';
import { cpSync, mkdirSync, readFileSync, rmSync, writeFileSync } from 'node:fs';
import { fileURLToPath, pathToFileURL } from 'node:url';

const here = (p) => fileURLToPath(new URL(p, import.meta.url));
const pkg = JSON.parse(readFileSync(here('package.json'), 'utf8'));
const e2e = process.argv.includes('--e2e');

// Locales come from src/strings.ts (one source for both languages).
const tmp = here('dist/.strings.mjs');
await build({ entryPoints: [here('src/strings.ts')], outfile: tmp, format: 'esm', bundle: true, logLevel: 'silent' });
const { en, ar } = await import(pathToFileURL(tmp).href);
rmSync(tmp);

function manifest(target) {
  const m = {
    manifest_version: 3,
    name: '__MSG_extName__',
    short_name: 'Keyra',
    description: '__MSG_extDescription__',
    version: pkg.version,
    default_locale: 'en',
    icons: { 16: 'icons/icon-16.png', 32: 'icons/icon-32.png', 48: 'icons/icon-48.png', 128: 'icons/icon-128.png' },
    action: { default_popup: 'popup.html', default_title: '__MSG_extName__', default_icon: { 16: 'icons/icon-16.png', 32: 'icons/icon-32.png' } },
    // storage: address, token, settings, the pending save card. scripting: the pairing listener,
    // injected only into the Keyra tab the extension opened.
    permissions: ['storage', 'scripting'],
    // Asked at run time for Keyra's address only (SPEC §9.4 pairing step 1).
    optional_host_permissions: ['http://*/*', 'https://*/*'],
    content_scripts: [{ matches: ['http://*/*', 'https://*/*'], js: ['content.js'], run_at: 'document_idle' }],
  };
  if (target === 'firefox') {
    m.background = { scripts: ['background.js'] };
    m.browser_specific_settings = { gecko: { id: 'companion@keyra', strict_min_version: '128.0', data_collection_permissions: { required: ['none'] } } };
  } else {
    m.background = { service_worker: 'background.js' };
    m.minimum_chrome_version = '116';
  }
  if (target === 'chrome-e2e') m.host_permissions = ['http://keyra.test/*'];
  return m;
}

const targets = ['chrome', 'firefox', ...(e2e ? ['chrome-e2e'] : [])];
for (const target of targets) {
  const out = here(`dist/${target}/`);
  rmSync(out, { recursive: true, force: true });
  mkdirSync(out, { recursive: true });
  await build({
    entryPoints: { background: here('src/background.ts'), content: here('src/content/content.ts'), popup: here('src/popup/popup.ts') },
    outdir: out,
    bundle: true,
    format: 'iife',
    target: ['chrome116', 'firefox128'],
    minify: true,
    legalComments: 'none',
    logLevel: 'warning',
  });
  cpSync(here('src/popup/popup.html'), `${out}popup.html`);
  cpSync(here('src/popup/popup.css'), `${out}popup.css`);
  cpSync(here('icons'), `${out}icons`, { recursive: true });
  mkdirSync(`${out}fonts`);
  // Readex Pro (SIL OFL 1.1), the same subset the web app embeds (DESIGN §3).
  cpSync(here('../web/src/fonts/keyra-sans.woff2'), `${out}fonts/keyra-sans.woff2`);
  for (const [lang, strings] of [['en', en], ['ar', ar]]) {
    mkdirSync(`${out}_locales/${lang}`, { recursive: true });
    const messages = Object.fromEntries(Object.entries(strings).map(([k, v]) => [k, { message: v }]));
    writeFileSync(`${out}_locales/${lang}/messages.json`, JSON.stringify(messages, null, 1));
  }
  writeFileSync(`${out}manifest.json`, JSON.stringify(manifest(target), null, 2));
}
console.log(`built ${targets.map((t) => `dist/${t}`).join(', ')} (v${pkg.version})`);
