// Shapes of the device API (SPEC §5). This file is the client's view of the contract.

export type TypeWhat = 'username' | 'password' | 'both' | 'totp';
export type ResultCode = 'typed' | 'cancelled' | 'expired' | 'no_usb' | 'no_host' | 'unsupported_char' | 'failed';

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

export type PresenceOp = 'setup' | 'wifi' | 'restore' | 'factory_reset' | 'ble_pair';

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
  /** `output`: where a typed action would go right now; null = nothing connected on the selected output. */
  host: { usb: boolean; ble: boolean; capsLock: boolean; output: 'usb' | 'ble' | null };
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
  bleEnabled: boolean;
  output: Output;
}

/** Where typing goes: `auto` = USB when plugged in, else the connected Bluetooth device. */
export type Output = 'auto' | 'usb' | 'ble';

export interface BlePeer {
  addr: string; // "A4:C1:38:0B:7F:3A"
  name: string; // the device's own name; '' until Keyra has read it
}

export interface BleBond extends BlePeer {
  lastSeen: number; // unix seconds, 0 = unknown
}

export interface BleInfo {
  enabled: boolean;
  pairing: { active: boolean; expiresIn: number };
  connected: BlePeer | null;
  bonds: BleBond[];
}

export interface Totp {
  code: string;
  period: number;
  remaining: number;
}
