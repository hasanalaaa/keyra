// End to end with a real Chromium that loads the extension (dist/chrome-e2e) against
// web/mock/server.mjs and local demo sites: one-press pairing, the key in the field, arm and type
// (the press is POST /__mock/button), a generated password, save, update, the anyHost guard, and
// no prompt for a plain sign-in. Then screenshots in Arabic and English, light and dark, into
// extension/screenshots/. Needs `npm --prefix web run build` (the mock serves the web app).
//
//   npm run e2e            HEADED=1 npm run e2e (watch it)            SHOTS=0 (skip screenshots)
import { chromium } from 'playwright';
import { execFileSync, spawn } from 'node:child_process';
import { existsSync, mkdirSync, mkdtempSync, rmSync } from 'node:fs';
import { createServer } from 'node:net';
import { tmpdir } from 'node:os';
import { join } from 'node:path';
import { fileURLToPath } from 'node:url';
import { startSite } from './site.mjs';

const EXT = fileURLToPath(new URL('..', import.meta.url));
const WEB = fileURLToPath(new URL('../../web/', import.meta.url));
const SHOTS = join(EXT, 'screenshots');
const PASS = 'keyra demo vault';
const HEADED = process.env.HEADED === '1';
const WANT_SHOTS = process.env.SHOTS !== '0';

if (!existsSync(join(WEB, 'dist/index.html'))) {
  console.error('web/dist is missing: run `npm --prefix web run build` first');
  process.exit(1);
}
execFileSync(process.execPath, ['build.mjs', '--e2e'], { cwd: EXT, stdio: 'inherit' });
const DIST = { en: join(EXT, 'dist/chrome-e2e'), ar: join(EXT, 'dist/chrome-e2e-ar') };

let failures = 0;
const check = (ok, what) => {
  console.log(`  ${ok ? '✓' : '✗'} ${what}`);
  if (!ok) failures++;
};
const sleep = (ms) => new Promise((r) => setTimeout(r, ms));
async function waitFor(fn, what, timeout = 8000) {
  const end = Date.now() + timeout;
  for (;;) {
    try {
      if (await fn()) return true;
    } catch {
      /* not yet */
    }
    if (Date.now() > end) {
      check(false, `${what} (timed out)`);
      return false;
    }
    await sleep(150);
  }
}

// ---------- the mock device ----------

function freePort() {
  return new Promise((resolve, reject) => {
    const srv = createServer();
    srv.on('error', reject);
    srv.listen(0, () => {
      const { port } = srv.address();
      srv.close(() => resolve(port));
    });
  });
}

const port = await freePort();
const mockProc = spawn(process.execPath, ['mock/server.mjs'], { cwd: WEB, env: { ...process.env, PORT: String(port) }, stdio: ['ignore', 'pipe', 'inherit'] });
await new Promise((resolve, reject) => {
  mockProc.stdout.on('data', (d) => String(d).includes('Keyra mock on') && resolve());
  mockProc.on('exit', (c) => reject(new Error(`mock exited (${c})`)));
});
const local = `http://localhost:${port}`; // the test's own session (the human's phone)
const ADDRESS = `http://keyra.test:${port}`; // what the extension talks to
const button = async (press = 'short') => (await (await fetch(`${local}/__mock/button`, { method: 'POST', body: JSON.stringify({ press }) })).json()).result;

let cookie = '';
let csrf = '';
async function api(method, path, body) {
  const headers = { 'Content-Type': 'application/json', ...(cookie && { Cookie: cookie }), ...(csrf && { 'X-Keyra-Csrf': csrf }) };
  const r = await fetch(local + path, { method, headers, body: body === undefined ? undefined : JSON.stringify(body) });
  const ks = /ks=[^;]+/.exec(r.headers.get('set-cookie') ?? '')?.[0];
  if (ks) cookie = ks;
  const text = await r.text();
  return { status: r.status, body: text ? JSON.parse(text) : {} };
}
async function phoneUnlock() {
  const r = await api('POST', '/api/unlock', { passphrase: PASS });
  csrf = r.body.csrf;
}
async function makeToken(name) {
  const body = { name, kind: 'extension', scope: 'all' };
  await api('POST', '/api/tokens', body);
  await button();
  for (let i = 0; i < 30; i++) {
    const r = await api('POST', '/api/tokens', body);
    if (r.status === 201) return r.body.token;
    await sleep(100);
  }
  throw new Error('token not created');
}
const entries = async () => (await api('GET', '/api/entries')).body.entries;

