// Shapes of the device API (SPEC §5). This file is the client's view of the contract.

export type TypeWhat = 'username' | 'password' | 'both' | 'totp';
export type ResultCode = 'typed' | 'cancelled' | 'expired' | 'no_usb' | 'unsupported_char' | 'failed';

export interface Pending {
  kind: 'type';
  id: number; // 0 for the type test and free text
  title: string | null; // null for free text (SPEC §9.2)
  what: TypeWhat | 'test' | 'text';
  submit: boolean;
  expiresIn: number;
}

export interface TypeResult {
  ok: boolean;
  code: ResultCode;
  at: number; // ms ago
  title?: string | null;
  what?: TypeWhat | 'test' | 'text';
}

export type PresenceOp = 'setup' | 'wifi' | 'restore' | 'factory_reset' | 'home_wifi' | 'trust_browser';

export interface PresenceResult {
  op: PresenceOp;
  ok: boolean;
  code: 'done' | 'failed' | 'expired' | 'cancelled';
  at: number; // ms ago
}

export interface Presence {
  awaiting: boolean;
  op: PresenceOp | null;
  expiresIn: number;
  result: PresenceResult | null;
}

export interface DeviceState {
  device: { name: string; version: string; model: string; mac: string };
  initialized: boolean;
  unlocked: boolean;
  session: boolean;
  autoLockMin: number;
  host: { usb: boolean; capsLock: boolean };
  pending: Pending | null;
  last: TypeResult | null;
  presence: Presence;
  net?: NetState; // absent on firmware before v1.1
  timeValid: boolean;
}

/** SPEC §8.2 state.net. `via`: how this very request reached Keyra. */
export interface NetState {
  ap: { on: boolean; ssid: string; clients: number };
  home: { enabled: boolean; connected: boolean; ssid: string; ip: string | null; rssi: number | null } | null;
  via: 'ap' | 'home';
}

export interface Network {
  ssid: string;
  rssi: number;
  secure: boolean;
  channel: number;
}

export interface TrustedBrowser {
  id: number;
  name: string;
  created: number; // unix seconds, 0 = unknown
  lastSeen: number;
  current: boolean;
}

export interface EntrySummary {
  id: number;
  title: string;
  url: string;
  username: string;
  favorite: boolean;
  hasPassword: boolean;
  hasTotp: boolean;
  updated: number;
  lastUsed: number;
}

export interface Entry {
  id: number;
  title: string;
  url: string;
  username: string;
  password: string;
  totp: string;
  notes: string;
  favorite: boolean;
  created: number;
  updated: number;
  lastUsed: number;
  history: OldPassword[]; // newest first, at most 10 (SPEC §9.3)
}

export interface OldPassword {
  password: string;
  changedAt: number; // unix seconds it was replaced, 0 = unknown
}

export type EntryInput = Omit<Entry, 'id' | 'created' | 'updated' | 'lastUsed' | 'history'>;

/** POST /api/type with free text (SPEC §9.2). */
export interface TypeTextRequest {
  text: string;
  repeat: 1 | 2;
  separator: 'tab' | 'enter';
}

export interface Settings {
  deviceName: string;
  wifiSsid: string;
  autoLockMin: number;
  keyDelayMs: number;
  bothSeparator: 'tab' | 'enter';
  submitAfterBoth: boolean;
  ledBrightness: number;
  homeWifi: { enabled: boolean; ssid: string };
  apMode: 'always' | 'fallback';
}

export interface Totp {
  code: string;
  period: number;
  remaining: number;
}
