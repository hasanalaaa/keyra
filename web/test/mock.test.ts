// The device mock (mock/server.mjs) against the API rules of the October 2026 security review
// (docs/research/SECURITY-REVIEW-2026-10.md): what a stolen session can and cannot learn or do
// without a press of Keyra's button. The firmware's own tests are in firmware/components/*/host_test.
import { spawn, type ChildProcess } from 'node:child_process';
import { createCipheriv, pbkdf2Sync, randomBytes } from 'node:crypto';
import { createServer } from 'node:net';
import { fileURLToPath } from 'node:url';
import { afterAll, beforeAll, describe, expect, it } from 'vitest';

const MOCK = fileURLToPath(new URL('../mock/server.mjs', import.meta.url));
const PASS = 'keyra demo vault';
let mock: ChildProcess;
let base = '';

function freePort(): Promise<number> {
  return new Promise((resolve, reject) => {
    const srv = createServer();
    srv.on('error', reject);
    srv.listen(0, () => {
      const { port } = srv.address() as { port: number };
      srv.close(() => resolve(port));
    });
  });
}

beforeAll(async () => {
  const port = await freePort();
  base = `http://localhost:${port}`; // "localhost" = Keyra's own Wi-Fi: no trusted-browser step
  mock = spawn(process.execPath, [MOCK], { env: { ...process.env, PORT: String(port) }, stdio: ['ignore', 'pipe', 'inherit'] });
  await new Promise<void>((resolve, reject) => {
    mock.stdout!.on('data', (d) => String(d).includes('Keyra mock on') && resolve());
    mock.on('exit', (code) => reject(new Error(`mock exited (${code})`)));
  });
});
afterAll(() => {
  mock?.kill();
});

interface Reply {
  status: number;
  body: any; // eslint-disable-line @typescript-eslint/no-explicit-any
}

/** One browser: its own session cookie and CSRF token. */
class Browser {
  cookie = '';
  csrf = '';
  async call(method: string, path: string, body?: unknown, { time = Date.now(), csrf = true } = {}): Promise<Reply> {
    const headers: Record<string, string> = { 'X-Keyra-Time': String(time) };
    if (body !== undefined) headers['Content-Type'] = 'application/json';
    if (this.cookie) headers.Cookie = this.cookie;
    if (csrf && this.csrf) headers['X-Keyra-CSRF'] = this.csrf;
    const res = await fetch(base + '/api' + path, { method, headers, body: body === undefined ? undefined : JSON.stringify(body) });
    const set = res.headers.get('set-cookie');
    if (set) this.cookie = set.split(';')[0];
    return { status: res.status, body: res.status === 204 ? null : await res.json() };
  }
  async unlock(): Promise<void> {
    const r = await this.call('POST', '/unlock', { passphrase: PASS });
    expect(r.status).toBe(200);
    this.csrf = r.body.csrf;
  }
  async revealWithPress(id: number): Promise<Reply> {
    const first = await this.call('POST', `/entries/${id}/reveal`);
    expect(first.status).toBe(202);
    await press();
    return this.call('POST', `/entries/${id}/reveal`);
  }
}

/** A press of Keyra's button; resolves once what it approved has run (the mock commits after 300 ms). */
async function press(kind: 'short' | 'long' = 'short'): Promise<string> {
  const r = await fetch(`${base}/__mock/button`, { method: 'POST', body: JSON.stringify({ press: kind }) });
  const result = ((await r.json()) as { result: string }).result;
  for (let i = 0; i < 100; i++) {
    const s = (await (await fetch(`${base}/api/state`)).json()) as { presence: { awaiting: boolean; op: string | null } };
    if (!s.presence.awaiting && s.presence.op === null) break;
    await new Promise((res) => setTimeout(res, 50));
  }
  return result;
}

/** Withdraws whatever waits for the button (a long press), so the next test starts clean. */
const cancelPress = () => press('long');

