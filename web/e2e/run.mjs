// End-to-end run against the mock (built app, device CSP): the full first-run and daily flows,
// plus README screenshots in web/screenshots/. Run `npm run build` first, then `npm run e2e`.
//
// Screenshot names: <screen>[-desktop][-en][-dark].png — the bare name is phone (390×844), Arabic, light.
import { chromium } from 'playwright';
import { execFileSync, spawn } from 'node:child_process';
import { existsSync, mkdirSync, readFileSync, readdirSync } from 'node:fs';
import { fileURLToPath } from 'node:url';

const WEB = fileURLToPath(new URL('..', import.meta.url));
const SHOTS = fileURLToPath(new URL('../screenshots/', import.meta.url));
const PASS = 'keyra demo vault';
const NEW_PASS = 'my quiet blue key 2026';

if (!existsSync(`${WEB}dist/index.html`)) {
  console.error('dist/ is missing: run `npm run build` first');
  process.exit(1);
}
mkdirSync(SHOTS, { recursive: true });

// ---------- mock processes ----------

const mocks = [];
async function startMock(port, env = {}) {
  const p = spawn(process.execPath, ['mock/server.mjs'], { cwd: WEB, env: { ...process.env, PORT: String(port), ...env }, stdio: ['ignore', 'pipe', 'inherit'] });
  mocks.push(p);
  await new Promise((resolve, reject) => {
    p.stdout.on('data', (d) => String(d).includes('Keyra mock on') && resolve());
    p.on('exit', (code) => reject(new Error(`mock exited (${code})`)));
  });
  return `http://localhost:${port}`;
}

async function button(base, press = 'short') {
  const r = await fetch(`${base}/__mock/button`, { method: 'POST', body: JSON.stringify({ press }) });
  return (await r.json()).result;
}

// ---------- browser helpers ----------

const PHONE = { viewport: { width: 390, height: 844 }, deviceScaleFactor: 2, isMobile: true, hasTouch: true };
const DESKTOP = { viewport: { width: 1280, height: 800 }, deviceScaleFactor: 1 };

let browser;
const errors = [];

async function open(base, { desktop = false, lang = 'ar', dark = false, a2hs = false } = {}) {
  const ctx = await browser.newContext({ ...(desktop ? DESKTOP : PHONE), colorScheme: dark ? 'dark' : 'light', locale: lang === 'ar' ? 'ar-IQ' : 'en-US', acceptDownloads: true });
  await ctx.addInitScript(
    ([l, t, hint]) => {
      localStorage.setItem('keyra.lang', l);
      localStorage.setItem('keyra.theme', t);
      if (!hint) localStorage.setItem('keyra.a2hs', 'never');
    },
    [lang, dark ? 'dark' : 'light', a2hs],
  );
  const page = await ctx.newPage();
  page.on('console', (m) => (m.type() === 'error' || m.type() === 'warning') && errors.push(`[console ${m.type()}] ${m.text()}`));
  page.on('pageerror', (e) => errors.push(`[pageerror] ${e.message}`));
  await page.goto(base + '/');
  await page.evaluate(() => document.fonts.ready);
  const tag = `${desktop ? '-desktop' : ''}${lang === 'en' ? '-en' : ''}${dark ? '-dark' : ''}`;
  return { ctx, page, tag };
}

async function shot(page, name, settle = 450) {
  await page.waitForTimeout(settle); // let springs/sheets finish; the ring drain keeps running on purpose
  await page.screenshot({ path: `${SHOTS}${name}.png` });
  console.log(`  ✓ ${name}.png`);
}

async function unlockUi(page, pass = PASS) {
  await page.locator('input[type=password]').fill(pass);
  await page.locator('button[type=submit]').click();
  await page.locator('.list-pane').waitFor();
  await page.locator('.acc-row, .empty').first().waitFor();
}

const row = (page, title) => page.locator('.acc-row', { hasText: title }).first();

function check(cond, msg) {
  if (!cond) throw new Error(`check failed: ${msg}`);
}

