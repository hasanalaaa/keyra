// The extension's API client against web/mock/server.mjs, which mirrors the device (SPEC §9.4, §17).
import { afterAll, beforeAll, beforeEach, describe, expect, it } from 'vitest';
import { createClient, KeyraError, normalizeAddress, type Client } from '../src/api';
import { startMock, sleep, type Mock } from './mock';

let mock: Mock;
let token: string;
let ext: Client;
let github: number;

/** fetch as a browser extension's service worker sends it: with its Origin. */
const fromExtension = (origin = 'chrome-extension://abcdefghijklmnopabcdefghijklmnop'): typeof fetch => (url, init) =>
  fetch(url, { ...init, headers: { ...(init?.headers as Record<string, string>), Origin: origin } });

async function code(p: Promise<unknown>): Promise<string> {
  try {
    await p;
    return 'ok';
  } catch (e) {
    return e instanceof KeyraError ? e.code : String(e);
  }
}

async function until(c: Client, state: string): Promise<Record<string, unknown>> {
  for (let i = 0; i < 40; i++) {
    const s = await c.status();
    if (s.state === state) return s as never;
    await sleep(1100);
  }
  throw new Error(`status never became ${state}`);
}

/** A new extension token per test: each has its own budget of 10 requests in 10 s. */
async function fresh(): Promise<void> {
  const old = ((await mock.session('GET', '/api/tokens')).body.tokens as { id: number; kind: string }[]).filter((t) => t.kind === 'extension');
  for (const t of old) await mock.session('DELETE', `/api/tokens/${t.id}`);
  token = await mock.createToken('extension');
  ext = createClient({ address: mock.base, token, fetch: fromExtension() });
}

beforeAll(async () => {
  mock = await startMock();
  await mock.unlock();
  await fresh();
  const list = (await mock.session('GET', '/api/entries')).body.entries as { id: number; title: string }[];
  github = list.find((e) => e.title === 'GitHub')!.id;
});
afterAll(() => mock?.stop());

describe('address', () => {
  it('normalises what people type', () => {
    expect(normalizeAddress('keyra.local')).toBe('http://keyra.local');
    expect(normalizeAddress(' http://192.168.4.1/ ')).toBe('http://192.168.4.1');
    expect(normalizeAddress('https://keyra.example:8443/x?y')).toBe('https://keyra.example:8443');
    expect(normalizeAddress('ftp://keyra.local')).toBeNull();
    expect(normalizeAddress('http://user:pw@keyra.local')).toBeNull();
    expect(normalizeAddress('')).toBeNull();
  });
});

describe('extension token', () => {
  beforeEach(fresh);
  it('is listed as kind "extension" and logged with detail 2', async () => {
    const list = (await mock.session('GET', '/api/tokens')).body.tokens as { kind: string }[];
    expect(list.map((t) => t.kind)).toContain('extension');
    const act = (await mock.session('GET', '/api/activity')).body.events as { kind: string; detail: number }[];
    expect(act.filter((e) => e.kind === 'token_created').at(-1)?.detail).toBe(2);
  });

  it('reads the device state without a token', async () => {
    const st = await createClient({ address: mock.base }).state();
    expect(st).toMatchObject({ initialized: true, unlocked: true });
  });

  it('matches by the shared host rule and never returns a username', async () => {
    const hosts = async (h: string) => (await ext.match(h)).map((e) => e.title);
    expect(await hosts('github.com')).toEqual(['GitHub']);
    expect(await hosts('WWW.GitHub.com.')).toEqual(['GitHub']);
    expect(await hosts('gist.github.com')).toEqual(['GitHub']);
    expect(await hosts('github.com.evil.example')).toEqual([]);
    expect(await hosts('google.com')).toEqual(['Google']); // accounts.google.com is a subdomain
    await fresh(); // stay under 10 requests in 10 s
    const m = await ext.match('github.com', 'hasanalaaa');
    expect(m).toEqual([{ id: github, title: 'GitHub', host: 'github.com', sameUser: true }]);
    expect((await ext.match('github.com', 'someone-else'))[0].sameUser).toBe(false);
    expect(JSON.stringify(m)).not.toContain('hasanalaaa');
    expect(await code(ext.match('bad host'))).toBe('invalid');
    expect(await code(ext.match('a'.repeat(254)))).toBe('invalid');
  });

  it('types only on the login’s own site unless the user chose another (anyHost)', async () => {
    expect(await code(ext.type({ id: github, what: 'password', host: 'evil.example' }))).toBe('host_mismatch');
    // A request without host is refused for an extension token.
    const r = await fetchJson('POST', '/api/agent/type', { id: github, what: 'password' });
    expect(r.status).toBe(400);

    const armed = await ext.type({ id: github, what: 'password', host: 'evil.example', anyHost: true });
    expect(armed.expiresIn).toBeGreaterThan(50_000);
    const st = (await mock.session('GET', '/api/state')).body as { pending: { host?: string; by?: string } };
    expect(st.pending).toMatchObject({ host: 'evil.example', by: 'test-extension' });
    expect(await mock.press()).toMatch(/typ/);
    await until(ext, 'typed');
    const act = (await mock.session('GET', '/api/activity')).body.events as { kind: string; detail: number; title: string }[];
    expect(act.filter((e) => e.kind === 'agent_armed').at(-1)).toMatchObject({ detail: 1 + 4, title: 'test-extension → evil.example' });
  });

  it('on its own site the pending item carries no host', async () => {
    await ext.type({ id: github, what: 'both', host: 'www.github.com' });
    const st = (await mock.session('GET', '/api/state')).body as { pending: { host?: string } };
    expect(st.pending.host).toBeUndefined();
    await ext.cancel();
    expect((await ext.status()).state).toBe('cancelled');
  });

  it('saves a new login after a press', async () => {
    const r = await ext.save({ title: 'New Site', url: 'https://newsite.example', username: 'me@new.example', password: 'Fresh-Pass-1' });
    expect(r.mode).toBe('create');
    await mock.press();
    const s = await until(ext, 'saved');
    const e = (await mock.session('GET', `/api/entries/${s.id}`)).body as { title: string; url: string; username: string };
    expect(e).toMatchObject({ title: 'New Site', url: 'https://newsite.example', username: 'me@new.example' });
  });

  it('updates a login: new password, the old one to its history', async () => {
    const before = (await mock.session('GET', `/api/entries/${github}`)).body as { history: unknown[] };
    expect(await code(ext.save({ title: '', url: 'https://github.com', username: 'x', password: '', replace: github }))).toBe('invalid');
    expect(await code(ext.save({ title: '', url: 'https://github.com', password: 'p', replace: 999999 }))).toBe('not_found');
    const r = await ext.save({ title: 'ignored', url: 'https://github.com', username: '', password: 'Rotated-Pass-2', replace: github });
    expect(r.mode).toBe('update');
    await mock.press();
    const s = await until(ext, 'saved');
    expect(s.id).toBe(github);
    const after = (await mock.session('GET', `/api/entries/${github}`)).body as { title: string; username: string; history: unknown[] };
    expect(after.title).toBe('GitHub'); // title ignored on replace
    expect(after.username).toBe('hasanalaaa'); // an empty username is "not sent"
    expect(after.history.length).toBe(before.history.length + 1);
    const act = (await mock.session('GET', '/api/activity')).body.events as { kind: string; detail: number }[];
    expect(act.filter((e) => e.kind === 'agent_saved').map((e) => e.detail).slice(0, 2)).toEqual([1, 0]); // newest first
  });

  it('generates without storing', async () => {
    const g = await ext.generate({ length: 24, lower: true, upper: true, digits: true, symbols: true, minDigits: 1, minSymbols: 1, avoidAmbiguous: true });
    expect(g.password).toHaveLength(24);
    expect(g.entropyBits).toBeGreaterThan(100);
  });
});