async function firstWith(b: Browser, want: 'hasPassword' | 'hasTotp'): Promise<number> {
  const list = await b.call('GET', '/entries');
  const e = list.body.entries.find((x: Record<string, unknown>) => x[want]);
  expect(e).toBeTruthy();
  return e.id;
}

describe('F1: a password edit is no oracle without a press', () => {
  it('answers a wrong and a right guess alike, and changes nothing', async () => {
    const owner = new Browser();
    await owner.unlock();
    const thief = new Browser();
    await thief.unlock();
    const id = await firstWith(owner, 'hasPassword');
    const real = (await owner.revealWithPress(id)).body.password as string;
    const before = (await thief.call('GET', `/entries/${id}`)).body;

    const wrong = await thief.call('PUT', `/entries/${id}`, { password: real + '-guess' });
    await cancelPress();
    const right = await thief.call('PUT', `/entries/${id}`, { password: real });
    await cancelPress();
    for (const r of [wrong, right]) {
      expect(r.status).toBe(202);
      expect(r.body.awaiting).toBe('button');
      expect(r.body.op).toBe('reveal');
    }
    const after = (await thief.call('GET', `/entries/${id}`)).body;
    expect(after.history).toEqual(before.history);
    expect(after.updated).toBe(before.updated);
    // Fields other than the password still save at once.
    expect((await thief.call('PUT', `/entries/${id}`, { favorite: !before.favorite })).status).toBe(200);
  });

  it('lets the session that was shown the password save a new one, once per version', async () => {
    const b = new Browser();
    await b.unlock();
    const id = await firstWith(b, 'hasPassword');
    const shown = await b.revealWithPress(id);
    expect(shown.status).toBe(200);
    const saved = await b.call('PUT', `/entries/${id}`, { password: 'Fresh-Pass-2026!' });
    expect(saved.status).toBe(200);
    const now = (await b.revealWithPress(id)).body;
    expect(now.password).toBe('Fresh-Pass-2026!');
    expect(now.history[0].password).toBe(shown.body.password);
    // The account changed since that reveal: a press approves the next new password.
    expect((await b.call('PUT', `/entries/${id}`, { favorite: true })).status).toBe(200);
    const asks = await b.call('PUT', `/entries/${id}`, { password: 'Second-Pass-2026!' });
    expect(asks.status).toBe(202);
    await press();
    expect((await b.call('PUT', `/entries/${id}`, { password: 'Second-Pass-2026!' })).status).toBe(200);
  });
});

describe('F2: a session cannot move the clock to harvest 2FA codes', () => {
  it('two /totp calls 60 s apart, an hour ahead, both answer the current code', async () => {
    const b = new Browser();
    await b.unlock(); // trusted: sets the clock
    const id = await firstWith(b, 'hasTotp');
    let now = await b.call('GET', `/entries/${id}/totp`);
    if (now.body.remaining < 5) {
      await new Promise((r) => setTimeout(r, (now.body.remaining + 1) * 1000));
      now = await b.call('GET', `/entries/${id}/totp`);
    }
    const hour = Date.now() + 3600000;
    const a = await b.call('GET', `/entries/${id}/totp`, undefined, { time: hour });
    const c = await b.call('GET', `/entries/${id}/totp`, undefined, { time: hour + 60000 });
    expect(a.body.code).toBe(now.body.code);
    expect(c.body.code).toBe(now.body.code);
    // Within the 5-minute bound a correction is still taken (a phone a little off)…
    const ahead = async (ms: number) => b.call('GET', `/entries/${id}/totp`, undefined, { time: Date.now() + ms });
    let near = await ahead(120000);
    if (near.body.remaining < 5) {
      await new Promise((r) => setTimeout(r, (near.body.remaining + 1) * 1000));
      near = await ahead(120000);
    }
    expect(near.status).toBe(200);
    // …measured from the trusted time, so a second step cannot walk it further.
    const walk = await ahead(120000 + 240000);
    expect(walk.body.code).toBe(near.body.code);
    expect(Math.abs(walk.body.remaining - near.body.remaining)).toBeLessThanOrEqual(1);
    await ahead(0); // back to the right time for the tests after this one
  });
});

