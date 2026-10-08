import type { BleInfo, DeviceState, Keyboard, Entry, HostOs, EntryInput, ActivityEvent, EntrySummary, Health, Network, Passkey, UpdateCheck, RecoveryInfo, Settings, Totp, TrustedBrowser, TypeTextRequest, TypeWhat, Pending, PresenceOp } from './types';
import { generateRequest, type GenSettings } from './generator';

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
const OPEN = new Set(['/unlock', '/unlock/recovery', '/setup', '/factory-reset', '/state']);

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

/**
 * POST /api/update with the firmware file as the body (SPEC §14), reporting upload progress.
 * XHR rather than fetch: fetch cannot report upload progress.
 */
function uploadFirmware(file: Blob, onProgress: (sent: number, total: number) => void): Promise<{ version: string }> {
  return new Promise((resolve, reject) => {
    const xhr = new XMLHttpRequest();
    xhr.open('POST', '/api/update');
    xhr.setRequestHeader('Content-Type', 'application/octet-stream');
    xhr.setRequestHeader('X-Keyra-Time', String(Date.now()));
    if (csrf) xhr.setRequestHeader('X-Keyra-CSRF', csrf);
    xhr.timeout = 10 * 60 * 1000;
    xhr.upload.onprogress = (e) => onProgress(e.loaded, e.lengthComputable ? e.total : file.size);
    xhr.onerror = xhr.ontimeout = () => reject(new ApiError(0, 'network', 'Network error'));
    xhr.onload = () => {
      let j: { error?: string; message?: string; version?: string } = {};
      try {
        j = JSON.parse(xhr.responseText || '{}');
      } catch {
        // Not JSON: the status decides.
      }
      if (xhr.status === 200 && j.version) return resolve({ version: j.version });
      const code = j.error ?? 'http_' + xhr.status;
      if (xhr.status === 401 && code === 'locked') {
        setCsrf(null);
        onLocked();
      }
      reject(new ApiError(xhr.status, code, j.message ?? xhr.statusText));
    };
    xhr.send(file);
  });
}

interface Session {
  csrf: string;
  failedAttempts?: number; // absent on firmware before SPEC §15
}

export interface Unlocked {
  failedAttempts: number;
}

export interface Awaiting {
  awaiting: 'button';
  expiresIn: number;
  op?: PresenceOp;
  /** Secret that lets this browser (only) withdraw the op: POST /presence/cancel. */
  cancel?: string;
}