// ---------- flow 1: first run → daily use (phone, Arabic) ----------

async function firstRunFlow(base, opts) {
  const { ctx, page, tag } = await open(base, opts);
  console.log(`first run ${tag || '(phone, ar, light)'}`);
  await page.locator('.welcome').waitFor();
  await shot(page, `welcome${tag}`);
  await page.locator('.hero-foot .btn-primary').click();

  // Step 1: passphrase
  const pw = page.locator('input[type=password]');
  await pw.nth(0).fill(NEW_PASS);
  await pw.nth(1).fill(NEW_PASS);
  await shot(page, `setup-passphrase${tag}`, 300);
  await page.locator('.sticky-cta .btn-primary').click();

  // Step 2: Wi-Fi password (generated, revealed)
  const wifiInput = page.locator('.form input').first();
  await wifiInput.waitFor();
  const wifi = await wifiInput.inputValue();
  check(/^[A-Za-z0-9]{4}-[A-Za-z0-9]{4}-[A-Za-z0-9]{4}$/.test(wifi), `generated Wi-Fi password ${wifi}`);
  await shot(page, `setup-wifi${tag}`, 300);
  await page.locator('.sticky-cta .btn-primary').click();

  // Step 3: press the button
  await page.locator('.ready-ready').waitFor();
  await shot(page, `setup-button${tag}`, 1200);
  check((await button(base)) === 'approved setup', 'button approves setup');
  await page.locator('.ready-typed').waitFor({ timeout: 5000 });
  await shot(page, `setup-done${tag}`, 700);

  // The link never drops in the mock, so the app unlocks on its own after ~8 s.
  await page.locator('.list-pane').waitFor({ timeout: 15000 });
  await page.locator('.empty').waitFor();
  await shot(page, `vault-empty${tag}`);

  // Add an account
  await page.locator('.empty .btn-primary').click();
  const form = page.locator('.edit-form');
  await form.waitFor();
  const inputs = form.locator('input');
  await inputs.nth(0).fill('GitHub');
  await inputs.nth(1).fill('github.com');
  await inputs.nth(2).fill('hasanalaaa');
  await form.locator('input[type=password]').fill('Tigris-River-42!');
  await shot(page, `edit${tag}`);
  await page.locator('.save-btn').click();
  await row(page, 'GitHub').waitFor();
  await page.locator('.layer .sheet').waitFor({ state: 'detached' });

  // Account → Both → Ready → button → Typed ✓
  await row(page, 'GitHub').click();
  await page.locator('.act-both').waitFor();
  await page.locator('.act-both').click();
  await page.locator('.ready-ready').waitFor();
  check((await button(base)).startsWith('typing both'), 'button types');
  await page.locator('.ready-typed').waitFor({ timeout: 5000 });
  await page.locator('.actions').waitFor({ timeout: 5000 }); // collapses back after the dwell
  if (!opts.desktop) {
    await page.keyboard.press('Escape');
    await page.locator('.layer .sheet').waitFor({ state: 'detached' });
  }

  // Import a CSV (Chrome format)
  await page.evaluate(() => (location.hash = '#/import'));
  await page.locator('.source-card').nth(1).click();
  await page.locator('input[type=file]').setInputFiles({
    name: 'Chrome Passwords.csv',
    mimeType: 'text/csv',
    buffer: Buffer.from(
      '﻿name,url,username,password,note\r\n' +
        'GitHub,github.com,hasanalaaa,Tigris-River-42!,\r\n' + // duplicate of the one added above
        'زين العراق,https://iq.zain.com,07801234567,Zain*Mobile88,\r\n' +
        'Netflix,https://netflix.com,family@hasan.iq,"Movie, Night",\r\n' +
        'Steam,https://store.steampowered.com,hasan_gamer,St3am!Valve,"two\nlines"\r\n',
    ),
  });
  await page.locator('.summary').waitFor();
  await page.locator('.import .btn-primary').click();
  await page.locator('.import .notice').waitFor();
  const imported = await page.locator('.import .t2').textContent();
  check(/3/.test(imported ?? ''), `imported 3 (got "${imported}")`);
  await page.locator('.import .btn-primary').click();
  await page.locator('.layer .sheet').waitFor({ state: 'detached' });
  check((await page.locator('.acc-row').count()) >= 4, 'list shows imported rows');

  // Backup download
  await page.evaluate(() => (location.hash = '#/backup'));
  await page.locator('.backup input[type=password]').first().fill('backup passphrase 2026');
  const [download] = await Promise.all([page.waitForEvent('download'), page.locator('.backup .btn-primary').first().click()]);
  check(/^keyra-backup-\d{8}\.json$/.test(download.suggestedFilename()), `backup filename ${download.suggestedFilename()}`);
  const file = JSON.parse(readFileSync(await download.path(), 'utf8'));
  check(file.format === 'keyra-backup' && file.v === 1, 'backup envelope');
  console.log(`  ✓ flow passed (backup ${download.suggestedFilename()})`);
  await ctx.close();
}

