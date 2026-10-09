// Client for Keyra's token API (SPEC §17, §9.4). Runs in the service worker only: it holds the
// access token, and no answer it can get ever contains a stored password or username.

export type ErrorCode =
  | 'not_connected' // no address/token yet
  | 'unreachable' // network error: Keyra off, another network, wrong address
  | 'timeout'
  | 'not_keyra' // something answered, but not Keyra's API
  | 'locked' // 401: the vault is locked
  | 'invalid_token' // 401: unknown or revoked token
  | 'rate_limited' // 429
  | 'host_mismatch' // 409: the login is for another site
  | 'busy' // 409: another request waits for the button
  | 'not_found' // 404: the login is gone or outside the token's scope
  | 'forbidden' // 403: a token that is not of kind "extension"
  | 'invalid' // 400
  | 'error';

export class KeyraError extends Error {
  constructor(
    readonly code: ErrorCode,
    message = code,
    readonly status = 0,
    readonly retryAfterMs = 0,
  ) {
    super(message);
  }
}

export interface Login {
  id: number;
  title: string;
  host: string;
  sameUser?: boolean;
}

export type What = 'username' | 'password' | 'both';

export interface Status {
  state: 'none' | 'armed' | 'waiting' | 'typed' | 'saved' | 'cancelled' | 'expired' | 'failed';
  request?: 'type' | 'save';
  id?: number;
  title?: string;
  what?: string;
  code?: string;
  expiresIn?: number;
}

export interface DeviceState {
  initialized: boolean;
  unlocked: boolean;
  name: string;
}

export interface GenOptions {
  length: number;
  lower: boolean;
  upper: boolean;
  digits: boolean;
  symbols: boolean;
  minDigits: number;
  minSymbols: number;
  avoidAmbiguous: boolean;
}

export const GEN_DEFAULTS: GenOptions = { length: 20, lower: true, upper: true, digits: true, symbols: true, minDigits: 1, minSymbols: 1, avoidAmbiguous: true };

export interface SaveRequest {
  title: string;
  url: string;
  username?: string;
  password: string;
  replace?: number;
}

export interface Client {
  state(): Promise<DeviceState>;
  entries(): Promise<Login[]>;
  match(host: string, username?: string): Promise<Login[]>;
  type(req: { id: number; what: What; host: string; anyHost?: boolean }): Promise<{ expiresIn: number }>;
  status(): Promise<Status>;
  cancel(): Promise<void>;
  save(req: SaveRequest): Promise<{ expiresIn: number; mode: 'create' | 'update' }>;
  generate(opts: GenOptions): Promise<{ password: string; entropyBits: number }>;
}

/** `keyra.local`, `http://192.168.1.20/`, … → `http://keyra.local` (an origin), or null. */
export function normalizeAddress(input: string): string | null {
  let s = input.trim();
  if (!s) return null;
  if (!/^[a-z][a-z0-9+.-]*:\/\//i.test(s)) s = `http://${s}`;
  let u: URL;
  try {
    u = new URL(s);
  } catch {
    return null;
  }
  if ((u.protocol !== 'http:' && u.protocol !== 'https:') || !u.hostname || u.username || u.password) return null;
  return u.origin;
}

export const TOKEN_RE = /^keyra_[a-z2-7]{32}$/;

const CODES = new Set<ErrorCode>(['locked', 'invalid_token', 'rate_limited', 'host_mismatch', 'busy', 'not_found', 'forbidden', 'invalid']);

export function createClient(opts: { address: string; token?: string; fetch?: typeof fetch; timeoutMs?: number }): Client {
  const doFetch = opts.fetch ?? fetch.bind(globalThis);
  const timeoutMs = opts.timeoutMs ?? 8000;

  async function call<T>(method: 'GET' | 'POST', path: string, body?: unknown, auth = true): Promise<T> {
    if (auth && !opts.token) throw new KeyraError('not_connected');
    const headers: Record<string, string> = {};
    if (auth) headers.Authorization = `Bearer ${opts.token}`;
    if (body !== undefined) headers['Content-Type'] = 'application/json';
    let res: Response;
    try {
      res = await doFetch(opts.address + path, {
        method,
        headers,
        body: body === undefined ? undefined : JSON.stringify(body),
        credentials: 'omit',
        cache: 'no-store',
        signal: AbortSignal.timeout(timeoutMs),
      });
    } catch (e) {
      const name = (e as Error)?.name;
      throw new KeyraError(name === 'TimeoutError' || name === 'AbortError' ? 'timeout' : 'unreachable', String((e as Error)?.message ?? e));
    }
    if (res.status === 204) return undefined as T;
    let data: Record<string, unknown> = {};
    try {
      data = (await res.json()) as Record<string, unknown>;
    } catch {
      throw new KeyraError('not_keyra', `HTTP ${res.status} without JSON`, res.status);
    }
    if (res.ok) return data as T;
    const raw = String(data.error ?? '');
    const code: ErrorCode = CODES.has(raw as ErrorCode) ? (raw as ErrorCode) : 'error';
    const retry = Number(data.retryAfterMs) || Number(res.headers.get('Retry-After')) * 1000 || 0;
    throw new KeyraError(code, String(data.message ?? raw ?? res.status), res.status, retry);
  }

  return {
    async state() {
      const s = await call<{ device?: { name?: string }; initialized?: boolean; unlocked?: boolean }>('GET', '/api/state', undefined, false);
      if (!s || typeof s.initialized !== 'boolean' || !s.device) throw new KeyraError('not_keyra', 'Not a Keyra');
      return { initialized: s.initialized, unlocked: !!s.unlocked, name: String(s.device.name ?? 'Keyra') };
    },
    entries: async () => (await call<{ entries: Login[] }>('GET', '/api/agent/entries')).entries,
    match: async (host, username) => (await call<{ entries: Login[] }>('POST', '/api/agent/match', username === undefined ? { host } : { host, username })).entries,
    type: async (req) => {
      const r = await call<{ expiresIn: number }>('POST', '/api/agent/type', req.anyHost ? req : { id: req.id, what: req.what, host: req.host });
      return { expiresIn: r.expiresIn };
    },
    status: () => call<Status>('GET', '/api/agent/status'),
    cancel: async () => {
      await call<void>('POST', '/api/agent/cancel');
    },
    save: async (req) => {
      const body = req.replace ? { title: req.title, url: req.url, username: req.username, password: req.password, replace: req.replace } : req;
      const r = await call<{ expiresIn: number; mode?: 'create' | 'update' }>('POST', '/api/agent/save', body);
      return { expiresIn: r.expiresIn, mode: r.mode ?? (req.replace ? 'update' : 'create') };
    },
    generate: (o) => call<{ password: string; entropyBits: number }>('POST', '/api/agent/generate', o),
  };
}
