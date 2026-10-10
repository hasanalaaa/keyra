// Starts web/mock/server.mjs (the device's API double) and acts as the human with the web app:
// unlock, create a token (press, then ask again), press Keyra's button.
import { spawn, type ChildProcess } from 'node:child_process';
import { createServer } from 'node:net';
import { fileURLToPath } from 'node:url';

const WEB = fileURLToPath(new URL('../../web/', import.meta.url));

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

export interface Mock {
  base: string;
  stop(): void;
  session(method: string, path: string, body?: unknown): Promise<{ status: number; body: Record<string, unknown> }>;
  unlock(): Promise<void>;
  lock(): Promise<void>;
  press(kind?: 'short' | 'long'): Promise<string>;
  createToken(kind: string, scope?: 'all' | number[]): Promise<string>;
}

export async function startMock(): Promise<Mock> {
  const port = await freePort();
  const p: ChildProcess = spawn(process.execPath, ['mock/server.mjs'], { cwd: WEB, env: { ...process.env, PORT: String(port) }, stdio: ['ignore', 'pipe', 'inherit'] });
  await new Promise<void>((resolve, reject) => {
    p.stdout!.on('data', (d) => String(d).includes('Keyra mock on') && resolve());
    p.on('exit', (code) => reject(new Error(`mock exited (${code})`)));
  });
  const base = `http://localhost:${port}`;
  let cookie = '';
  let csrf = '';

  const session: Mock['session'] = async (method, path, body) => {
    const headers: Record<string, string> = { 'Content-Type': 'application/json' };
    if (cookie) headers.Cookie = cookie;
    if (csrf) headers['X-Keyra-Csrf'] = csrf;
    const r = await fetch(base + path, { method, headers, body: body === undefined ? undefined : JSON.stringify(body) });
    const set = r.headers.get('set-cookie');
    const ks = set && /ks=[^;]+/.exec(set)?.[0];
    if (ks) cookie = ks;
    const text = await r.text();
    return { status: r.status, body: text ? JSON.parse(text) : {} };
  };
  const press: Mock['press'] = async (kind = 'short') => {
    const r = await fetch(`${base}/__mock/button`, { method: 'POST', body: JSON.stringify({ press: kind }) });
    return ((await r.json()) as { result: string }).result;
  };

  return {
    base,
    stop: () => p.kill(),
    session,
    press,
    async unlock() {
      const r = await session('POST', '/api/unlock', { passphrase: 'keyra demo vault' });
      if (r.status !== 200) throw new Error(`unlock: ${r.status}`);
      csrf = String(r.body.csrf);
    },
    async lock() {
      await session('POST', '/api/lock', {});
      cookie = '';
      csrf = '';
    },
    async createToken(kind, scope = 'all') {
      const body = { name: `test-${kind}`, kind, scope };
      const first = await session('POST', '/api/tokens', body);
      if (first.status !== 202) throw new Error(`token: ${first.status} ${JSON.stringify(first.body)}`);
      await press();
      for (let i = 0; i < 30; i++) {
        const again = await session('POST', '/api/tokens', body);
        if (again.status === 201) return String(again.body.token);
        await new Promise((r) => setTimeout(r, 100));
      }
      throw new Error('token was not created');
    },
  };
}

export const sleep = (ms: number) => new Promise((r) => setTimeout(r, ms));