export const api = {
  // Generous: a phone that just switched networks may need a few seconds to
  // resolve keyra.local again before the request can even start.
  state: () => json<DeviceState>('GET', '/state', undefined, 12000),
  setup: (passphrase: string, wifiPassword: string) => json<Awaiting>('POST', '/setup', { passphrase, wifiPassword }),
  /**
   * Unlocked → `{ failedAttempts }` (wrong guesses since the last unlock, SPEC §15);
   * Awaiting = this browser must first be trusted with the button (home network, SPEC §8.2).
   */
  async unlock(passphrase: string): Promise<Awaiting | Unlocked> {
    const r = await json<Session | Awaiting>('POST', '/unlock', { passphrase }, 30000);
    if (isAwaiting(r)) return r;
    setCsrf(r.csrf);
    return { failedAttempts: r.failedAttempts ?? 0 };
  },
  /** Forgotten passphrase (SPEC §12.2): the recovery key (40 hex) sets `next` and unlocks. */
  async unlockRecovery(recoveryKey: string, next: string): Promise<Awaiting | Unlocked> {
    const r = await json<Session | Awaiting>('POST', '/unlock/recovery', { recoveryKey, next }, 30000);
    if (isAwaiting(r)) return r;
    setCsrf(r.csrf);
    return { failedAttempts: r.failedAttempts ?? 0 };
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
  /** 200 → the entry with its secrets; 202 → press Keyra's button, then call again (SPEC §12.3). */
  reveal: (id: number) => json<Entry | Awaiting>('POST', `/entries/${id}/reveal`),
  recovery: () => json<RecoveryInfo>('GET', '/recovery'),
  /** 200 → the new key, shown once; 202 → press first, then call again. */
  createRecovery: () => json<{ recoveryKey: string; created: number } | Awaiting>('POST', '/recovery'),
  removeRecovery: () => json<void | Awaiting>('DELETE', '/recovery'),
  create: (e: EntryInput) => json<{ id: number }>('POST', '/entries', e),
  update: (id: number, e: Partial<EntryInput>) => json<{ id: number }>('PUT', `/entries/${id}`, e),
  remove: (id: number) => json<void>('DELETE', `/entries/${id}`),
  importBatch: (entries: Partial<EntryInput>[]) =>
    json<{ added: number; skipped: number }>('POST', '/entries/import', { entries }, 30000),
  totp: (id: number) => json<Totp>('GET', `/entries/${id}/totp`),
  /** `target`: "usb" or a paired device's address; omitted = the device's own choice (SPEC §8.1). */
  /** `switchLang`: the macOS/iOS host is in another input language; Keyra switches there and back (SPEC §10.5). */
  type: (id: number, what: TypeWhat, target?: string, switchLang = false) =>
    json<{ pending: Pending }>('POST', '/type', { id, what, target, switchLang }),
  typeTest: (target?: string, switchLang = false) => json<{ pending: Pending }>('POST', '/type', { test: true, target, switchLang }),
  /** Layout Doctor (SPEC §10.3): types fixed keys; what appears names the computer's layout. */
  typeProbe: (target?: string, switchLang = false) => json<{ pending: Pending }>('POST', '/type', { probe: true, target, switchLang }),
  /** Free text (SPEC §9.2). */
  typeText: (r: TypeTextRequest & { target?: string; switchLang?: boolean }) => json<{ pending: Pending }>('POST', '/type', r),
  /** On the device, from its hardware RNG (SPEC §9.1). */
  /** `layouts`: for the layout-safe option, the computers' layouts (SPEC §10.2). */
  generate: (s: GenSettings, layouts: string[] = []) => json<{ password: string; entropyBits: number }>('POST', '/generate', generateRequest(s, layouts)),
  cancelType: () => json<void>('POST', '/type/cancel'),
  /** Withdraws a waiting "press Keyra's button" op, so a later press does not run it. */
  cancelPresence: (op: PresenceOp, cancel: string) => json<void>('POST', '/presence/cancel', { op, cancel }),
  settings: () => json<Settings>('GET', '/settings'),
  keyboard: () => json<Keyboard>('GET', '/keyboard'),
  /** 200 → Settings; 202 → presence required (Wi-Fi changes). */
  async putSettings(s: Partial<Settings> & { wifiPassword?: string }): Promise<Settings | Awaiting> {
    return json<Settings | Awaiting>('PUT', '/settings', s);
  },
  passphrase: (current: string, next: string) => json<void>('POST', '/passphrase', { current, next }, 30000),
  /** Blob → the file; Awaiting → press Keyra's button, then call again (SPEC §12.3). */
  async backup(passphrase: string): Promise<Blob | Awaiting> {
    const res = await request('POST', '/backup', { passphrase }, 60000);
    if (res.status === 202) return (await res.json()) as Awaiting;
    return res.blob();
  },
  restore: (passphrase: string, backup: unknown, mode: 'merge' | 'replace') =>
    json<{ added: number; updated: number } | Awaiting>('POST', '/restore', { passphrase, backup, mode }, 60000),
  factoryReset: () => json<Awaiting>('POST', '/factory-reset'),
  ble: () => json<BleInfo>('GET', '/ble'),
  blePair: () => json<Awaiting>('POST', '/ble/pair'),
  bleForget: (addr: string) => json<void>('DELETE', `/ble/bonds/${addr}`),
  bleSetOs: (addr: string, os: HostOs) => json<void>('PUT', `/ble/bonds/${addr}`, { os }),
  /** Blocks a few seconds on the device while the radio scans. */
  wifiScan: async () => (await json<{ networks: Network[] }>('GET', '/wifi/scan', undefined, 45000)).networks,
  putHomeWifi: (b: { enabled: boolean; ssid?: string; password?: string }) => json<Awaiting>('PUT', '/wifi/home', b),
  trusted: async () => (await json<{ browsers: TrustedBrowser[] }>('GET', '/trusted')).browsers,
  revokeTrusted: (id: number) => json<void>('DELETE', `/trusted/${id}`),
  health: () => json<Health>('GET', '/health'),
  updateUpload: uploadFirmware,
  // Keyra asks GitHub itself: a TLS handshake and an answer from the internet.
  updateCheck: () => json<UpdateCheck>('POST', '/update/check', undefined, 45000),
  updateDownload: () => json<{ downloading: boolean }>('POST', '/update/download'),
  updateApply: () => json<Awaiting & { version: string }>('POST', '/update/apply'),
  healthRotate: (on: boolean) => json<Health>('POST', '/health/rotate', { on }),
  activity: () => json<{ events: ActivityEvent[]; max: number }>('GET', '/activity'),
  passkeys: async () => await json<{ passkeys: Passkey[]; max: number }>('GET', '/fido'),
  deletePasskey: (id: number) => json<void>('DELETE', `/fido/${id}`),
};

export const isAwaiting = (r: unknown): r is Awaiting =>
  typeof r === 'object' && r !== null && (r as Awaiting).awaiting === 'button';
