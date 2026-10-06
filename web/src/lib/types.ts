// Shapes of the device API (SPEC §5). This file is the client's view of the contract.

export type TypeWhat = 'username' | 'password' | 'both' | 'totp';
export type ResultCode = 'typed' | 'cancelled' | 'expired' | 'no_usb' | 'unsupported_char' | 'failed';

export interface Pending {
  kind: 'type';
  id: number;
  title: string;
  what: TypeWhat | 'test';
  submit: boolean;
  expiresIn: number;
}

export interface TypeResult {
  ok: boolean;
  code: ResultCode;
  at: number; // ms ago
  title?: string;
  what?: TypeWhat | 'test';
}

export type PresenceOp = 'setup' | 'wifi' | 'restore' | 'factory_reset';

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
  timeValid: boolean;
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
}

export type EntryInput = Omit<Entry, 'id' | 'created' | 'updated' | 'lastUsed'>;

export interface Settings {
  deviceName: string;
  wifiSsid: string;
  autoLockMin: number;
  keyDelayMs: number;
  bothSeparator: 'tab' | 'enter';
  submitAfterBoth: boolean;
  ledBrightness: number;
}

export interface Totp {
  code: string;
  period: number;
  remaining: number;
}
