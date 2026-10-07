// App-wide state: device state polling, session/lock tracking, entries cache, toasts, preferences.
import { useEffect, useState } from 'preact/hooks';
import { api, hasCsrf, setLockedHandler, forgetSession, type Awaiting } from './api';
import { detectLang, setLang, type Lang, type LangPref } from './i18n';
import type { BleInfo, DeviceState, EntrySummary } from './types';

export type ThemePref = 'auto' | 'light' | 'dark';
export type LockReason = 'idle' | 'button' | 'manual' | 'session' | 'unplugged' | null;

export interface Toast {
  id: number;
  text: string;
  kind: 'info' | 'ok' | 'error';
  action?: { label: string; run: () => void };
}

export interface AppState {
  device: DeviceState | null;
  /** When the request that produced `device` was sent (so actions can ignore stale polls). */
  deviceAt: number;
  online: boolean;
  authed: boolean;
  lockReason: LockReason;
  entries: EntrySummary[] | null;
  /** GET /api/ble, refreshed when the Bluetooth link changes (names the device Ready types into). */
  ble: BleInfo | null;
  /** settings.lastBackupAt (unix s, 0 = never; null = not loaded) for the backup reminder (SPEC §12.5). */
  backupAt: number | null;
  langPref: LangPref;
  lang: Lang;
  themePref: ThemePref;
  toast: Toast | null;
}

const LANG_KEY = 'keyra.lang';
const THEME_KEY = 'keyra.theme';

function readPref<T extends string>(key: string, allowed: readonly T[], fallback: T): T {
  try {
    const v = localStorage.getItem(key);
    return v && (allowed as readonly string[]).includes(v) ? (v as T) : fallback;
  } catch {
    return fallback;
  }
}

function writePref(key: string, value: string): void {
  try {
    localStorage.setItem(key, value);
  } catch {
    // Preference just won't survive a reload.
  }
}

const langPref = readPref<LangPref>(LANG_KEY, ['auto', 'ar', 'en'], 'auto');

let state: AppState = {
  device: null,
  deviceAt: 0,
  online: true,
  authed: false,
  lockReason: null,
  entries: null,
  ble: null,
  backupAt: null,
  langPref,
  lang: detectLang(langPref, navigator.language || 'en'),
  themePref: readPref<ThemePref>(THEME_KEY, ['auto', 'light', 'dark'], 'auto'),
  toast: null,
};

const listeners = new Set<() => void>();

export const getState = (): AppState => state;

export function setState(patch: Partial<AppState>): void {
  state = { ...state, ...patch };
  listeners.forEach((l) => l());
}

export function useApp(): AppState {
  const [, force] = useState(0);
  useEffect(() => {
    const l = () => force((n) => n + 1);
    listeners.add(l);
    return () => void listeners.delete(l);
  }, []);
  return state;
}

// ---------- preferences ----------

function applyDocumentPrefs(): void {
  const html = document.documentElement;
  setLang(state.lang);
  html.lang = state.lang;
  html.dir = state.lang === 'ar' ? 'rtl' : 'ltr';
  html.dataset.theme = state.themePref;
}

export function setLangPref(p: LangPref): void {
  writePref(LANG_KEY, p);
  setState({ langPref: p, lang: detectLang(p, navigator.language || 'en') });
  applyDocumentPrefs();
}

export function setThemePref(p: ThemePref): void {
  writePref(THEME_KEY, p);
  setState({ themePref: p });
  applyDocumentPrefs();
}

// ---------- toasts ----------

let toastSeq = 0;
let toastTimer: ReturnType<typeof setTimeout> | undefined;

export function toast(text: string, kind: Toast['kind'] = 'info', action?: Toast['action']): void {
  clearTimeout(toastTimer);
  setState({ toast: { id: ++toastSeq, text, kind, action } });
  toastTimer = setTimeout(dismissToast, kind === 'error' || action ? 5000 : 2400);
}

export function dismissToast(): void {
  clearTimeout(toastTimer);
  setState({ toast: null });
}

// ---------- activity (for telling an idle auto-lock from a button lock) ----------

let lastActivity = Date.now();
let manualLock = false;
const markActivity = () => {
  lastActivity = Date.now();
};

function lockReasonNow(d: DeviceState | null): LockReason {
  if (manualLock) return 'manual';
  if (d && d.unlocked) return 'session';
  // The USB computer went away with the vault open: "lock on unplug" (SPEC §12.4).
  if (state.device?.host.usb && d && !d.host.usb) return 'unplugged';
  const idleFor = Date.now() - lastActivity;
  const autoLockMs = (d?.autoLockMin ?? 15) * 60000;
  return idleFor >= autoLockMs - 5000 ? 'idle' : 'button';
}