// ---------- flow 2: screenshots of the seeded vault ----------

async function vaultShots(base, opts) {
  const { ctx, page, tag } = await open(base, opts);
  console.log(`vault ${tag || '(phone, ar, light)'}`);
  await page.locator('input[type=password]').waitFor();
  await shot(page, `unlock${tag}`);
  await unlockUi(page);
  await page.locator('.count').waitFor();
  await shot(page, `vault${tag}`);

  await row(page, 'GitHub').click();
  await page.locator('.code-card .code').filter({ hasText: /\d/ }).waitFor();
  await shot(page, `account${tag}`);

  await page.locator('.act-both').click();
  await page.locator('.ready-ready').waitFor();
  await shot(page, `ready${tag}`, 2200);
  await button(base);
  await page.locator('.ready-typed').waitFor({ timeout: 5000 });
  await shot(page, `typed${tag}`, 650);
  await page.locator('.actions').waitFor({ timeout: 5000 });

  if (!opts.desktop) {
    await page.keyboard.press('Escape');
    await page.locator('.layer .sheet').waitFor({ state: 'detached' });
  }
  await page.locator('.search input').fill('بنك');
  await shot(page, `search${tag}`, 300);
  await page.locator('.search input').fill('');

  await page.evaluate(() => (location.hash = '#/settings'));
  await page.locator('.settings .seg').first().waitFor();
  await shot(page, `settings${tag}`);
  await page.evaluate(() => (location.hash = '#/import'));
  await page.locator('.source-card').first().waitFor();
  await shot(page, `import${tag}`);
  await page.evaluate(() => (location.hash = '#/'));
  await page.locator('.layer .sheet').waitFor({ state: 'detached' });

  await page.locator('.top-bar .icon-btn').last().click(); // lock
  await page.locator('.locked-glyph').waitFor();
  await shot(page, `locked${tag}`);
  await ctx.close();
}

// ---------- flow 3: Bluetooth — pair a device, type into it, forget it ----------

