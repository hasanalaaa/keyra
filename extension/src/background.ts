// Service worker (Firefox: event page). The only part that talks to Keyra: it keeps the address,
// the access token and the settings in storage.local, a pending save card per tab in
// storage.session, and a typed password only in memory, for at most two minutes (SPEC §9.4).
import { createClient, GEN_DEFAULTS, KeyraError, normalizeAddress, TOKEN_RE, type Client, type Login } from './api';
import { ext } from './ext';
import { normalizeHost } from './host';
import type { Failure, PageConfig, PageMsg, PopupMsg, PopupState, Reach, SaveCard, Settings } from './messages';
import { pairListener } from './pair';

const HOLD_MS = 120_000;
const DEFAULTS: Settings = { showIcons: true, offerSave: true, never: [], gen: GEN_DEFAULTS };

interface Stored {
  address?: string;
  token?: string;
  settings: Settings;
}

async function stored(): Promise<Stored> {
  const s = (await ext.storage.local.get(['address', 'token', 'settings'])) as Partial<Stored>;
  return { address: s.address, token: s.token, settings: { ...DEFAULTS, ...(s.settings ?? {}) } };
}

async function client(): Promise<Client> {
  const s = await stored();
  if (!s.address || !s.token) throw new KeyraError('not_connected');
  return createClient({ address: s.address, token: s.token });
}

async function failure(e: unknown): Promise<Failure> {
  const { address } = await stored();
  if (e instanceof KeyraError) return { ok: false, code: e.code, message: e.message, retryAfterMs: e.retryAfterMs, address };
  return { ok: false, code: 'error', message: String((e as Error)?.message ?? e), address };
}

const originOf = (url?: string) => {
  try {
    return url ? new URL(url).origin : '';
  } catch {
    return '';
  }
};
const hostOf = (url?: string) => {
  try {
    return url ? normalizeHost(new URL(url).hostname.replace(/^\[|\]$/g, '')) : '';
  } catch {
    return '';
  }
};

// ---------- per-tab state ----------

/** The tab whose request is the token's "last request" on Keyra (status is per token, not per tab). */
let owner: { tabId: number; kind: 'type' | 'save'; host: string } | null = null;
/** Passwords from a sent form, until Save / Not now (memory only). */
const held = new Map<number, { password: string; at: number }>();
/** Keyra typed a login here: a sign-in right after it is not a new login to save. */
const typedIn = new Map<number, { host: string; at: number }>();
/** The username from a "username first" step, for the password step after it. */
const lastUser = new Map<number, { host: string; username: string; at: number }>();

interface SaveRecord {
  card: SaveCard;
  url: string;
  at: number;
  loads: number;
}
const saveKey = (tabId: number) => `save:${tabId}`;

async function dropSave(tabId: number): Promise<void> {
  held.delete(tabId);
  await ext.storage.session.remove(saveKey(tabId));
}

function sweep(): void {
  const now = Date.now();
  for (const [k, v] of held) if (now - v.at > HOLD_MS) void dropSave(k);
  for (const [k, v] of typedIn) if (now - v.at > HOLD_MS) typedIn.delete(k);
  for (const [k, v] of lastUser) if (now - v.at > 5 * 60_000) lastUser.delete(k);
}

// ---------- pairing (SPEC §9.4 steps 1–4) ----------

interface Pairing {
  address: string;
  nonce: string;
  tabId: number;
  at: number;
}
const PAIR_MS = 5 * 60_000;

async function pairing(): Promise<Pairing | null> {
  const p = (await ext.storage.session.get('pairing')).pairing as Pairing | undefined;
  return p && Date.now() - p.at < PAIR_MS ? p : null;
}