const site = await startSite();
const at = (host, path) => `https://${host}:${site.port}${path}`;

// ---------- browser ----------

async function launch(lang) {
  const dir = mkdtempSync(join(tmpdir(), 'keyra-e2e-profile-'));
  const ctx = await chromium.launchPersistentContext(dir, {
    channel: 'chromium',
    headless: !HEADED,
    env: { ...process.env, LANGUAGE: lang === 'ar' ? 'ar' : 'en_US', LANG: lang === 'ar' ? 'ar_IQ.UTF-8' : 'en_US.UTF-8' },
    locale: lang === 'ar' ? 'ar' : 'en-US',
    viewport: { width: 1180, height: 760 },
    deviceScaleFactor: 2,
    ignoreHTTPSErrors: true,
    args: [
      `--disable-extensions-except=${DIST[lang]}`,
      `--load-extension=${DIST[lang]}`,
      `--lang=${lang === 'ar' ? 'ar' : 'en-US'}`,
      '--host-resolver-rules=MAP keyra.test 127.0.0.1, MAP github.com 127.0.0.1, MAP evil.example 127.0.0.1, MAP newsite.example 127.0.0.1',
    ],
  });
  await ctx.addInitScript((l) => {
    if (location.hostname === 'keyra.test') localStorage.setItem('keyra.lang', l);
  }, lang);
  let sw = ctx.serviceWorkers()[0];
  sw ??= await ctx.waitForEvent('serviceworker');
  const id = new URL(sw.url()).host;
  const errors = [];
  ctx.on('weberror', (e) => errors.push(String(e.error())));
  current = { ctx, sw, id, dir, errors };
  return current;
}

/** The tab id of `page`, as the extension sees it (the popup takes it as ?tab=). */
async function tabIdOf(b, page) {
  await page.bringToFront();
  return b.sw.evaluate(async () => (await chrome.tabs.query({ active: true, lastFocusedWindow: true }))[0]?.id);
}

async function popup(b, page, theme = 'light') {
  const tab = page ? await tabIdOf(b, page) : 0;
  const p = await b.ctx.newPage();
  await p.emulateMedia({ colorScheme: theme });
  await p.setViewportSize({ width: 368, height: 600 });
  await p.goto(`chrome-extension://${b.id}/popup.html${tab ? `?tab=${tab}` : ''}`);
  await p.locator('.bar').waitFor();
  await p.evaluate(() => document.fonts.ready);
  return p;
}

async function shotPopup(p, name) {
  if (!WANT_SHOTS) return;
  await sleep(350);
  const h = await p.evaluate(() => Math.ceil(document.body.getBoundingClientRect().height));
  await p.screenshot({ path: join(SHOTS, `${name}.png`), clip: { x: 0, y: 0, width: 368, height: Math.min(600, Math.max(440, h)) } });
}

async function shotPage(p, name) {
  if (!WANT_SHOTS) return;
  await sleep(400);
  await p.screenshot({ path: join(SHOTS, `${name}.png`) });
}

const L = (lang, en, ar) => (lang === 'ar' ? ar : en);
const card = (p) => p.locator('.root .card');
const keyIcon = (p) => p.locator('.root .key');
const menu = (p) => p.locator('.root .menu');

async function openSite(b, url, theme = 'light') {
  const p = await b.ctx.newPage();
  await p.emulateMedia({ colorScheme: theme });
  await p.goto(url);
  return p;
}

// ---------- the functional run (English, light) ----------