async function bleFlow(base, opts) {
  const { ctx, page, tag } = await open(base, opts);
  console.log(`bluetooth ${tag || '(phone, ar, light)'}`);
  await page.locator('input[type=password]').waitFor();
  await unlockUi(page);
  await page.evaluate(() => (location.hash = '#/settings'));
  const section = page.locator('section.group:has(#bluetooth)');
  await section.locator('.bond-row').first().waitFor();
  await section.scrollIntoViewIfNeeded();
  await shot(page, `bluetooth${tag}`);

  // Pair: press the button, then the phone/computer picks Keyra from its list.
  await section.locator('.pair-row').click();
  await page.locator('.ready-ready').waitFor();
  check((await button(base)) === 'approved ble_pair', 'button opens the pairing window');
  await page.locator('.ready-ready .ready-title', { hasText: /[“«]Keyra[”»]/ }).waitFor({ timeout: 5000 });
  await shot(page, `ble-pair${tag}`, 900);
  const r = await fetch(`${base}/__mock/ble`, { method: 'POST', body: JSON.stringify({ pair: "Hasan's iPad" }) });
  check((await r.json()).paired === true, 'mock device pairs inside the window');
  await page.locator('.ready-typed').waitFor({ timeout: 5000 });
  await page.locator('.layer .sheet').waitFor({ state: 'detached', timeout: 6000 });
  await section.locator('.bond-row', { hasText: "Hasan's iPad" }).waitFor();

  // Unplugged, so Auto types into the iPad, and Ready says so.
  await fetch(`${base}/__mock/usb`, { method: 'POST', body: JSON.stringify({ usb: false }) });
  await page.evaluate(() => (location.hash = '#/'));
  await page.locator('.top-bar .chip', { hasText: /Bluetooth|بلوتوث/ }).waitFor({ timeout: 8000 });
  await row(page, 'GitHub').click();
  await page.locator('.act-both').click();
  await page.locator('.ready-ready .notice', { hasText: "Hasan's iPad" }).waitFor({ timeout: 5000 });
  await shot(page, `ready-ble${tag}`, 1200);
  check((await button(base)).startsWith('typing both'), 'button types over Bluetooth');
  await page.locator('.ready-typed').waitFor({ timeout: 5000 });
  await page.locator('.actions').waitFor({ timeout: 5000 });
  await page.keyboard.press('Escape');
  await page.locator('.layer .sheet').waitFor({ state: 'detached' });

  // Forget it again.
  await page.evaluate(() => (location.hash = '#/settings'));
  const ipad = section.locator('.bond-row', { hasText: "Hasan's iPad" });
  await ipad.waitFor();
  await page.waitForTimeout(400); // let the page transition finish before opening the alert
  await ipad.locator('.icon-btn').click();
  await page.locator('.alert-actions button').first().click();
  await section.locator('.bond-row', { hasText: "Hasan's iPad" }).waitFor({ state: 'detached' });
  console.log('  ✓ Bluetooth flow passed');
  await ctx.close();
}

/** Shrinks the PNGs for the README when pngquant is on PATH (they are committed). */
function quantizeShots() {
  const files = readdirSync(SHOTS).filter((f) => f.endsWith('.png')).map((f) => SHOTS + f);
  try {
    execFileSync('pngquant', ['--quality=80-95', '--speed=1', '--strip', '--skip-if-larger', '--force', '--ext', '.png', ...files]);
  } catch (e) {
    // 99 = a file would not get smaller (kept as is); ENOENT = no pngquant.
    if (e.status !== 99 && e.code !== 'ENOENT') throw e;
    if (e.code === 'ENOENT') console.warn('pngquant not found: screenshots left unquantized');
  }
}

// ---------- run ----------

const t0 = Date.now();
try {
  browser = await chromium.launch();
  const seeded = await startMock(8791);
  const fresh1 = await startMock(8792, { MOCK_FRESH: '1' });
  const fresh2 = await startMock(8793, { MOCK_FRESH: '1' });

  await firstRunFlow(fresh1, {});
  await firstRunFlow(fresh2, { desktop: true, lang: 'en', dark: true });

  for (const opts of [
    {},
    { lang: 'en' },
    { dark: true },
    { lang: 'en', dark: true },
    { desktop: true },
    { desktop: true, lang: 'en' },
    { desktop: true, dark: true },
  ]) {
    await vaultShots(seeded, opts);
  }

  await bleFlow(await startMock(8794), {});
  await bleFlow(await startMock(8795), { lang: 'en' });

  quantizeShots();
  if (errors.length) {
    console.error(`\n${errors.length} browser console error(s):\n${errors.join('\n')}`);
    process.exitCode = 1;
  } else console.log(`\ne2e passed in ${((Date.now() - t0) / 1000).toFixed(0)} s`);
} catch (e) {
  console.error(e);
  process.exitCode = 1;
} finally {
  await browser?.close();
  for (const m of mocks) m.kill();
}