function nonce(): string {
  const b = crypto.getRandomValues(new Uint8Array(16));
  return btoa(String.fromCharCode(...b)).replace(/\+/g, '-').replace(/\//g, '_').replace(/=+$/, '');
}

/** "Chrome on Mac" — how the token is listed in Keyra → Settings → Apps and agents. */
export function browserLabel(ua: string): string {
  const has = (x: string) => ua.includes(x);
  const b = has('Edg/') ? 'Edge' : has('OPR/') ? 'Opera' : has('Vivaldi') ? 'Vivaldi' : has('Firefox/') ? 'Firefox' : has('Chrome/') ? 'Chrome' : has('Safari/') ? 'Safari' : 'Browser';
  const p = has('Mac OS X') || has('Macintosh') ? 'Mac' : has('Windows') ? 'Windows' : has('CrOS') ? 'ChromeOS' : has('Android') ? 'Android' : has('Linux') ? 'Linux' : '';
  return p ? `${b} on ${p}` : b;
}

async function inject(tabId: number, p: Pairing): Promise<void> {
  try {
    await ext.scripting.executeScript({ target: { tabId }, func: pairListener, args: [p.nonce, p.address] });
  } catch (e) {
    console.warn('Keyra: could not prepare the pairing tab', e);
  }
}

ext.tabs.onUpdated.addListener((tabId, info) => {
  if (info.status !== 'complete') return;
  void pairing().then((p) => {
    if (p && p.tabId === tabId) void inject(tabId, p);
  });
});

ext.tabs.onRemoved.addListener((tabId) => {
  void dropSave(tabId);
  typedIn.delete(tabId);
  lastUser.delete(tabId);
  if (owner?.tabId === tabId) owner = null;
  void pairing().then((p) => {
    if (p?.tabId === tabId) void ext.storage.session.remove('pairing');
  });
});

async function startPairing(input: string): Promise<{ ok: true } | Failure> {
  const address = normalizeAddress(input);
  if (!address) return { ok: false, code: 'invalid' };
  if (!(await ext.permissions.contains({ origins: [`${address}/*`] }))) return { ok: false, code: 'forbidden', address };
  try {
    const st = await createClient({ address }).state();
    if (!st.initialized) return { ok: false, code: 'not_keyra', message: 'uninitialized', address };
  } catch (e) {
    return { ...(await failure(e)), address };
  }
  const n = nonce();
  const name = browserLabel(navigator.userAgent);
  const tab = await ext.tabs.create({ url: `${address}/#/connect?ext=${encodeURIComponent(name)}&n=${n}` });
  if (tab.id === undefined) return { ok: false, code: 'error' };
  await ext.storage.local.set({ address });
  await ext.storage.session.set({ pairing: { address, nonce: n, tabId: tab.id, at: Date.now() } satisfies Pairing });
  return { ok: true };
}

async function finishPairing(sender: chrome.runtime.MessageSender, n: string, token: string): Promise<{ ok: true } | Failure> {
  const p = await pairing();
  // Only the tab we opened, still on the address the user typed, with our nonce.
  if (!p || n !== p.nonce || sender.tab?.id !== p.tabId || originOf(sender.url) !== p.address || !TOKEN_RE.test(token)) {
    return { ok: false, code: 'invalid' };
  }
  try {
    await createClient({ address: p.address, token }).match('keyra.invalid');
  } catch (e) {
    return failure(e);
  }
  await ext.storage.local.set({ address: p.address, token });
  await ext.storage.session.remove('pairing');
  matchCache.clear();
  void ext.tabs.remove(p.tabId).catch(() => undefined);
  return { ok: true };
}

// ---------- matching (cached briefly: Keyra allows 10 requests per 10 s) ----------

const matchCache = new Map<string, { at: number; entries: Login[] }>();
async function match(host: string, username?: string): Promise<Login[]> {
  const key = `${host}\u0000${username ?? '\u0001'}`;
  const hit = matchCache.get(key);
  if (hit && Date.now() - hit.at < 15_000) return hit.entries;
  const entries = await (await client()).match(host, username);
  matchCache.set(key, { at: Date.now(), entries });
  if (matchCache.size > 50) matchCache.delete(matchCache.keys().next().value!);
  return entries;
}

// ---------- page requests ----------

async function onPage(msg: PageMsg, sender: chrome.runtime.MessageSender): Promise<unknown> {
  const tabId = sender.tab?.id ?? -1;
  const host = hostOf(sender.url);
  const s = await stored();
  sweep();
  try {
    switch (msg.t) {
      case 'config':
        return { connected: !!(s.address && s.token), showIcons: s.settings.showIcons, self: !!s.address && originOf(sender.url) === s.address, address: s.address } satisfies PageConfig;
      case 'match':
        return { ok: true, host, entries: await match(host, msg.username) };
      case 'entries':
        return { ok: true, host, entries: await (await client()).entries() };
      case 'type': {
        const r = await (await client()).type({ id: msg.id, what: msg.what, host, anyHost: msg.anyHost === true });
        owner = { tabId, kind: 'type', host };
        return { ok: true, ...r };
      }
      case 'status': {
        if (owner && owner.tabId !== tabId) return { ok: true, status: { state: 'cancelled' } };
        const status = await (await client()).status();
        if (status.state === 'typed' && owner?.kind === 'type') typedIn.set(tabId, { host: owner.host, at: Date.now() });
        if (status.state === 'saved') matchCache.clear();
        return { ok: true, status };
      }
      case 'cancel':
        if (owner?.tabId === tabId) await (await client()).cancel().catch((e) => (e instanceof KeyraError && e.status === 409 ? undefined : Promise.reject(e)));
        return { ok: true };
      case 'generate':
        return { ok: true, ...(await (await client()).generate(s.settings.gen)) };
      case 'offer':
        return { ok: true, card: await offer(tabId, sender.url ?? '', host, msg, s.settings) };
      case 'pending':
        return { ok: true, card: await pendingCard(tabId) };
      case 'decide':
        return await decide(tabId, msg.decision, s.settings);
      case 'keepalive':
        return { ok: true };
      case 'username':
        // A page that asks for the username first passes it on to its password step.
        if (typeof msg.username === 'string') lastUser.set(tabId, { host, username: msg.username.slice(0, 256), at: Date.now() });
        return { ok: true };
      case 'openKeyra':
        if (s.address) await ext.tabs.create({ url: `${s.address}/`, index: (sender.tab?.index ?? 0) + 1 });
        return { ok: true };
      case 'paired':
        return await finishPairing(sender, msg.n, msg.token);
    }
  } catch (e) {
    return failure(e);
  }
}

async function offer(
  tabId: number,
  url: string,
  host: string,
  snap: { username: string; password: string; isNew: boolean; title: string },
  settings: Settings,
): Promise<SaveCard | null> {
  if (!settings.offerSave || !host || !snap.password || settings.never.includes(host)) return null;
  const typed = typedIn.get(tabId);
  if (!snap.isNew && typed?.host === host) return null; // Keyra just typed this sign-in
  let username = snap.username;
  const remembered = lastUser.get(tabId);
  if (!username && remembered?.host === host) username = remembered.username;
  let matches: Login[];
  try {
    matches = await match(host, username);
  } catch {
    return null; // locked or offline: ask nothing rather than guess
  }
  const same = matches.find((m) => m.sameUser);
  let card: SaveCard;
  if (same) {
    if (!snap.isNew) return null; // a plain sign-in with a login Keyra already has
    card = { mode: 'update', host, title: same.title, username, replace: same.id };
  } else {
    if (!snap.isNew && !username && matches.length > 0) return null; // most likely a sign-in with one of those
    card = { mode: 'create', host, title: snap.title || host, username };
  }
  held.set(tabId, { password: snap.password, at: Date.now() });
  await ext.storage.session.set({ [saveKey(tabId)]: { card, url: originOf(url), at: Date.now(), loads: 1 } satisfies SaveRecord });
  return card;
}

async function pendingCard(tabId: number): Promise<SaveCard | null> {
  const rec = (await ext.storage.session.get(saveKey(tabId)))[saveKey(tabId)] as SaveRecord | undefined;
  if (!rec) return null;
  // Shown on the page that sent the form and on one page after it, within two minutes.
  if (Date.now() - rec.at > HOLD_MS || rec.loads >= 2 || !held.has(tabId)) {
    await dropSave(tabId);
    return null;
  }
  await ext.storage.session.set({ [saveKey(tabId)]: { ...rec, loads: rec.loads + 1 } });
  return rec.card;
}

async function decide(tabId: number, decision: 'save' | 'later' | 'never', settings: Settings): Promise<unknown> {
  const rec = (await ext.storage.session.get(saveKey(tabId)))[saveKey(tabId)] as SaveRecord | undefined;
  if (decision !== 'save' || !rec) {
    if (decision === 'never' && rec && !settings.never.includes(rec.card.host)) {
      await ext.storage.local.set({ settings: { ...settings, never: [...settings.never, rec.card.host].sort() } });
    }
    await dropSave(tabId);
    return { ok: true };
  }
  const pw = held.get(tabId);
  if (!pw) {
    await dropSave(tabId);
    return { ok: false, code: 'not_found' };
  }
  const { card } = rec;
  const r = await (await client()).save({ title: card.title, url: rec.url, username: card.username || undefined, password: pw.password, replace: card.replace });
  owner = { tabId, kind: 'save', host: card.host };
  await dropSave(tabId); // sent once; Keyra holds it until the press
  return { ok: true, ...r };
}

// ---------- popup requests ----------

async function reach(s: Stored): Promise<Reach> {
  if (!s.address || !s.token) return 'not_connected';
  try {
    const st = await createClient({ address: s.address }).state();
    if (!st.initialized) return 'uninitialized';
    if (!st.unlocked) return 'locked';
    await createClient({ address: s.address, token: s.token }).status();
    return 'ready';
  } catch (e) {
    const code = e instanceof KeyraError ? e.code : 'error';
    if (code === 'invalid_token' || code === 'locked' || code === 'not_keyra') return code;
    if (code === 'rate_limited') return 'ready';
    return 'unreachable';
  }
}

async function onPopup(msg: PopupMsg): Promise<unknown> {
  const s = await stored();
  try {
    switch (msg.t) {
      case 'popup':
        return { address: s.address, connected: !!(s.address && s.token), reach: await reach(s), pairing: !!(await pairing()), settings: s.settings } satisfies PopupState;
      case 'pair':
        return await startPairing(msg.address);
      case 'paste': {
        const address = normalizeAddress(msg.address);
        const token = msg.token.trim();
        if (!address || !TOKEN_RE.test(token)) return { ok: false, code: 'invalid' };
        if (!(await ext.permissions.contains({ origins: [`${address}/*`] }))) return { ok: false, code: 'forbidden', address };
        await createClient({ address, token }).match('keyra.invalid');
        await ext.storage.local.set({ address, token });
        matchCache.clear();
        return { ok: true };
      }
      case 'popupMatch':
        return { ok: true, entries: await match(normalizeHost(msg.host)) };
      case 'popupEntries':
        return { ok: true, entries: await (await client()).entries() };
      case 'popupGenerate':
        await ext.storage.local.set({ settings: { ...s.settings, gen: msg.gen } });
        return { ok: true, ...(await (await client()).generate(msg.gen)) };
      case 'settings':
        await ext.storage.local.set({ settings: { ...s.settings, ...msg.patch } });
        return { ok: true };
      case 'disconnect':
        await ext.storage.local.remove('token');
        matchCache.clear();
        owner = null;
        return { ok: true };
    }
  } catch (e) {
    return failure(e);
  }
}

// ---------- wiring ----------

const extPage = (url?: string) => !!url && url.startsWith(ext.runtime.getURL(''));

ext.runtime.onMessage.addListener((msg: { t?: string }, sender, sendResponse) => {
  if (sender.id !== ext.runtime.id || !msg || typeof msg.t !== 'string') return false;
  const work = extPage(sender.url) ? onPopup(msg as PopupMsg) : onPage(msg as PageMsg, sender);
  void work.then(sendResponse, async (e) => sendResponse(await failure(e)));
  return true;
});
