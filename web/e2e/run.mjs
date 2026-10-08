// End-to-end run against the mock (built app, device CSP): the full first-run and daily flows,
// plus README screenshots in web/screenshots/. Run `npm run build` first, then `npm run e2e`.
//
// Screenshot names: <screen>[-desktop][-en][-dark].png — the bare name is phone (390×844), Arabic, light.
import { chromium } from 'playwright';
import { execFileSync, spawn } from 'node:child_process';
import { existsSync, mkdirSync, readFileSync, readdirSync } from 'node:fs';
import { createServer as createNetServer } from 'node:net';
import { fileURLToPath } from 'node:url';
import { migrationUri, qrPng } from './fixtures.mjs';

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

/** A port nobody is listening on, so parallel e2e runs on one machine never collide. */
function freePort() {
  return new Promise((resolve, reject) => {
    const srv = createNetServer();
    srv.on('error', reject);
    srv.listen(0, () => {
      const { port } = srv.address();
      srv.close(() => resolve(port));
    });
  });
}

async function startMock(env = {}) {
  const port = await freePort();
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

/** GET with the browser's session cookie. */
async function authed(ctx, url) {
  return fetch(url, { headers: { cookie: (await ctx.cookies()).map((c) => `${c.name}=${c.value}`).join('; ') } });
}

async function presence(ctx, base) {
  return (await (await authed(ctx, `${base}/api/state`)).json()).presence;
}

async function waitFor(cond, msg, ms = 5000) {
  for (const end = Date.now() + ms; Date.now() < end; await new Promise((r) => setTimeout(r, 100))) if (await cond()) return;
  throw new Error(`timed out: ${msg}`);
}

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

  // 2FA from a QR photo: a plain otpauth:// QR in the Edit form …
  await page.evaluate(() => (location.hash = '#/new'));
  await page.locator('.edit-form').waitFor();
  await page.locator('.edit-form input[type=file]').setInputFiles({
    name: 'qr.png',
    mimeType: 'image/png',
    buffer: await qrPng('otpauth://totp/Acme%20Cloud:dev%40acme.io?secret=GEZDGNBVGY3TQOJQ&issuer=Acme%20Cloud&digits=8'),
  });
  await page.waitForFunction(() => document.querySelector('.mono-input')?.value.startsWith('otpauth://totp/Acme%20Cloud'));
  const formInputs = page.locator('.edit-form input');
  check((await formInputs.nth(0).inputValue()) === 'Acme Cloud', 'QR fills the empty name');
  check((await formInputs.nth(2).inputValue()) === 'dev@acme.io', 'QR fills the empty user name');
  check((await page.locator('.mono-input').inputValue()).includes('digits=8'), 'QR keeps 8 digits');
  await page.locator('.edit-form .file-btn').scrollIntoViewIfNeeded();
  await shot(page, `edit-qr${tag}`, 2800); // after the toast has gone
  await page.locator('.save-btn').click();
  await row(page, 'Acme Cloud').waitFor();
  await page.locator('.layer .sheet').waitFor({ state: 'detached' });

  // … and a Google Authenticator export QR (one existing account to attach to, one new, one HOTP that is skipped).
  await page.evaluate(() => (location.hash = '#/import'));
  await page.locator('.source-wide').click();
  await page.locator('.import input[type=file]').setInputFiles({
    name: 'export.png',
    mimeType: 'image/png',
    buffer: await qrPng(
      migrationUri([
        { secret: Buffer.from('12345678901234567890'), name: 'GitHub:hasanalaaa', issuer: 'GitHub' },
        { secret: Buffer.from('linear-secret-key!'), name: 'Linear:dev@acme.io', issuer: 'Linear', algorithm: 2 },
        { secret: Buffer.from('counter-based-key'), name: 'Old:hotp', issuer: 'Old', type: 1 },
      ]),
    ),
  });
  await page.locator('.qr-list').waitFor();
  check((await page.locator('.qr-list li').count()) === 2, 'migration preview lists the 2 TOTP accounts');
  check(await page.locator('.seg').isVisible(), 'attach/new choice is offered when an account matches');
  await shot(page, `import-qr${tag}`);
  await page.locator('.import .btn-primary').click();
  await page.locator('.import .notice').waitFor();
  const qrDone = (await page.locator('.import .t2').allTextContents()).join(' | ');
  check(/1/.test(qrDone) && (qrDone.match(/\|/g) ?? []).length === 1, `QR import added 1 and attached 1 (got "${qrDone}")`);
  await page.locator('.import .btn-primary').click();
  await page.locator('.layer .sheet').waitFor({ state: 'detached' });
  await row(page, 'Linear').waitFor();
  await row(page, 'GitHub').click();
  await page.locator('.code-card .code').waitFor();
  await page.keyboard.press('Escape');

  // Backup download
  await page.evaluate(() => (location.hash = '#/backup'));
  await page.locator('.backup input[type=password]').first().fill('backup passphrase 2026');
  // A backup is handed over only after a press on Keyra (SPEC §12.3).
  const [download] = await Promise.all([
    page.waitForEvent('download'),
    (async () => {
      await page.locator('.backup .btn-primary').first().click();
      await page.waitForTimeout(500);
      check((await button(base)) === 'approved backup', 'button approves the backup');
    })(),
  ]);
  check(/^keyra-backup-\d{8}\.json$/.test(download.suggestedFilename()), `backup filename ${download.suggestedFilename()}`);
  const file = JSON.parse(readFileSync(await download.path(), 'utf8'));
  check(file.format === 'keyra-backup' && file.v === 2, 'backup envelope');
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

  // On demand: once the pairing link is let go, typing connects just for the action.
  const mock = (body) => fetch(`${base}/__mock/ble`, { method: 'POST', body: JSON.stringify(body) });
  await mock({ connected: false });
  await mock({ autoConnect: false }); // connect by hand below, so the "Connecting…" state can be seen
  await fetch(`${base}/__mock/usb`, { method: 'POST', body: JSON.stringify({ usb: false }) });
  // Unplugging the computer it was used from locks Keyra (SPEC §12.4) and says why.
  await page.locator('button', { hasText: /^(Unlock|فتح)$/ }).click({ timeout: 8000 });
  await page.evaluate(() => (location.hash = '#/'));
  await unlockUi(page);
  await page.locator('.top-bar .chip', { hasText: /Bluetooth|بلوتوث/ }).waitFor({ timeout: 8000 });
  await row(page, 'GitHub').click();
  // Pick the iPad in the account sheet's "Type into" picker (remembered per browser).
  await page.locator('.target-picker button', { hasText: "Hasan's iPad" }).click();
  check((await page.evaluate(() => localStorage.getItem('keyra.target'))) !== null, 'target remembered');
  await page.locator('.act-both').click();
  await page.locator('.ready-ready .ready-title', { hasText: "Hasan's iPad" }).waitFor({ timeout: 5000 });
  await shot(page, `ready-connecting${tag}`, 1200);
  check((await button(base)) === 'connecting (blink)', 'a press before the host connects does nothing');
  await mock({ connected: true });
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

// ---------- flow 3: home Wi‑Fi + trusted browser (SPEC §8.2), phone, English ----------

async function homeFlow(base, opts) {
  const { ctx, page, tag } = await open(base, opts);
  console.log(`home Wi‑Fi ${tag}`);
  // Through the home network an unknown browser must be trusted with the button first.
  await page.locator('input[type=password]').fill(PASS);
  await page.locator('button[type=submit]').click();
  await page.locator('.ready-ready').waitFor();
  check((await page.locator('.ready-title').textContent())?.includes('trust this browser'), 'trust prompt shown');
  await shot(page, `unlock-trust${tag}`, 1200);
  check((await button(base)) === 'approved trust_browser', 'button approves trust');
  await page.locator('.list-pane').waitFor({ timeout: 10000 }); // retried unlock succeeds

  await page.evaluate(() => (location.hash = '#/settings'));
  await page.locator('.nav-row', { hasText: /Home Wi.Fi/ }).click();
  await page.getByRole('switch', { name: /Use home Wi.Fi/ }).click(); // off → pick a network
  await page.locator('.net-row', { hasText: 'Al-Rashid Home' }).first().waitFor({ timeout: 8000 });
  check(await page.locator('.net-row', { hasText: 'Cafe Baghdad Free' }).isDisabled(), 'open network not joinable');
  await shot(page, `home-wifi-pick${tag}`);
  await page.locator('.net-row', { hasText: 'Al-Rashid Home' }).first().click();
  await page.locator('input[type=password]').fill('home-secret-42');
  await page.locator('form button[type=submit]').click();
  await page.locator('.ready-ready').waitFor();
  check((await button(base)) === 'approved home_wifi', 'button approves home_wifi');
  await page.locator('.home-status .chip-ok').waitFor({ timeout: 10000 });
  await page.locator('.home-hint').waitFor();
  await shot(page, `home-wifi${tag}`);
  await page.keyboard.press('Escape');

  await page.locator('.nav-row', { hasText: 'Trusted browsers' }).click();
  await page.locator('.trusted-row .chip-accent').waitFor();
  await shot(page, `trusted${tag}`);
  console.log('  ✓ flow passed');
  await ctx.close();
}

// ---------- flow 5: Settings → Passkeys (docs/FIDO.md): list and delete ----------

// ---------- Password health (SPEC §13): flags only, accounts open from the list ----------

async function healthFlow(base, opts) {
  const { ctx, page, tag } = await open(base, opts);
  console.log(`health ${tag || '(phone, ar, light)'}`);
  await page.locator('input[type=password]').waitFor();
  await unlockUi(page);
  await page.evaluate(() => (location.hash = '#/settings'));
  await page.locator('.settings .seg').first().waitFor();
  await page.locator('.nav-row', { hasText: opts.lang === 'en' ? 'Password health' : 'صحة كلمات السر' }).click();
  await page.locator('.health-summary').waitFor();
  // The seed: Amazon and Dropbox share a password, Router's is weak, Zain's is over a year old.
  check((await page.locator('.health .acc-row').count()) === 4, 'four flagged accounts listed');
  check(/4/.test((await page.locator('.health-summary strong').textContent()) ?? ''), 'summary counts 4 accounts');
  await shot(page, `health${tag}`);
  // Change every password (SPEC §13.1): start, everything is listed, end with a confirmation.
  await page.locator('.health .btn', { hasText: opts.lang === 'en' ? 'Start' : 'ابدأ' }).click();
  await page.locator('.alert .btn-primary').click();
  await page.locator('.rotate-card').waitFor();
  check((await page.locator('.rotate .acc-row').count()) === 24, 'every account with a password is on the list');
  await page.locator('.rotate-card').scrollIntoViewIfNeeded();
  await shot(page, `health-rotate${tag}`);
  await page.locator('.rotate > .btn').click();
  await page.locator('.alert .btn-danger-confirm').click();
  await page.locator('.rotate-card').waitFor({ state: 'detached' });
  await page.locator('.health .acc-row', { hasText: 'Router' }).first().click();
  await page.locator('.details').waitFor();
  check(page.url().includes('#/a/'), 'a flagged account opens');
  console.log('  ✓ health flow passed');
  await ctx.close();
}

// ---------- Activity (SPEC §15): a wrong guess is reported after the next unlock and logged ----------

async function activityFlow(base, opts) {
  const { ctx, page, tag } = await open(base, opts);
  console.log(`activity ${tag || '(phone, ar, light)'}`);
  await page.locator('input[type=password]').waitFor();
  const before = errors.length;
  await page.locator('input[type=password]').fill('not the passphrase');
  await page.locator('button[type=submit]').click();
  await page.locator('.field-error').first().waitFor({ timeout: 8000 });
  // The browser logs the deliberate wrong guess (401); that one is expected.
  for (let i = errors.length - 1; i >= before; i--) if (/status of 401/.test(errors[i])) errors.splice(i, 1);
  await unlockUi(page);
  const warn = page.locator('.toast-error');
  await warn.waitFor({ timeout: 5000 });
  check(/1/.test((await warn.textContent()) ?? ''), 'unlock reports the wrong attempt');
  await page.evaluate(() => (location.hash = '#/settings'));
  await page.locator('.settings .seg').first().waitFor();
  await page.locator('.nav-row', { hasText: opts.lang === 'en' ? 'Activity' : 'سجل النشاط' }).click();
  await page.locator('.activity-row').first().waitFor();
  const rows = await page.locator('.activity-row bdi').allTextContents();
  check(/Unlocked|فُتحت/.test(rows[0]), `newest first is the unlock (got "${rows[0]}")`);
  check(page.locator('.activity-row.warn').first() !== null && (await page.locator('.activity-row.warn').count()) >= 1, 'the failed attempts are listed');
  await shot(page, `activity${tag}`);
  console.log('  ✓ activity flow passed');
  await ctx.close();
}

// ---------- Delete after typing (SPEC §16): a one-time account goes after its password is typed ----------

async function burnFlow(base, opts) {
  const { ctx, page, tag } = await open(base, opts);
  console.log(`burn ${tag || '(phone, ar, light)'}`);
  await page.locator('input[type=password]').waitFor();
  await unlockUi(page);
  await page.evaluate(() => (location.hash = '#/new'));
  const form = page.locator('.edit-form');
  await form.waitFor();
  const inputs = form.locator('input');
  await inputs.nth(0).fill('Backup code');
  await inputs.nth(2).fill('hasan');
  await form.locator('input[type=password]').fill('7731-0942-5518');
  await form.locator('.switch-row', { hasText: opts.lang === 'en' ? 'Delete after typing' : 'احذفه بعد' }).click();
  await form.locator('.burn-foot').waitFor();
  await shot(page, `edit-burn${tag}`);
  await page.locator('.save-btn').click();
  await page.locator('.layer .sheet').waitFor({ state: 'detached' });
  await row(page, 'Backup code').click();
  await page.locator('.burn-note').waitFor();
  await page.locator('.act-both').click();
  await page.locator('.ready-ready').waitFor();
  check((await button(base)).startsWith('typing both'), 'button types the one-time account');
  // Gone at once: the account sheet closes with a note instead of an error.
  await page.locator('.toast-ok').waitFor({ timeout: 6000 });
  await page.locator('.acc-row', { hasText: 'Backup code' }).waitFor({ state: 'detached', timeout: 5000 });
  const list = await (await fetch(`${base}/api/entries`, { headers: { cookie: (await ctx.cookies()).map((c) => `${c.name}=${c.value}`).join('; ') } })).json();
  check(!list.entries?.some((e) => e.title === 'Backup code'), 'the account is gone after its one use');
  console.log('  ✓ burn flow passed');
  await ctx.close();
}

// ---------- Auto-type sequences (SPEC §10.4): {PRESS} splits typing into parts, one press each ----------

async function sequenceFlow(base, opts) {
  const { ctx, page, tag } = await open(base, opts);
  const en = opts.lang === 'en';
  console.log(`sequence ${tag || '(phone, ar, light)'}`);
  await page.locator('input[type=password]').waitFor();
  await unlockUi(page);
  await page.evaluate(() => (location.hash = '#/new'));
  const form = page.locator('.edit-form');
  await form.waitFor();
  const inputs = form.locator('input');
  await inputs.nth(0).fill('Bank portal');
  await inputs.nth(2).fill('hasan');
  await form.locator('input[type=password]').fill('Euphrates-Bank-2026');
  check((await form.locator('.seq-editor').count()) === 0, 'the sequence editor starts collapsed');
  await form.locator('.seq-toggle').click();
  const seq = form.locator('.seq-editor input');
  // The editor refuses what the firmware refuses: no shortcut tokens.
  await seq.fill('{CTRL+V}');
  await form.locator('.seq-editor .field-error').waitFor();
  check(await page.locator('.save-btn').isDisabled(), 'an invalid sequence cannot be saved');
  await seq.fill('');
  for (const token of ['{USERNAME}', '{ENTER}', '{PRESS}', '{PASSWORD}', '{ENTER}']) await form.locator(`.seq-token[title="${token}"]`).click();
  check((await seq.inputValue()) === '{USERNAME}{ENTER}{PRESS}{PASSWORD}{ENTER}', 'token buttons insert at the caret');
  check((await form.locator('.seq-part').count()) === 2, 'the preview shows one line per press');
  await form.locator('.seq-preview').scrollIntoViewIfNeeded();
  await shot(page, `edit-sequence${tag}`);
  await page.locator('.save-btn').click();
  await page.locator('.layer .sheet').waitFor({ state: 'detached' });

  // Part 1, then part 2 after the next press.
  await row(page, 'Bank portal').click();
  await page.locator('.act-seq').click();
  await page.locator('.ready-ready').waitFor();
  await page.locator('.ready .seq-part.current', { hasText: '{USERNAME}' }).waitFor();
  await page.locator('.toast').waitFor({ state: 'detached', timeout: 8000 }); // "Saved" would cover the card
  await shot(page, `ready-sequence${tag}`);
  check((await button(base)) === 'typing sequence part 1/2 · Bank portal', 'the first press types part 1');
  await page.locator('.ready .seq-part.current', { hasText: '{PASSWORD}' }).waitFor({ timeout: 8000 });
  check(/2/.test((await page.locator('.ready-body').textContent()) ?? ''), 'Ready says part 2 is next');
  await shot(page, `ready-sequence-part2${tag}`);
  check((await button(base)) === 'typing sequence part 2/2 · Bank portal', 'the second press types part 2');
  await page.locator('.ready-typed').waitFor({ timeout: 8000 });
  await page.locator('.ready-typed').waitFor({ state: 'detached', timeout: 8000 });

  // Settings → Typing → the order "Both" types.
  await page.evaluate(() => (location.hash = '#/settings'));
  await page.locator('.settings .seg').first().waitFor();
  const orderRow = page.locator('.nav-row', { hasText: en ? 'Order for "Both"' : 'ترتيب «الاثنان معاً»' });
  await orderRow.click();
  const sheet = page.locator('.both-seq');
  await sheet.waitFor();
  check((await sheet.locator('.seq-part').count()) === 1, 'the built-in order is previewed');
  await sheet.locator('.seq-editor input').fill('{USERNAME}{TAB}{DELAY 500}{PASSWORD}{ENTER}');
  await shot(page, `settings-both-order${tag}`);
  await sheet.locator('button[type=submit]').click();
  await sheet.waitFor({ state: 'detached' });
  check((await orderRow.locator('.row-value').textContent()) === (en ? 'Custom' : 'مخصّص'), 'the row says the order is custom');
  const cookie = (await ctx.cookies()).map((c) => `${c.name}=${c.value}`).join('; ');
  const saved = await (await fetch(`${base}/api/settings`, { headers: { cookie } })).json();
  check(saved.bothSequence === '{USERNAME}{TAB}{DELAY 500}{PASSWORD}{ENTER}', 'bothSequence is stored on the device');

  // "Both" now types that order (as a sequence) on an account without its own.
  await page.evaluate(() => (location.hash = '#/'));
  await row(page, 'Microsoft').click();
  await page.locator('.act-both').click();
  await page.locator('.ready-ready').waitFor();
  check((await button(base)) === 'typing sequence part 1/1 · Microsoft', '"Both" uses the custom order');
  await page.locator('.ready-typed').waitFor({ timeout: 8000 });

  await page.evaluate(() => (location.hash = '#/settings'));
  await orderRow.click();
  await sheet.locator('.btn-ghost', { hasText: en ? 'Reset to default' : 'أعد الافتراضي' }).click();
  await sheet.waitFor({ state: 'detached' });
  const reset = await (await fetch(`${base}/api/settings`, { headers: { cookie } })).json();
  check(reset.bothSequence === '', 'reset returns to the built-in order');
  console.log('  ✓ sequence flow passed');
  await ctx.close();
}

// ---------- Firmware update (SPEC §14): from a file and from the latest release ----------

/** An ESP-IDF app image as far as the mock checks it: magic, app description, signature sector. */
function fakeFirmware(version) {
  const b = Buffer.alloc(64 * 1024);
  b[0] = 0xe9;
  b.writeUInt32LE(0xabcd5432, 0x20);
  b.write(version, 0x30, 'latin1');
  b.write('keyra', 0x50, 'latin1');
  b[b.length - 4096] = 0xe7;
  return b;
}

async function updateFlow(base, opts) {
  const { ctx, page, tag } = await open(base, opts);
  console.log(`update ${tag || '(phone, ar, light)'}`);
  await page.locator('input[type=password]').waitFor();
  await unlockUi(page);
  await page.evaluate(() => (location.hash = '#/settings'));
  await page.locator('.settings .seg').first().waitFor();
  await page.locator('.nav-row', { hasText: opts.lang === 'en' ? 'Firmware update' : 'تحديث البرنامج' }).click();
  await page.locator('.update-summary').waitFor();
  // From GitHub (the mock's release is 1.1.0, the device 1.0.0).
  await page.locator('.update .btn-primary').click();
  await page.locator('.update-notes').waitFor({ timeout: 8000 });
  await shot(page, `update${tag}`);
  await page.locator('.update .btn-primary').click();
  await page.locator('[role=progressbar]').waitFor();
  await page.locator('.ready-ready').waitFor({ timeout: 10000 });
  check((await button(base)) === 'approved update', 'button installs the update');
  await page.locator('.ready-typed').waitFor({ timeout: 5000 });
  // The mock "restarts" into 1.1.0: locked, and the new version shows.
  await page.locator('button', { hasText: /^(Unlock|فتح)$/ }).click({ timeout: 10000 });
  await page.evaluate(() => (location.hash = '#/'));
  await unlockUi(page);
  const v = await (await fetch(`${base}/api/state`)).json();
  check(v.device.version === '1.1.0', `running the new version (got ${v.device.version})`);
  // From a file: an older image is refused, the same version is taken.
  await page.evaluate(() => (location.hash = '#/settings'));
  await page.locator('.settings .seg').first().waitFor();
  await page.locator('.nav-row', { hasText: opts.lang === 'en' ? 'Firmware update' : 'تحديث البرنامج' }).click();
  const before = errors.length;
  await page.locator('.update input[type=file]').setInputFiles({ name: 'keyra-firmware.bin', mimeType: 'application/octet-stream', buffer: fakeFirmware('1.0.0') });
  await page.locator('.ready-error').waitFor({ timeout: 8000 });
  // The browser logs the refusal (409 downgrade); that one is the point of the test.
  for (let i = errors.length - 1; i >= before; i--) if (/status of 409/.test(errors[i])) errors.splice(i, 1);
  check(/older|أقدم/.test((await page.locator('.ready-error').textContent()) ?? ''), 'an older version is refused');
  console.log('  ✓ update flow passed');
  await ctx.close();
}

async function passkeysFlow(base, opts) {
  const { ctx, page, tag } = await open(base, opts);
  console.log(`passkeys ${tag || '(phone, ar, light)'}`);
  await page.locator('input[type=password]').waitFor();
  await unlockUi(page);
  await page.evaluate(() => (location.hash = '#/settings'));
  await page.locator('.settings .seg').first().waitFor();
  await page.locator('.nav-row', { hasText: opts.lang === 'en' ? 'Passkeys' : 'مفاتيح المرور' }).click();
  await page.locator('.passkey-row').first().waitFor();
  check((await page.locator('.passkey-row').count()) === 3, 'three seeded passkeys listed');
  check((await page.locator('.passkey-row bdi').first().textContent()) === 'www.amazon.com', 'newest first');
  await shot(page, `passkeys${tag}`);
  const github = page.locator('.passkey-row', { hasText: 'github.com' });
  const keys = async () => (await (await authed(ctx, `${base}/api/fido`)).json()).passkeys.length;
  // Deleting waits for Keyra's button (SPEC §5): cancelled, the passkey stays.
  const askDelete = async () => {
    await github.locator('.icon-btn').click();
    await page.locator('.alert', { hasText: 'github.com' }).waitFor();
    await page.locator('.alert-actions button').first().click();
    await page.locator('.ready-ready').waitFor();
  };
  await askDelete();
  check((await keys()) === 3, 'passkey still there before the press');
  await page.locator('.ready-ready button').click();
  await github.waitFor();
  await waitFor(async () => (await presence(ctx, base)).result?.code === 'cancelled', 'delete_passkey withdrawn');
  check((await keys()) === 3, 'passkey stays after a cancel');
  await askDelete();
  check((await button(base)) === 'approved delete_passkey', 'button approves the passkey deletion');
  await page.locator('.ready-ready').waitFor({ state: 'detached', timeout: 5000 });
  await page.locator('.passkey-row').first().waitFor();
  check(!(await github.count()), 'deleted passkey not listed');
  check((await page.locator('.passkey-row').count()) === 2, 'passkey deleted');
  check((await keys()) === 2, 'passkey gone after the press');
  const r = await fetch(`${base}/api/fido`);
  check(r.status === 401, 'passkey list needs a session');
  console.log('  ✓ passkeys flow passed');
  await ctx.close();
}

// ---------- Delete an account (SPEC §5): only a press of Keyra's button removes it ----------

async function deleteFlow(base, opts) {
  const { ctx, page, tag } = await open(base, opts);
  console.log(`delete account ${tag || '(phone, ar, light)'}`);
  await page.locator('input[type=password]').waitFor();
  await unlockUi(page);
  const entries = async () => (await (await authed(ctx, `${base}/api/entries`)).json()).entries;
  const victim = (await entries())[0];
  const there = async () => (await entries()).some((e) => e.id === victim.id);
  await page.evaluate((id) => (location.hash = `#/a/${id}/edit`), victim.id);
  await page.locator('.ready-ready').waitFor(); // editing reveals the password: one press
  check((await button(base)) === 'approved reveal', 'button approves the reveal');
  const form = page.locator('.edit-form');
  await form.waitFor();
  const askDelete = async () => {
    await form.locator('.delete-btn').click();
    await page.locator('.alert-actions button').first().click();
    await page.locator('.ready-ready').waitFor();
  };
  await askDelete();
  check(await there(), 'account still there before the press');
  await page.locator('.ready-ready button').click();
  await form.waitFor();
  await waitFor(async () => (await presence(ctx, base)).result?.code === 'cancelled', 'delete_entry withdrawn');
  check(await there(), 'account stays after a cancel');
  await askDelete();
  check((await button(base)) === 'approved delete_entry', 'button approves the deletion');
  await page.locator('.toast-ok').waitFor({ timeout: 6000 });
  await page.locator('.layer .sheet').waitFor({ state: 'detached', timeout: 6000 });
  await page.locator('.acc-row', { hasText: victim.title }).waitFor({ state: 'detached', timeout: 5000 });
  check(!(await there()), 'account gone after the press');
  console.log('  ✓ delete flow passed');
  await ctx.close();
}

// ---------- Keyboard layouts (SPEC §10.1-10.3): pick one, Layout Doctor, layout-safe generator ----------

async function keyboardFlow(base, opts) {
  const { ctx, page, tag } = await open(base, opts);
  const en = opts.lang === 'en';
  console.log(`keyboard ${tag || '(phone, ar, light)'}`);
  await page.locator('input[type=password]').waitFor();
  await unlockUi(page);
  const saved = () => page.evaluate(async () => (await (await fetch('/api/settings')).json()).layoutUsb);
  // The Arabic UI names layouts in Arabic (keyboard.ts AR_NAMES).
  const german = en ? 'German' : 'الألمانية';
  const ukName = en ? 'English (UK)' : 'الإنجليزية (البريطانية)';
  const usbRow = page.locator('.nav-row', { hasText: en ? 'USB keyboard layout' : 'تخطيط مفاتيح USB' });

  // Pick from the list: German (Windows).
  await page.evaluate(() => (location.hash = '#/settings'));
  await page.locator('.settings .seg').first().waitFor();
  await usbRow.click();
  await page.locator('.layout-row').first().waitFor();
  check((await page.locator('.layout-row').count()) === 16, 'every layout of the firmware table is offered');
  check((await page.locator('.layout-row .layout-exp').count()) === 15, 'all but US are marked experimental');
  await shot(page, `layouts${tag}`);
  await page.locator('.layout-row', { hasText: german }).first().click();
  await page.locator('.layer .sheet').waitFor({ state: 'detached' });
  check((await saved()) === 'de', 'German saved for USB');
  check(((await usbRow.textContent()) ?? '').includes(german), 'the row shows the saved layout');

  // Layout Doctor: Keyra types the probe; what appeared names the layout.
  await usbRow.click();
  await page.locator('.doctor-btn').click();
  await page.locator('.doctor-type').click();
  await page.locator('.ready-ready').waitFor();
  const typed = await button(base);
  check(typed === 'typing probe · qwzy ö"§-', `the probe is typed (${typed})`);
  await page.locator('.ready-typed').waitFor({ timeout: 5000 });
  const input = page.locator('.doctor-input');
  await input.waitFor({ timeout: 5000 });
  await input.fill(typed.slice('typing probe · '.length));
  await page.locator('.doctor-result').waitFor();
  // A German Windows and a German Mac computer show the same: Keyra already has one, offers the other.
  check(((await page.locator('.doctor-result .notice').textContent()) ?? '').includes(german), 'already set to the matching layout');
  check((await page.locator('.doctor-use').count()) === 1, 'the Mac twin is offered');
  await shot(page, `layout-doctor${tag}`);
  // The computer showed the UK layout's line instead.
  await input.fill('qwyz ;"£/');
  await page.locator('.doctor-use', { hasText: ukName }).click();
  await page.locator('.layer .sheet').waitFor({ state: 'detached' });
  check((await saved()) === 'uk', 'the suggested layout is saved');

  // Generator: a custom symbol set, then only characters the same on UK (USB) and US (Bluetooth).
  const bodies = [];
  page.on('request', (r) => r.url().endsWith('/api/generate') && bodies.push(JSON.parse(r.postData() ?? '{}')));
  await page.evaluate(() => (location.hash = '#/generate'));
  await page.waitForFunction(() => (document.querySelector('.gen-preview')?.textContent ?? '').trim().length === 20);
  await page.locator('.symbol-input').fill('-_.-');
  await page.locator('.symbol-input').press('Enter');
  check((await page.locator('.symbol-input').inputValue()) === '-_.', 'symbol set cleaned to distinct punctuation');
  await page.waitForFunction(() => {
    const v = (document.querySelector('.gen-preview')?.textContent ?? '').trim();
    return /^[A-Za-z0-9._-]{20}$/.test(v) && /[._-]/.test(v);
  });
  await page.locator('.symbol-input').fill('');
  await page.locator('.symbol-input').press('Enter'); // back to the default set
  const safe = page.getByRole('switch', { name: en ? 'Safe for my keyboard layouts' : 'آمنة لتخطيطات لوحات مفاتيحي' });
  check(((await page.locator('.row-note').textContent()) ?? '').includes(ukName), 'the note names the layouts');
  const before = bodies.length;
  await safe.click();
  await page.waitForFunction(() => document.querySelector('.gen-preview.busy') === null);
  for (let i = 0; i < 40 && !bodies.slice(before).some((b) => b.layoutSafe); i++) await page.waitForTimeout(100);
  await page.waitForFunction(() => document.querySelector('.gen-preview.busy') === null);
  const last = bodies.at(-1);
  check(last.layoutSafe === true && JSON.stringify(last.layouts) === '["uk","us"]' && !('symbolSet' in last), `layout-safe request (${JSON.stringify(last)})`);
  const pw = ((await page.locator('.gen-preview').textContent()) ?? '').trim();
  check(pw.length === 20 && !/[@"#]/.test(pw), `no character that moves between UK and US (${pw})`);
  await safe.scrollIntoViewIfNeeded();
  await shot(page, `generate-layout-safe${tag}`);
  console.log('  ✓ keyboard flow passed');
  await ctx.close();
}

// ---------- flow 4: generator → type twice → save → update → history; type text (SPEC §9) ----------

async function generatorFlow(base, opts) {
  const { ctx, page, tag } = await open(base, opts);
  console.log(`generator ${tag || '(phone, ar, light)'}`);
  await unlockUi(page);
  const preview = page.locator('.gen-preview');
  const pwNow = async () => ((await preview.textContent()) ?? '').trim();

  await page.locator('.top-bar .gen-btn').click();
  await page.waitForFunction(() => (document.querySelector('.gen-preview')?.textContent ?? '').trim().length === 20);
  check(/bits/.test((await page.locator('.meter-label').textContent()) ?? '') || /بت/.test((await page.locator('.meter-label').textContent()) ?? ''), 'entropy shown');
  await page.locator('.len-input').fill('32');
  await page.locator('.len-input').press('Enter');
  await page.waitForFunction(() => (document.querySelector('.gen-preview')?.textContent ?? '').trim().length === 32);
  const symbols = page.getByRole('switch').nth(3);
  await symbols.click(); // symbols off
  await page.waitForFunction(() => /^[A-Za-z0-9]{32}$/.test((document.querySelector('.gen-preview')?.textContent ?? '').trim()));
  await symbols.click(); // back on
  await page.waitForFunction(() => /[^A-Za-z0-9]/.test((document.querySelector('.gen-preview')?.textContent ?? '').trim()));
  await shot(page, `generate${tag}`);
  const pw1 = await pwNow();
  check(pw1.length === 32, `generated ${pw1.length} chars`);

  // Type twice: password, Tab, password.
  await page.locator('.gen-twice').click();
  await page.locator('.ready-ready').waitFor();
  check((await button(base)) === `typing text (${2 * 32 + 1} chars)`, 'typed twice with a Tab between');
  await page.locator('.ready-typed').waitFor({ timeout: 5000 });
  await page.locator('.gen-actions').waitFor({ timeout: 5000 });
  check((await pwNow()) === pw1, 'same password after typing');

  // Save as a new account: the form opens with the password filled in.
  await page.locator('.gen-save').click();
  await page.locator('.save-new').click();
  const form = page.locator('.edit-form');
  await form.waitFor();
  check((await form.locator('input[type=password]').inputValue()) === pw1, 'new account prefilled');
  await form.locator('input').nth(0).fill('Shop');
  await page.locator('.save-btn').click();
  await row(page, 'Shop').waitFor();
  await page.locator('.layer .sheet').waitFor({ state: 'detached' });

  // A second password replaces it: the first moves to the account's history.
  await page.locator('.top-bar .gen-btn').click();
  await page.waitForFunction((old) => {
    const v = (document.querySelector('.gen-preview')?.textContent ?? '').trim();
    return v.length === 32 && v !== old;
  }, pw1);
  const pw2 = await pwNow();
  await page.locator('.gen-save').click();
  await page.locator('.save-update').click();
  await page.locator('.pick .acc-row', { hasText: 'Shop' }).click();
  await page.locator('.alert .btn-primary').click();
  await page.locator('.history-row').waitFor();
  check((await page.locator('.history-row').count()) === 1, 'one old password');
  await page.locator('.history-row .icon-btn').first().click(); // reveal
  await page.waitForTimeout(400);
  await button(base); // secrets reach the phone only after a press (SPEC §12.3); it opens a short grace window
  await page.waitForFunction((pw) => (document.querySelector('.history-row .kv-value')?.textContent ?? '').trim() === pw, pw1, { timeout: 5000 }).catch(() => {});
  check(((await page.locator('.history-row .kv-value').textContent()) ?? '').trim() === pw1, 'history holds the first password');
  await page.locator('.details .kv-row', { hasText: /Password|كلمة المرور/ }).locator('.icon-btn').first().click();
  check((await page.locator('.details .secret').first().textContent())?.trim() === pw2, 'current password is the second one');
  await shot(page, `account-history${tag}`);

  // The Edit form's inline generator.
  await page.evaluate(() => (location.hash = location.hash + '/edit'));
  await page.locator('.gen-toggle').click();
  await page.waitForFunction(() => (document.querySelector('.gen-inline .gen-preview')?.textContent ?? '').trim().length === 32);
  const pw3 = ((await page.locator('.gen-inline .gen-preview').textContent()) ?? '').trim();
  await page.locator('.gen-inline').scrollIntoViewIfNeeded();
  await shot(page, `edit-generator${tag}`);
  await page.locator('.gen-inline .btn-primary').click();
  check((await page.locator('.edit-form input[type=password]').inputValue()) === pw3, 'inline generator fills the field');
  await page.locator('.layer .sheet-end .icon-btn').last().click(); // close: unsaved, so it asks first
  await page.locator('.alert .btn-danger-confirm').click(); // discard
  await page.locator('.edit-form').waitFor({ state: 'detached' });
  await page.evaluate(() => (location.hash = '#/'));
  await page.locator('.layer .sheet').waitFor({ state: 'detached' });

  // Type text… from the menu.
  await page.locator('.top-bar .more-btn').click();
  await page.locator('.menu-row').first().click();
  await page.locator('.type-text textarea').fill('hello world');
  await shot(page, `type-text${tag}`);
  await page.locator('.type-text button[type=submit]').click();
  await page.locator('.ready-ready').waitFor();
  check((await button(base)) === 'typing text (11 chars)', 'text typed once');
  await page.locator('.ready-typed').waitFor({ timeout: 5000 });
  await page.locator('.type-text').waitFor({ timeout: 5000 });
  check((await page.locator('.type-text textarea').inputValue()) === '', 'text cleared after typing');
  console.log('  ✓ flow passed');
  await ctx.close();
}

// ---------- helpers for the account/safety flows below ----------

const L = (opts, en, ar) => (opts.lang === 'en' ? en : ar);

/** Drops the browser's log lines for responses a flow provoked on purpose (e.g. a wrong guess's 401). */
function expectErrors(before, ...statuses) {
  const re = new RegExp(`status of (${statuses.join('|')})`);
  for (let i = errors.length - 1; i >= before; i--) if (re.test(errors[i])) errors.splice(i, 1);
}

/** The app's own API call from inside the page (session cookie + CSRF header), → { status, body, retryAfter }. */
function pageApi(page, method, path, body) {
  return page.evaluate(
    async ([method, path, body]) => {
      const headers = { 'Content-Type': 'application/json', 'X-Keyra-CSRF': sessionStorage.getItem('keyra.csrf') ?? '' };
      const r = await fetch(path, { method, headers, body: body === undefined ? undefined : JSON.stringify(body) });
      const text = await r.text();
      return { status: r.status, body: text ? JSON.parse(text) : null, retryAfter: r.headers.get('Retry-After') };
    },
    [method, path, body],
  );
}

async function settingsRow(page, label) {
  await page.evaluate(() => (location.hash = '#/settings'));
  await page.locator('.settings .seg').first().waitFor();
  await page.waitForTimeout(400); // let the page transition finish before opening a sheet
  await page.locator('.nav-row', { hasText: label }).click();
}

/** Locks from the top bar and opens the unlock form again. */
async function lockUi(page) {
  await page.evaluate(() => (location.hash = '#/'));
  await page.locator('.layer .sheet').waitFor({ state: 'detached' });
  await page.locator('.top-bar .icon-btn').last().click();
  await page.locator('.locked-glyph').waitFor();
  await page.locator('button', { hasText: /^(Unlock|فتح)$/ }).click();
  await page.locator('input[type=password]').first().waitFor();
}

/** A wrong passphrase on the unlock form is refused (its 401 is expected). */
async function unlockWrong(page, pass) {
  const before = errors.length;
  await page.locator('input[type=password]').fill(pass);
  await page.locator('button[type=submit]').click();
  await page.locator('.field-error').first().waitFor({ timeout: 8000 });
  expectErrors(before, 401);
}

// ---------- Recovery key (SPEC §12.2): created with a press, it sets a new passphrase on the unlock screen ----------

async function recoveryFlow(base, opts) {
  const { ctx, page, tag } = await open(base, opts);
  console.log(`recovery ${tag || '(phone, ar, light)'}`);
  await page.locator('input[type=password]').waitFor();
  await unlockUi(page);
  await settingsRow(page, L(opts, 'Recovery kit', 'عدّة الاسترداد'));
  await page.locator('.recovery .btn', { hasText: L(opts, 'Create recovery kit', 'أنشئ عدّة الاسترداد') }).click();
  await page.locator('.recovery .ready-ready').waitFor();
  check((await button(base)) === 'approved recovery', 'button approves the recovery key');
  const keyEl = page.locator('[data-testid=recovery-key]');
  await keyEl.waitFor({ timeout: 8000 });
  const keyText = ((await keyEl.textContent()) ?? '').trim();
  check(keyText.length >= 40, `recovery key shown (${keyText.length} chars)`);
  await page.locator('.recovery .btn', { hasText: L(opts, "I've saved it", 'حفظته') }).click();
  await page.locator('.recovery').waitFor({ state: 'detached' });

  await lockUi(page);
  await page.locator('button', { hasText: L(opts, 'Use recovery key', 'استخدم مفتاح الاسترداد') }).click();
  const form = page.locator('.recover-form');
  await form.waitFor();
  await form.locator('.mono-input').first().fill(keyText);
  await form.locator('input[type=password]').nth(0).fill(NEW_PASS);
  await form.locator('input[type=password]').nth(1).fill(NEW_PASS);
  await form.locator('button[type=submit]').click();
  await page.locator('.list-pane').waitFor({ timeout: 10000 });
  await page.locator('.toast-ok').waitFor({ timeout: 5000 });

  // The old passphrase is gone; the one set with the key opens the vault.
  await lockUi(page);
  await unlockWrong(page, PASS);
  await unlockUi(page, NEW_PASS);
  console.log('  ✓ recovery flow passed');
  await ctx.close();
}

// ---------- Restore (SPEC §11): merge without a press, replace after one; a wrong passphrase stops before it ----------

async function restoreFlow(base, opts) {
  const { ctx, page, tag } = await open(base, opts);
  console.log(`restore ${tag || '(phone, ar, light)'}`);
  await page.locator('input[type=password]').waitFor();
  await unlockUi(page);
  const count = async () => (await pageApi(page, 'GET', '/api/entries')).body.entries.length;
  const seeded = await count();

  await page.evaluate(() => (location.hash = '#/backup'));
  const backupPass = 'restore test passphrase';
  await page.locator('.backup input[type=password]').first().fill(backupPass);
  const [download] = await Promise.all([
    page.waitForEvent('download'),
    (async () => {
      await page.locator('.backup .btn-primary').first().click();
      await page.waitForTimeout(500);
      check((await button(base)) === 'approved backup', 'button approves the backup');
    })(),
  ]);
  const backup = readFileSync(await download.path());
  check(JSON.parse(backup.toString()).format === 'keyra-backup', 'backup file downloaded');
  // An account added after the backup: a merge keeps it, a replace drops it.
  check((await pageApi(page, 'POST', '/api/entries', { title: 'After the backup', username: 'later', password: 'Later-Password-77' })).status === 201, 'entry added');

  const file = page.locator('.backup input[type=file]');
  const pass = page.locator('.backup input[type=password]').nth(1);
  const restoreBtn = page.locator('.backup .btn', { hasText: L(opts, /^Restore$/, /^استعادة$/) });
  const seg = (label) => page.locator('.backup .seg-item', { hasText: label });

  // Merge: no press, every entry of the file matches one here.
  await file.setInputFiles({ name: 'keyra-backup.json', mimeType: 'application/json', buffer: backup });
  await pass.fill(backupPass);
  await seg(L(opts, 'Merge', 'دمج')).click();
  await restoreBtn.click();
  const merged = page.locator('.toast-ok', { hasText: L(opts, 'Added 0, updated', 'أُضيف 0 وحُدِّث') });
  await merged.waitFor({ timeout: 8000 });
  check(new RegExp(`\\b${seeded}\\b`).test((await merged.textContent()) ?? ''), `merge updated all ${seeded}`);
  check((await count()) === seeded + 1, 'merge keeps the newer account');
  await merged.waitFor({ state: 'detached', timeout: 8000 });

  // Replace with a wrong passphrase: refused at once, nothing waits for the button.
  await file.setInputFiles({ name: 'keyra-backup.json', mimeType: 'application/json', buffer: backup });
  await pass.fill('not the backup passphrase');
  await seg(L(opts, 'Replace', 'استبدال')).click();
  const before = errors.length;
  await restoreBtn.click();
  await page.locator('.alert .btn-danger-confirm').click();
  await page.locator('.backup .field-error').waitFor({ timeout: 8000 });
  expectErrors(before, 401);
  const st = (await pageApi(page, 'GET', '/api/state')).body;
  check(!st.presence.awaiting && (await page.locator('.ready-ready').count()) === 0, 'a wrong passphrase never asks for the button');

  // Replace: after the press the vault is exactly the backup.
  await pass.fill(backupPass);
  await restoreBtn.click();
  await page.locator('.alert .btn-danger-confirm').click();
  await page.locator('.backup .ready-ready').waitFor();
  check((await button(base)) === 'approved restore', 'button approves the replace');
  await page.locator('.toast-ok', { hasText: new RegExp(`\\b${seeded}\\b`) }).waitFor({ timeout: 8000 });
  check((await count()) === seeded, 'replace drops the newer account');
  console.log('  ✓ restore flow passed');
  await ctx.close();
}

// ---------- Factory reset from Settings (press) → Welcome ----------

async function resetFlow(base, opts) {
  const { ctx, page, tag } = await open(base, opts);
  console.log(`factory reset ${tag || '(phone, ar, light)'}`);
  await page.locator('input[type=password]').waitFor();
  await unlockUi(page);
  await page.evaluate(() => (location.hash = '#/settings'));
  await page.locator('.settings .seg').first().waitFor();
  await page.waitForTimeout(400);
  await page.locator('.danger-group .nav-row').click();
  await page.locator('.alert .btn-danger-confirm').click();
  await page.locator('.ready-ready').waitFor();
  check((await button(base)) === 'approved factory_reset', 'button approves the reset');
  await page.locator('.welcome').waitFor({ timeout: 10000 });
  await page.locator('.toast-ok', { hasText: L(opts, 'Keyra is starting fresh.', 'سيبدأ Keyra من جديد.') }).waitFor({ timeout: 6000 });
  const st = await (await fetch(`${base}/api/state`)).json();
  check(st.initialized === false && st.unlocked === false, 'device is uninitialized after the reset');
  console.log('  ✓ factory reset flow passed');
  await ctx.close();
}

// ---------- Change passphrase: wrong current is refused and throttled like unlock; the new one unlocks ----------

async function passphraseFlow(base, opts) {
  const { ctx, page, tag } = await open(base, opts);
  console.log(`passphrase ${tag || '(phone, ar, light)'}`);
  await page.locator('input[type=password]').waitFor();
  await unlockUi(page);
  await settingsRow(page, L(opts, 'Change master passphrase', 'تغيير العبارة الرئيسية'));
  const sheet = page.locator('.layer .sheet form');
  await sheet.waitFor();
  const fields = sheet.locator('input[type=password]');
  await fields.nth(0).fill('not my passphrase');
  await fields.nth(1).fill(NEW_PASS);
  await fields.nth(2).fill(NEW_PASS);
  let before = errors.length;
  await sheet.locator('button[type=submit]').click();
  await sheet.locator('.field-error', { hasText: L(opts, "That passphrase isn't right.", 'هذه العبارة غير صحيحة.') }).waitFor({ timeout: 8000 });

  // Shared throttle with unlock (Vault::attempt): the 5th wrong guess waits 2 s, then both answer 429.
  let r;
  for (let i = 2; i <= 5; i++) r = await pageApi(page, 'POST', '/api/passphrase', { current: 'still wrong', next: NEW_PASS });
  check(r.status === 401 && r.body.error === 'wrong' && r.body.retryAfterMs === 2000, `5th wrong guess says wait (${JSON.stringify(r.body)})`);
  r = await pageApi(page, 'POST', '/api/passphrase', { current: PASS, next: NEW_PASS });
  check(r.status === 429 && r.body.error === 'rate_limited' && r.body.retryAfterMs > 0 && r.retryAfter === '2', `then 429 (${JSON.stringify(r)})`);
  r = await pageApi(page, 'POST', '/api/unlock', { passphrase: PASS });
  check(r.status === 429, 'unlock shares the throttle');
  expectErrors(before, 401, 429);
  await page.waitForTimeout(2100);

  await fields.nth(0).fill(PASS);
  await sheet.locator('button[type=submit]').click();
  await page.locator('.toast-ok', { hasText: L(opts, 'Passphrase changed.', 'تم تغيير العبارة.') }).waitFor({ timeout: 8000 });
  await sheet.waitFor({ state: 'detached' });

  await lockUi(page);
  await unlockWrong(page, PASS);
  await unlockUi(page, NEW_PASS);
  console.log('  ✓ passphrase flow passed');
  await ctx.close();
}

// ---------- Settings persist on the device; mock contract checks (state, typing text per layout) ----------

async function settingsFlow(base, opts) {
  const { ctx, page, tag } = await open(base, opts);
  console.log(`settings ${tag || '(phone, ar, light)'}`);
  await page.locator('input[type=password]').waitFor();
  const anon = await (await fetch(`${base}/api/state`)).json();
  check(!('usbOs' in anon.host), 'host.usbOs is not shown without a session');
  await unlockUi(page);
  check('usbOs' in (await pageApi(page, 'GET', '/api/state')).body.host, 'host.usbOs is shown to a session');

  const autoLock = L(opts, 'Auto-lock', 'القفل التلقائي');
  await settingsRow(page, autoLock);
  const put = page.waitForRequest((q) => q.url().endsWith('/api/settings') && q.method() === 'PUT');
  await page.locator('[role=radio]', { hasText: L(opts, '30 min', '30 دقيقة') }).click();
  check(JSON.parse((await put).postData() ?? '{}').autoLockMin === 30, 'PUT /api/settings carries autoLockMin');
  await page.locator('.nav-row', { hasText: autoLock }).locator('.row-value', { hasText: '30' }).waitFor();

  await page.reload();
  await settingsRow(page, autoLock).catch(async () => {
    await unlockUi(page); // a reload may land on the unlock screen; the setting lives on the device either way
    await settingsRow(page, autoLock);
  });
  await page.locator('[role=radio][aria-checked=true]', { hasText: '30' }).waitFor();
  check((await pageApi(page, 'GET', '/api/settings')).body.autoLockMin === 30, 'autoLockMin persisted');
  check((await pageApi(page, 'GET', '/api/state')).body.autoLockMin === 30, 'state reports the new auto-lock');
  await page.keyboard.press('Escape');

  // Free text is checked against the output's layout (validate::typeText), not as ASCII.
  const before = errors.length;
  let r = await pageApi(page, 'POST', '/api/type', { text: 'Grüße', target: 'usb' });
  check(r.status === 400, `US layout cannot type ü/ß (${r.status})`);
  check((await pageApi(page, 'PUT', '/api/settings', { layoutUsb: 'de' })).status === 200, 'German set for USB');
  r = await pageApi(page, 'POST', '/api/type', { text: 'Grüße', target: 'usb' });
  check(r.status === 202, `German layout types ü/ß (${r.status})`);
  check((await button(base)) === 'typing text (5 chars)', 'the text is typed after a press');
  expectErrors(before, 400);
  await page.waitForTimeout(800);
  check((await pageApi(page, 'GET', '/api/state')).body.last?.code === 'typed', 'typed without unsupported_char');

  // GET /api/keyboard "chars" and the Type text screen check text the way the device does.
  const kbd = (await pageApi(page, 'GET', '/api/keyboard')).body;
  const charsOf = (id) => kbd.layouts.find((l) => l.id === id).chars;
  check(charsOf('us').length === 95, 'US layout: exactly printable ASCII');
  check(charsOf('de').includes('ü') && !charsOf('ar').includes('a'), 'German has ü, Arabic 101 has no Latin letters');
  const typeScreen = async (text) => {
    await page.evaluate(() => (location.hash = '#/'));
    await page.evaluate(() => (location.hash = '#/type'));
    await page.locator('.type-text textarea').fill(text);
    await page.waitForTimeout(300); // GET /api/keyboard
    return page.locator('.type-text .notice').count();
  };
  check((await typeScreen('Grüße')) === 0, 'German: Grüße accepted on the Type text screen');
  check(!(await page.locator('.type-text button[type=submit]').isDisabled()), 'and it can be sent');
  check((await pageApi(page, 'PUT', '/api/settings', { layoutUsb: 'ar' })).status === 200, 'Arabic 101 set for USB');
  check((await typeScreen('ab سلام')) === 1, 'Arabic 101: Latin letters flagged');
  const note = (await page.locator('.type-text .notice').textContent()) ?? '';
  check(note.includes('a b') && !note.includes('س'), `only the missing letters are named (${note})`);
  check(await page.locator('.type-text button[type=submit]').isDisabled(), 'and it cannot be sent');
  await page.evaluate(() => (location.hash = '#/'));
  await pageApi(page, 'PUT', '/api/settings', { layoutUsb: 'us' });
  console.log('  ✓ settings flow passed');
  await ctx.close();
}

// ---------- Trusted browsers (SPEC §8.2): removing this browser signs it out; it must be trusted again ----------

async function trustedFlow(base, opts) {
  const { ctx, page, tag } = await open(base, opts);
  console.log(`trusted ${tag || '(phone, ar, light)'}`);
  const trustAndUnlock = async () => {
    await page.locator('input[type=password]').fill(PASS);
    await page.locator('button[type=submit]').click();
    await page.locator('.ready-ready').waitFor();
    check((await button(base)) === 'approved trust_browser', 'button approves trust');
    await page.locator('.list-pane, .settings').first().waitFor({ timeout: 10000 }); // back where it was
  };
  await page.locator('input[type=password]').waitFor();
  await trustAndUnlock();
  await settingsRow(page, L(opts, 'Trusted browsers', 'المتصفحات الموثوقة'));
  const mine = page.locator('.trusted-row', { has: page.locator('.chip-accent') });
  await mine.waitFor();
  check((await page.locator('.trusted-row').count()) === 1, 'one trusted browser');
  const before = errors.length;
  await mine.locator('.icon-btn').click();
  await page.locator('.alert .btn-danger-confirm').click();
  // Its session ended with the trust: the app is locked and the next unlock asks for the button again.
  await page.locator('input[type=password], .locked-glyph').first().waitFor({ timeout: 8000 });
  expectErrors(before, 401);
  if (await page.locator('.locked-glyph').count()) await page.locator('button', { hasText: /^(Unlock|فتح)$/ }).click();
  await trustAndUnlock();
  await settingsRow(page, L(opts, 'Trusted browsers', 'المتصفحات الموثوقة'));
  await mine.waitFor();
  check((await page.locator('.trusted-row').count()) === 1, 'trusted again (the removed one is gone)');
  console.log('  ✓ trusted flow passed');
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
  const seeded = await startMock();
  const fresh1 = await startMock({ MOCK_FRESH: '1' });
  const fresh2 = await startMock({ MOCK_FRESH: '1' });
  const home = await startMock({ MOCK_VIA: 'home' });

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

  await bleFlow(await startMock(), {});
  await bleFlow(await startMock(), { lang: 'en' });
  await homeFlow(home, { lang: 'en' });
  await generatorFlow(await startMock(), { lang: 'en' });
  await generatorFlow(await startMock(), {});
  await passkeysFlow(await startMock(), {});
  await passkeysFlow(await startMock(), { lang: 'en', dark: true });
  await deleteFlow(await startMock(), {});
  await deleteFlow(await startMock(), { lang: 'en', dark: true });
  await healthFlow(await startMock(), {});
  await healthFlow(await startMock(), { lang: 'en', dark: true });
  await activityFlow(await startMock(), {});
  await activityFlow(await startMock(), { lang: 'en', dark: true });
  await burnFlow(await startMock(), {});
  await burnFlow(await startMock(), { lang: 'en', dark: true });
  await sequenceFlow(await startMock(), {});
  await sequenceFlow(await startMock(), { lang: 'en', dark: true });
  await updateFlow(await startMock({ MOCK_HOME_ONLINE: '1' }), {});
  await updateFlow(await startMock({ MOCK_HOME_ONLINE: '1' }), { lang: 'en', dark: true });
  await keyboardFlow(await startMock(), {});
  await keyboardFlow(await startMock(), { lang: 'en', dark: true });
  await recoveryFlow(await startMock(), {});
  await recoveryFlow(await startMock(), { lang: 'en', dark: true });
  await restoreFlow(await startMock(), {});
  await restoreFlow(await startMock(), { lang: 'en', dark: true });
  await resetFlow(await startMock(), {});
  await resetFlow(await startMock(), { lang: 'en', dark: true });
  await passphraseFlow(await startMock(), {});
  await passphraseFlow(await startMock(), { lang: 'en', dark: true });
  await settingsFlow(await startMock(), {});
  await settingsFlow(await startMock(), { lang: 'en', dark: true });
  await trustedFlow(await startMock({ MOCK_VIA: 'home' }), {});
  await trustedFlow(await startMock({ MOCK_VIA: 'home' }), { lang: 'en', dark: true });

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