function craftBackup(passphrase: string, counter: number, title: string): unknown {
  const salt = randomBytes(16);
  const iv = randomBytes(12);
  const c = createCipheriv('aes-256-gcm', pbkdf2Sync(passphrase, salt, 1000, 32, 'sha256'), iv);
  const record = Buffer.from(JSON.stringify({ id: 4242, rpId: 'evil.example', userName: 'x', displayName: '', created: 0, cred: randomBytes(16).toString('base64') })).toString('base64');
  const entries = [{ title, url: '', username: 'mallory', password: 'planted', totp: '', notes: '', favorite: false, created: 1, updated: 1 }];
  const plain = JSON.stringify({ entries, passkeys: { keys: [randomBytes(32).toString('base64')], records: [record], counter } });
  const data = Buffer.concat([c.update(plain), c.final(), c.getAuthTag()]);
  return { format: 'keyra-backup', v: 3, kdf: { alg: 'pbkdf2-sha256', iter: 1000, salt: salt.toString('base64') }, iv: iv.toString('base64'), data: data.toString('base64') };
}

describe('F3: a merge restore needs the press too', () => {
  it('adds nothing before the press', async () => {
    const b = new Browser();
    await b.unlock();
    const count = async () => (await b.call('GET', '/entries')).body.entries.length;
    const n = await count();
    const r = await b.call('POST', '/restore', { passphrase: 'backup pass phrase', backup: craftBackup('backup pass phrase', 5, 'Planted'), mode: 'merge' });
    expect(r.status).toBe(202);
    expect(r.body.op).toBe('restore');
    expect(await count()).toBe(n);
    await cancelPress();
    expect(await count()).toBe(n);
    const again = await b.call('POST', '/restore', { passphrase: 'backup pass phrase', backup: craftBackup('backup pass phrase', 5, 'Planted'), mode: 'merge' });
    expect(again.status).toBe(202);
    await press();
    expect(await count()).toBe(n + 1);
  });

  it('refuses a passkey counter above 2^31 - 1', async () => {
    const b = new Browser();
    await b.unlock();
    for (const counter of [0xffffffff, 0x80000000]) {
      const r = await b.call('POST', '/restore', { passphrase: 'backup pass phrase', backup: craftBackup('backup pass phrase', counter, 'Counter'), mode: 'merge' });
      expect(r.status).toBe(400);
      expect(r.body.error).toBe('invalid');
    }
  });
});

describe('F11: factory reset while unlocked needs a session and its CSRF token', () => {
  it('is refused to strangers and to a session without CSRF', async () => {
    const owner = new Browser();
    await owner.unlock();
    const stranger = new Browser();
    const r = await stranger.call('POST', '/factory-reset');
    expect(r.status).toBe(401);
    expect(r.body.error).toBe('session_required');
    expect((await owner.call('POST', '/factory-reset', undefined, { csrf: false })).status).toBe(403);
    const armed = await owner.call('POST', '/factory-reset');
    expect(armed.status).toBe(202);
    await cancelPress();
  });

  it('stays open while locked (a forgotten passphrase)', async () => {
    const owner = new Browser();
    await owner.unlock();
    expect((await owner.call('POST', '/lock')).status).toBe(204);
    const stranger = new Browser();
    const r = await stranger.call('POST', '/factory-reset');
    expect(r.status).toBe(202);
    await cancelPress();
  });
});

describe('F13: the Wi-Fi password answers alike whether a guess is right', () => {
  it('arms the press for the current password as for any other', async () => {
    const b = new Browser();
    await b.unlock();
    const same = await b.call('PUT', '/settings', { wifiPassword: 'Tigris-42-Kx9p' }); // the seeded one
    await cancelPress();
    const other = await b.call('PUT', '/settings', { wifiPassword: 'Another-Guess-1' });
    await cancelPress();
    expect(same.status).toBe(202);
    expect(other.status).toBe(202);
    expect(Object.keys(same.body).sort()).toEqual(Object.keys(other.body).sort());
  });
});