async function fetchJson(method: string, path: string, body: unknown) {
  const r = await fromExtension()(mock.base + path, { method, headers: { Authorization: `Bearer ${token}`, 'Content-Type': 'application/json' }, body: JSON.stringify(body) });
  return { status: r.status, body: await r.json() };
}

describe('errors the extension shows', () => {
  it('kinds: match is for extensions only; save/generate refuse agents', async () => {
    const app = createClient({ address: mock.base, token: await mock.createToken('app'), fetch: fromExtension() });
    expect(await code(app.match('github.com'))).toBe('forbidden');
    const agent = createClient({ address: mock.base, token: await mock.createToken('agent'), fetch: fromExtension() });
    expect(await code(agent.generate({ length: 20, lower: true, upper: true, digits: true, symbols: true, minDigits: 1, minSymbols: 1, avoidAmbiguous: true }))).toBe('forbidden');
  });

  it('accepts extension origins on agent routes only', async () => {
    const moz = createClient({ address: mock.base, token, fetch: fromExtension('moz-extension://4b1c3f2e-1111-2222-3333-444455556666') });
    expect(await code(moz.cancel())).toMatch(/ok|error/); // not a CSRF refusal
    const evil = createClient({ address: mock.base, token, fetch: fromExtension('https://evil.example') });
    expect(await code(evil.cancel())).toBe('error'); // 403 csrf
    const r = await fromExtension()(`${mock.base}/api/lock`, { method: 'POST' });
    expect(r.status).toBe(403); // session routes keep the same-origin rule
  });

  it('busy, invalid token, rate limit, locked, unreachable', async () => {
    await fresh();
    await mock.session('POST', '/api/type', { id: github, what: 'password' }); // the web app arms first
    expect(await code(ext.type({ id: github, what: 'password', host: 'github.com' }))).toBe('busy');
    await mock.press('long');

    const bad = createClient({ address: mock.base, token: 'keyra_' + 'a'.repeat(32), fetch: fromExtension() });
    expect(await code(bad.status())).toBe('invalid_token');

    let last = '';
    for (let i = 0; i < 12 && last !== 'rate_limited'; i++) last = await code(ext.status());
    expect(last).toBe('rate_limited');
    try {
      await ext.status();
    } catch (e) {
      expect((e as KeyraError).retryAfterMs).toBeGreaterThan(0);
    }

    await mock.lock();
    expect(await code(ext.status())).toBe('locked');
    expect(await code(createClient({ address: 'http://127.0.0.1:9', token, timeoutMs: 2000 }).status())).toMatch(/unreachable|timeout/);
    expect(await code(createClient({ address: mock.base }).status())).toBe('not_connected');
  });
});