function becameLocked(d: DeviceState | null): void {
  if (!state.authed) return;
  forgetSession();
  setState({ authed: false, lockReason: lockReasonNow(d), entries: null, ble: null, backupAt: null });
  manualLock = false;
}

// ---------- polling ----------

let fastHolds = 0;
let inFlight = false;
let rerun = false;
let timer: ReturnType<typeof setTimeout> | undefined;

/** Keep polling at 1 s while the returned release function has not been called (Ready, reconnect). */
export function holdFastPolling(): () => void {
  fastHolds++;
  pollNow();
  let released = false;
  return () => {
    if (!released) {
      released = true;
      fastHolds--;
    }
  };
}

function nextDelay(): number {
  const d = state.device;
  if (fastHolds > 0 || !state.online || (d && (d.pending || d.presence.awaiting))) return 1000;
  return 5000;
}

let stateFailures = 0;

export async function pollNow(): Promise<void> {
  if (inFlight) {
    rerun = true;
    return;
  }
  clearTimeout(timer);
  inFlight = true;
  try {
    const sent = Date.now();
    const d = await api.state();
    const authed = d.unlocked && d.session && hasCsrf();
    if (state.authed && !authed) becameLocked(d);
    const linkChanged = state.device?.host.ble !== d.host.ble;
    setState({ device: d, deviceAt: sent, online: true });
    if (state.authed && (linkChanged || (d.host.ble && !state.ble))) void loadBle();
    stateFailures = 0;
  } catch {
    // One slow or dropped poll (e.g. right after a Wi-Fi switch) is not an outage.
    if (++stateFailures >= 2) setState({ online: false });
  } finally {
    inFlight = false;
  }
  if (rerun) {
    rerun = false;
    void pollNow();
    return;
  }
  if (!document.hidden) timer = setTimeout(() => void pollNow(), nextDelay());
}

export function startApp(): void {
  applyDocumentPrefs();
  setLockedHandler(() => becameLocked(state.device));
  for (const ev of ['pointerdown', 'keydown']) window.addEventListener(ev, markActivity, { passive: true });
  document.addEventListener('visibilitychange', () => {
    clearTimeout(timer);
    if (!document.hidden) {
      void pollNow();
      if (state.authed) void loadEntries();
    }
  });
  matchMedia('(prefers-color-scheme: dark)').addEventListener?.('change', () => applyDocumentPrefs());
  void pollNow().then(() => {
    const d = state.device;
    if (d && d.unlocked && d.session && hasCsrf()) {
      setState({ authed: true });
      void loadEntries();
      void loadBle();
      void loadBackupAt();
    }
  });
}

// ---------- session ----------

/** Resolves to Awaiting when the device wants this browser trusted first (the caller shows the button wait and retries). */
export async function unlock(passphrase: string): Promise<Awaiting | null> {
  return opened(await api.unlock(passphrase));
}

/** Forgotten passphrase (SPEC §12.2): the recovery key (hex) sets `next` and unlocks. */
export async function unlockRecovery(keyHex: string, next: string): Promise<Awaiting | null> {
  return opened(await api.unlockRecovery(keyHex, next));
}

async function opened(awaiting: Awaiting | null): Promise<Awaiting | null> {
  if (awaiting) return awaiting;
  markActivity();
  setState({ authed: true, lockReason: null });
  await Promise.all([pollNow(), loadEntries(), loadBle(), loadBackupAt()]);
  return null;
}

export async function loadBackupAt(): Promise<void> {
  try {
    setState({ backupAt: (await api.settings()).lastBackupAt ?? 0 });
  } catch {
    // Locked/offline: no reminder.
  }
}

export async function lockNow(): Promise<void> {
  manualLock = true;
  try {
    await api.lock();
  } catch {
    // Already locked or unreachable: either way the session is gone on our side.
  }
  becameLocked(state.device);
  void pollNow();
}

export async function loadBle(): Promise<void> {
  try {
    setState({ ble: await api.ble() });
  } catch {
    // Locked/offline are handled elsewhere; Ready simply names no device.
  }
}

export async function loadEntries(): Promise<void> {
  try {
    setState({ entries: await api.entries() });
  } catch {
    // 401 is handled by the locked handler; network errors surface via the connection banner.
  }
}