async function functional() {
  console.log('\nfunctional (Chromium + extension + mock)');
  const b = await launch('en');

  // 1. One-press pairing (SPEC §9.4 steps 1–4).
  const pop = await popup(b, null);
  const reach0 = await pop.locator('[data-testid=reach]').textContent();
  check(reach0.includes('Not connected'), `popup starts "Not connected" (${reach0})`);
  await pop.locator('#address').fill(ADDRESS);
  const pairTab = b.ctx.waitForEvent('page');
  await pop.locator('[data-testid=connect]').click();
  const kp = await pairTab;
  await kp.waitForLoadState();
  check(/#\/connect\?ext=Chrome(%20| )on/.test(kp.url()) && /&n=[A-Za-z0-9_-]{22}$/.test(kp.url()), `Keyra opens #/connect with the name and a 128-bit nonce (${kp.url().replace(/n=.*/, 'n=…')})`);
  // Pages an extension opens skip the context's init scripts: pick the web app's language here
  // (the reload also checks that the pairing listener is injected again).
  await kp.evaluate(() => localStorage.setItem('keyra.lang', 'en'));
  await kp.reload();
  await kp.locator('input[type=password]').first().fill(PASS);
  await kp.keyboard.press('Enter');
  await kp.locator('.sheet', { hasText: 'Connect browser extension' }).waitFor({ timeout: 10000 });
  check(await kp.locator('.sheet').getByText(/Connect “Chrome on/).isVisible(), 'the web app asks "Connect …?" with the extension name');
  const closed = kp.waitForEvent('close', { timeout: 15000 }).then(() => true, () => false);
  await kp.locator('.sheet button', { hasText: 'Create token' }).click();
  await kp.locator('.ready-ready').waitFor();
  check((await button()) === 'approved token_create', 'one press of Keyra approves the token');
  check(await closed, 'the Keyra tab closes by itself after handing over the token');
  const stored = await b.sw.evaluate(() => chrome.storage.local.get(['address', 'token']));
  check(stored.address === ADDRESS && /^keyra_[a-z2-7]{32}$/.test(stored.token), 'the extension stored the address and the token');
  await phoneUnlock();
  const toks = (await api('GET', '/api/tokens')).body.tokens;
  check(toks.some((t) => t.kind === 'extension' && t.scope === 'all' && /^Chrome on/.test(t.name)), 'Keyra lists it as a browser extension token');
  await pop.reload();
  await waitFor(async () => (await pop.locator('[data-testid=reach]').textContent()).includes('Ready'), 'popup shows "Ready"');
  await pop.close();

  // 2. The key in the field, the list of matching logins, arm and type.
  const gh = await openSite(b, at('github.com', '/login'));
  await gh.locator('#login_field').click();
  await keyIcon(gh).waitFor();
  const kb = await keyIcon(gh).boundingBox();
  const fb = await gh.locator('#login_field').boundingBox();
  check(kb && fb && kb.x > fb.x + fb.width / 2 && kb.x + kb.width <= fb.x + fb.width && kb.y >= fb.y && kb.y + kb.height <= fb.y + fb.height, 'the key sits inside the field, at its end');
  await keyIcon(gh).click();
  await menu(gh).locator('.item', { hasText: 'GitHub' }).waitFor();
  check((await menu(gh).locator('.item[data-id]').count()) === 1, 'the menu lists only the login for github.com');
  check(await gh.evaluate(() => document.activeElement?.id === 'login_field'), 'opening the menu keeps focus in the field');
  await menu(gh).locator('.item', { hasText: 'GitHub' }).click();
  await card(gh).getByText("Press Keyra's button").waitFor();
  check(await card(gh).getByText('Both · GitHub').isVisible(), 'the press card names what and which login (Both · GitHub)');
  check(await gh.evaluate(() => document.activeElement?.id === 'login_field'), 'the username field is focused for the typing');
  const count = (await card(gh).locator('.count').textContent()).trim();
  check(/^(1:00|0:5\d)$/.test(count), `a 60-second countdown runs (${count})`);
  const pend = (await api('GET', '/api/state')).body.pending;
  check(pend?.what === 'both' && pend.title === 'GitHub' && /^Chrome on/.test(pend.by) && pend.host === undefined, 'Keyra holds "both · GitHub" for the extension, no foreign host');
  check(/typ/.test(await button()), 'the press types it');
  await card(gh).getByText('Typed').waitFor({ timeout: 6000 });
  check(true, 'the card turns into "Typed"');
  await card(gh).waitFor({ state: 'detached', timeout: 4000 });

  // A plain sign-in with a login Keyra already has asks nothing.
  await gh.locator('#login_field').fill('hasanalaaa');
  await gh.locator('#password').fill('whatever-was-typed');
  await gh.locator('button[type=submit]').click();
  await gh.waitForURL('**/home');
  await sleep(1500);
  check((await card(gh).count()) === 0, 'a sign-in with an existing login asks nothing');

  // 3. Phishing guard: GitHub's login on another site only after a warning naming both hosts.
  const evil = await openSite(b, at('evil.example', '/login'));
  await evil.locator('#password').click();
  await keyIcon(evil).click();
  await menu(evil).getByText('No login saved for evil.example.').waitFor();
  await menu(evil).locator('.item', { hasText: 'Other login…' }).click();
  await menu(evil).locator('input[type=search]').fill('git');
  await menu(evil).locator('.item', { hasText: 'GitHub' }).click();
  await card(evil).getByText('This login is for another site').waitFor();
  const warnText = await card(evil).textContent();
  check(warnText.includes('github.com') && warnText.includes('evil.example'), 'the warning names both hosts');
  check((await api('GET', '/api/state')).body.pending === null, 'nothing is armed before the user confirms');
  await card(evil).locator('button', { hasText: 'Type it here anyway' }).click();
  await card(evil).getByText("Press Keyra's button").waitFor();
  const pend2 = (await api('GET', '/api/state')).body.pending;
  check(pend2?.host === 'evil.example' && pend2.what === 'password', 'armed with anyHost: Keyra shows the phone the page host');
  check(await evil.evaluate(() => document.activeElement?.id === 'password'), 'the password field is focused');
  await card(evil).locator('button', { hasText: 'Cancel' }).click();
  await card(evil).getByText('Cancelled').waitFor();
  check((await api('GET', '/api/state')).body.pending === null, 'Cancel withdraws it on Keyra');
  const act = (await api('GET', '/api/activity')).body.events;
  check(act.some((e) => e.kind === 'agent_armed' && e.detail === 5 && /→ evil\.example$/.test(e.title)), 'the activity log records the other site');
  await evil.close();

  // 4. Sign-up: a strong password from Keyra, then "Save to Keyra?" after the page changes.
  const ns = await openSite(b, at('newsite.example', '/signup'));
  await ns.locator('#email').fill('me@newsite.example');
  await ns.locator('#new').click();
  await keyIcon(ns).click();
  await menu(ns).locator('.item', { hasText: 'Strong password from Keyra' }).click();
  if (!(await waitFor(async () => (await ns.locator('#new').inputValue()).length === 20, 'the generated password fills the new-password field'))) console.log('    card:', await card(ns).textContent().catch(() => '(none)'));
  const gen = await ns.locator('#new').inputValue();
  check(gen === (await ns.locator('#confirm').inputValue()), 'and the confirmation field');
  check(!JSON.stringify(await entries()).includes('newsite'), 'generating stores nothing');
  await ns.locator('button[type=submit]').click();
  await ns.waitForURL('**/welcome');
  await card(ns).getByText('Save to Keyra?').waitFor({ timeout: 6000 });
  check(true, 'the save card survives the navigation');
  check((await card(ns).textContent()).includes('me@newsite.example'), 'it shows the username');
  const session = await b.sw.evaluate(() => chrome.storage.session.get(null));
  check(!JSON.stringify(session).includes(gen), 'the password is not in session storage');
  await card(ns).locator('button', { hasText: /^Save$/ }).click();
  await card(ns).getByText("Press Keyra's button").waitFor();
  check(await card(ns).getByText('Save · New Site').isVisible(), 'saving waits for the press (Save · New Site)');
  check((await button()) === 'approved agent_save', 'the press saves it');
  await card(ns).getByText('Saved to Keyra').waitFor({ timeout: 6000 });
  const saved = (await entries()).find((e) => e.title === 'New Site');
  check(saved?.url === `https://newsite.example:${site.port}` && saved.username === 'me@newsite.example', 'Keyra has the new login (origin only, the username)');

  // Never for this site.
  const ns2 = await openSite(b, at('newsite.example', '/login'));
  await ns2.locator('#login_field').fill('someone-else');
  await ns2.locator('#password').fill('pw-1');
  await ns2.locator('#password').press('Enter');
  await card(ns2).getByText('Save to Keyra?').waitFor({ timeout: 6000 });
  await card(ns2).locator('button', { hasText: 'Never for this site' }).click();
  const st = await b.sw.evaluate(() => chrome.storage.local.get('settings'));
  check(st.settings?.never?.includes('newsite.example'), '"Never for this site" remembers the host');
  await ns2.close();
  await ns.close();

  // 5. Change password on github.com → "Update the password for GitHub?".
  const before = (await api('GET', `/api/entries/${(await entries()).find((e) => e.title === 'GitHub').id}`)).body;
  await gh.goto(at('github.com', '/settings/password'));
  await gh.locator('#old').fill('gh!R3d-Lantern-Fox');
  await gh.locator('#new').fill('Brand-New-Pass-77');
  await gh.locator('#confirm').fill('Brand-New-Pass-77');
  await gh.locator('button[type=submit]').click();
  await gh.waitForURL('**/settings/saved');
  await card(gh).getByText('Update the password for GitHub?').waitFor({ timeout: 6000 });
  check(true, 'a new password for a login Keyra has → "Update the password for GitHub?"');
  await card(gh).locator('button', { hasText: 'Update' }).first().click();
  await card(gh).getByText('Update · GitHub').waitFor();
  await button();
  await card(gh).getByText('Password updated').waitFor({ timeout: 6000 });
  const after = (await api('GET', `/api/entries/${before.id}`)).body;
  check(after.history.length === before.history.length + 1, 'the old password went to its history');

  // 6. The popup: logins for this site, search, type into the page.
  await gh.goto(at('github.com', '/login'));
  const p2 = await popup(b, gh);
  await p2.locator('[data-testid=here] .row', { hasText: 'GitHub' }).waitFor();
  check(await p2.getByText('On github.com').isVisible(), 'popup: logins for the current site');
  await p2.locator('.search input').fill('zain');
  await p2.locator('[data-testid=results] .row').first().waitFor();
  check((await p2.locator('[data-testid=results] .row').count()) === 1, 'popup: search over every login');
  await p2.locator('.search input').fill('');
  const popupClosed = p2.waitForEvent('close').then(() => true, () => false);
  await p2.locator('[data-testid=here] .row', { hasText: 'GitHub' }).click();
  check(await popupClosed, '"Type into this page" closes the popup');
  await card(gh).getByText("Press Keyra's button").waitFor();
  check(true, 'and the press card appears in the page');
  await button('long');
  await card(gh).getByText('Cancelled').waitFor({ timeout: 6000 });
  check(true, 'a long press cancels it');

  // 7. Locked and revoked.
  await api('POST', '/api/lock', {});
  await gh.reload();
  await gh.locator('#password').click();
  await keyIcon(gh).click();
  // The list may come from the 15 s cache; arming is what meets the lock.
  const locked = menu(gh).getByText('Keyra is locked').or(menu(gh).locator('.item[data-id]').first());
  await locked.waitFor();
  if (await menu(gh).locator('.item[data-id]').count()) await menu(gh).locator('.item[data-id]').first().click();
  await gh.locator('.root').getByText('Keyra is locked').first().waitFor();
  check(await gh.locator('.root').getByRole('button', { name: 'Open Keyra' }).first().isVisible(), 'locked Keyra: says so and offers "Open Keyra"');
  await phoneUnlock();
  const tok = (await api('GET', '/api/tokens')).body.tokens.find((t) => t.kind === 'extension');
  await api('DELETE', `/api/tokens/${tok.id}`);
  const p3 = await popup(b, gh);
  await waitFor(async () => (await p3.locator('[data-testid=reach]').textContent()).includes('Not connected'), 'revoked token: popup says "Not connected"');
  check(await p3.getByText('Connection revoked').isVisible(), 'and explains the token was revoked');
  check(b.errors.length === 0, `no page errors${b.errors.length ? `: ${b.errors.join(' | ')}` : ''}`);
  await b.ctx.close();
  rmSync(b.dir, { recursive: true, force: true });
}

// ---------- screenshots (Arabic and English, light and dark) ----------

async function screenshots(lang) {
  console.log(`\nscreenshots (${lang})`);
  const b = await launch(lang);
  await phoneUnlock();
  const token = await makeToken(lang === 'ar' ? 'Chrome على Mac' : 'Chrome on Mac');
  await b.sw.evaluate(([address, token]) => chrome.storage.local.set({ address, token }), [ADDRESS, token]);

  for (const theme of ['light', 'dark']) {
    const tag = `${lang === 'en' ? '-en' : ''}${theme === 'dark' ? '-dark' : ''}`;
    const gh = await openSite(b, at('github.com', '/login'), theme);
    await gh.locator('#login_field').click();
    await keyIcon(gh).click();
    await menu(gh).locator('.item', { hasText: 'GitHub' }).waitFor();
    await shotPage(gh, `menu${tag}`);
    await menu(gh).locator('.item', { hasText: 'GitHub' }).click();
    await card(gh).locator('.ring').waitFor();
    await sleep(2500);
    await shotPage(gh, `press${tag}`);
    await button();
    await card(gh).locator('.tone-ok').waitFor({ timeout: 6000 });
    await shotPage(gh, `typed${tag}`);

    const evil = await openSite(b, at('evil.example', '/login'), theme);
    await evil.locator('#password').click();
    await keyIcon(evil).click();
    await menu(evil).locator('.item.special').last().click();
    await menu(evil).locator('input[type=search]').fill('git');
    await shotPage(evil, `other${tag}`);
    await menu(evil).locator('.item', { hasText: 'GitHub' }).click();
    await card(evil).locator('.tone-warn').waitFor();
    await shotPage(evil, `warning${tag}`);
    await evil.close();

    const ns = await openSite(b, at('newsite.example', '/signup'), theme);
    await ns.locator('#email').fill('me@newsite.example');
    await ns.locator('#new').click();
    await keyIcon(ns).click();
    await menu(ns).locator('.item.special').first().waitFor();
    await shotPage(ns, `generate${tag}`);
    await menu(ns).locator('.item.special').first().click();
    await waitFor(async () => (await ns.locator('#new').inputValue()).length > 0, 'generated');
    await ns.locator('button[type=submit]').click();
    await ns.waitForURL('**/welcome');
    await card(ns).locator('.kv').waitFor();
    await shotPage(ns, `save${tag}`);
    await card(ns).locator('.btn.secondary').click();
    await ns.close();

    await gh.goto(at('github.com', '/settings/password'));
    await gh.locator('#old').fill('x');
    await gh.locator('#new').fill(`Another-${theme}-Pass-1`);
    await gh.locator('#confirm').fill(`Another-${theme}-Pass-1`);
    await gh.locator('button[type=submit]').click();
    await gh.waitForURL('**/settings/saved');
    await card(gh).locator('.kv').waitFor();
    await shotPage(gh, `update${tag}`);
    await card(gh).locator('.btn.secondary').click();

    await gh.goto(at('github.com', '/login'));
    const p = await popup(b, gh, theme);
    await p.locator('[data-testid=here] .row').first().waitFor();
    await shotPopup(p, `popup${tag}`);
    await p.locator('.search input').fill(lang === 'ar' ? 'ب' : 'a');
    await p.locator('[data-testid=results] .row').first().waitFor();
    await shotPopup(p, `popup-search${tag}`);
    await p.locator('.seg-item').nth(1).click();
    await p.locator('[data-testid=generated]').waitFor();
    await shotPopup(p, `popup-generator${tag}`);
    await p.locator('.seg-item').nth(2).click();
    await shotPopup(p, `popup-settings${tag}`);
    await p.close();
    await gh.close();
  }
  // Not connected: the one-press pairing screen.
  await b.sw.evaluate(() => chrome.storage.local.remove('token'));
  for (const theme of ['light', 'dark']) {
    const p = await popup(b, null, theme);
    await p.locator('#address').fill('http://keyra.local');
    await shotPopup(p, `popup-connect${lang === 'en' ? '-en' : ''}${theme === 'dark' ? '-dark' : ''}`);
    await p.close();
  }
  check(b.errors.length === 0, `no page errors${b.errors.length ? `: ${b.errors.join(' | ')}` : ''}`);
  await b.ctx.close();
  rmSync(b.dir, { recursive: true, force: true });
}

let current = null;
try {
  await functional();
  if (WANT_SHOTS) {
    mkdirSync(SHOTS, { recursive: true });
    for (const tok of (await api('GET', '/api/tokens')).body.tokens ?? []) await api('DELETE', `/api/tokens/${tok.id}`);
    await screenshots('en');
    for (const tok of (await api('GET', '/api/tokens')).body.tokens ?? []) await api('DELETE', `/api/tokens/${tok.id}`);
    await screenshots('ar');
  }
} catch (e) {
  console.error(e);
  failures++;
  // Leave a picture of every open page for the failure.
  const dir = mkdtempSync(join(tmpdir(), 'keyra-e2e-fail-'));
  for (const [i, p] of (current?.ctx.pages() ?? []).entries()) await p.screenshot({ path: join(dir, `page-${i}.png`) }).catch(() => undefined);
  console.error(`screenshots of the open pages: ${dir}`);
} finally {
  mockProc.kill();
  site.stop();
}
console.log(failures ? `\n${failures} check(s) failed` : '\nall e2e checks passed');
process.exit(failures ? 1 : 0);
