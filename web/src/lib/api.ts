import type { DeviceState, Entry, EntryInput, EntrySummary, Network, Settings, Totp, TrustedBrowser, TypeWhat, Pending, PresenceOp } from './types';

export class ApiError extends Error {
  constructor(
    readonly status: number, // 0 = network failure / timeout
    readonly code: string,
    message: string,
    readonly retryAfterMs = 0,
  ) {
    super(message);
  }
}

const CSRF_KEY = 'keyra.csrf';
let csrf: string | null = readCsrf();
let onLocked: () => void = () => {};

function readCsrf(): string | null {
  try {
    return sessionStorage.getItem(CSRF_KEY);
  } catch {
    return null;
  }
}

function setCsrf(v: string | null): void {
  csrf = v;
  try {
    if (v) sessionStorage.setItem(CSRF_KEY, v);
    else sessionStorage.removeItem(CSRF_KEY);
  } catch {
    // Storage blocked (private mode): the token simply lives in memory for this page.
  }
}

export const hasCsrf = (): boolean => csrf !== null;
export const forgetSession = (): void => setCsrf(null);
/** Called on any 401 `locked` from a protected route so the app can show Unlock in place. */
export const setLockedHandler = (fn: () => void): void => {
  onLocked = fn;
};

// Routes that must work without a session (SPEC §5): no CSRF header, and a 401 is not "locked".
const OPEN = new Set(['/unlock', '/setup', '/factory-reset', '/state']);

async function request(method: string, path: string, body?: unknown, timeoutMs = 15000): Promise<Response> {
  const headers: Record<string, string> = { 'X-Keyra-Time': String(Date.now()) };
  if (body !== undefined) headers['Content-Type'] = 'application/json; charset=utf-8';
  if (method !== 'GET' && !OPEN.has(path) && csrf) headers['X-Keyra-CSRF'] = csrf;
  const ctl = new AbortController();
  const timer = setTimeout(() => ctl.abort(), timeoutMs);
  let res: Response;
  try {
    res = await fetch(`/api${path}`, {
      method,
      headers,
      body: body === undefined ? undefined : JSON.stringify(body),
      credentials: 'same-origin',
      cache: 'no-store',
      signal: ctl.signal,
    });
  } catch {
    throw new ApiError(0, 'network', 'Network error');
  } finally {
    clearTimeout(timer);
  }
  if (res.ok) return res;
  let code = 'http_' + res.status;
  let message = res.statusText;
  let retryAfterMs = 0;
  try {
    const j = (await res.json()) as { error?: string; message?: string; retryAfterMs?: number };
    code = j.error ?? code;
    message = j.message ?? message;
    retryAfterMs = j.retryAfterMs ?? 0;
  } catch {
    // Non-JSON error body: keep the HTTP status as the code.
  }
  if (res.status === 401 && code === 'locked' && !OPEN.has(path)) {
    setCsrf(null);
    onLocked();
  }
  throw new ApiError(res.status, code, message, retryAfterMs);
}

async function json<T>(method: string, path: string, body?: unknown, timeoutMs?: number): Promise<T> {
  const res = await request(method, path, body, timeoutMs);
  if (res.status === 204) return undefined as T;
  return (await res.json()) as T;
}

export interface Awaiting {
  awaiting: 'button';
  expiresIn: number;
  op?: PresenceOp;
}

export const api = {
  // Generous: a phone that just switched networks may need a few seconds to
  // resolve keyra.local again before the request can even start.
  state: () => json<DeviceState>('GET', '/state', undefined, 12000),
  setup: (passphrase: string, wifiPassword: string) => json<Awaiting>('POST', '/setup', { passphrase, wifiPassword }),
  /** null = unlocked; Awaiting = this browser must first be trusted with the button (home network, SPEC §8.2). */
  async unlock(passphrase: string): Promise<Awaiting | null> {
    const r = await json<{ csrf: string } | Awaiting>('POST', '/unlock', { passphrase }, 30000);
    if (isAwaiting(r)) return r;
    setCsrf(r.csrf);
    return null;
  },
  async lock(): Promise<void> {
    try {
      await json<void>('POST', '/lock');
    } finally {
      setCsrf(null);
    }
  },
  entries: async () => (await json<{ entries: EntrySummary[] }>('GET', '/entries')).entries,
  entry: (id: number) => json<Entry>('GET', `/entries/${id}`),
  create: (e: EntryInput) => json<{ id: number }>('POST', '/entries', e),
  update: (id: number, e: Partial<EntryInput>) => json<{ id: number }>('PUT', `/entries/${id}`, e),
  remove: (id: number) => json<void>('DELETE', `/entries/${id}`),
  importBatch: (entries: Partial<EntryInput>[]) =>
    json<{ added: number; skipped: number }>('POST', '/entries/import', { entries }, 30000),
  totp: (id: number) => json<Totp>('GET', `/entries/${id}/totp`),
  type: (id: number, what: TypeWhat) => json<{ pending: Pending }>('POST', '/type', { id, what }),
  typeTest: () => json<{ pending: Pending }>('POST', '/type', { test: true }),
  cancelType: () => json<void>('POST', '/type/cancel'),
  settings: () => json<Settings>('GET', '/settings'),
  /** 200 → Settings; 202 → presence required (Wi-Fi changes). */
  async putSettings(s: Partial<Settings> & { wifiPassword?: string }): Promise<Settings | Awaiting> {
    return json<Settings | Awaiting>('PUT', '/settings', s);
  },
  passphrase: (current: string, next: string) => json<void>('POST', '/passphrase', { current, next }, 30000),
  async backup(passphrase: string): Promise<Blob> {
    const res = await request('POST', '/backup', { passphrase }, 60000);
    return res.blob();
  },
  restore: (passphrase: string, backup: unknown, mode: 'merge' | 'replace') =>
    json<{ added: number; updated: number } | Awaiting>('POST', '/restore', { passphrase, backup, mode }, 60000),
  factoryReset: () => json<Awaiting>('POST', '/factory-reset'),
  /** Blocks a few seconds on the device while the radio scans. */
  wifiScan: async () => (await json<{ networks: Network[] }>('GET', '/wifi/scan', undefined, 45000)).networks,
  putHomeWifi: (b: { enabled: boolean; ssid?: string; password?: string }) => json<Awaiting>('PUT', '/wifi/home', b),
  trusted: async () => (await json<{ browsers: TrustedBrowser[] }>('GET', '/trusted')).browsers,
  revokeTrusted: (id: number) => json<void>('DELETE', `/trusted/${id}`),
};

export const isAwaiting = (r: unknown): r is Awaiting =>
  typeof r === 'object' && r !== null && (r as Awaiting).awaiting === 'button';
