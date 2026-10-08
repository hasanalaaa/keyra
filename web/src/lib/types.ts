// Shapes of the device API (SPEC §5). This file is the client's view of the contract.

/** `sequence` (SPEC §10.4): the entry's own sequence, else settings.bothSequence, else the built-in Both order. */
export type TypeWhat = 'username' | 'password' | 'both' | 'totp' | 'sequence';
export type ResultCode = 'typed' | 'cancelled' | 'expired' | 'no_usb' | 'no_host' | 'unsupported_char' | 'failed' | 'host_changed';

export interface Pending {
  kind: 'type';
  id: number; // 0 for the type test and free text
  title: string | null; // null for free text (SPEC §9.2)
  what: TypeWhat | 'test' | 'text' | 'probe';
  submit: boolean;
  expiresIn: number;
  target?: string | null; // "usb" or a Bluetooth device address
  /** what "sequence" only (SPEC §10.4): the whole sequence masked, and the part (1-based) the next press types. */
  preview?: string;
  part?: number;
  parts?: number;
}

export interface TypeResult {
  ok: boolean;
  code: ResultCode;
  at: number; // ms ago
  title?: string | null;
  what?: TypeWhat | 'test' | 'text' | 'probe';
}

export type PresenceOp =
  | 'setup'
  | 'wifi'
  | 'restore'
  | 'factory_reset'
  | 'home_wifi'
  | 'trust_browser'
  | 'ble_pair'
  | 'reveal'
  | 'backup'
  | 'recovery'
  | 'unprotect'
  | 'update'
  | 'delete_entry'
  | 'delete_passkey';

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
  device: { name: string; version: string; model: string; mac: string; powerDip: boolean }; // powerDip: this boot followed a brownout reset
  initialized: boolean;
  unlocked: boolean;
  session: boolean;
  autoLockMin: number;
  /** `output`: where a typed action would go right now; null = nothing connected on the selected output. */
  /**
   * `ble`: a Bluetooth host is connected right now. `output`: the kind of host a new action would use (null = none).
   * `bleTarget`: the device the armed action will type into; `connecting`: still waiting for it to connect.
   */
  host: { usb: boolean; ble: boolean; capsLock: boolean; output: 'usb' | 'ble' | null; bleTarget: BlePeer | null; connecting: boolean; usbOs?: HostOs };
  pending: Pending | null;
  last: TypeResult | null;
  presence: Presence;
  net?: NetState; // absent on firmware before v1.1
  timeValid: boolean;
  /** SPEC §12.3: reveal grace left for this session after a press (ms). */
  graceMs?: number;
  /** SPEC §14: a firmware image being received, checked and waiting, or why it failed (sessions only). */
  update?: UpdateState;
}

export interface UpdateState {
  phase: 'receiving' | 'staged' | 'restarting' | 'failed';
  source: 'upload' | 'github';
  done: number; // bytes
  total: number; // bytes, 0 = not known yet
  version: string; // set once staged
  error: string; // UpdateError code when failed
}

/** POST /api/update/check (SPEC §14). */
export interface UpdateCheck {
  current: string;
  latest: string;
  newer: boolean;
  size: number;
  notes: string;
}

export type HomeError = '' | 'wrong_password' | 'not_found' | 'failed';

/** SPEC §8.2 state.net. `via`: how this very request reached Keyra. */
export interface NetState {
  ap: { on: boolean; ssid: string; clients: number };
  /** `error`: why the last join failed ('' = none yet); absent on older firmware. */
  home: { enabled: boolean; connected: boolean; ssid: string; ip: string | null; rssi: number | null; error?: HomeError } | null;
  via: 'ap' | 'home';
}

export interface Network {
  ssid: string;
  rssi: number;
  secure: boolean;
  channel: number;
}

/** GET /api/activity (SPEC §15), newest first. `at` is unix seconds (0 = the device had no clock). */
export interface ActivityEvent {
  kind:
    | 'unlock'
    | 'failed_unlocks'
    | 'lock'
    | 'typed'
    | 'revealed'
    | 'backup'
    | 'restore'
    | 'passphrase'
    | 'recovery_created'
    | 'recovery_removed'
    | 'ble_forgot'
    | 'trusted_removed'
    | 'entry_deleted'
    | 'text_typed'
    | 'ble_pairing'
    | 'rotate_started'
    | 'rotate_ended'
    | 'entry_burned'
    | 'unknown';
  at: number;
  id?: number;
  n?: number;
  detail: number;
  title?: string;
}

/** A discoverable FIDO credential (passkey) stored on Keyra (docs/FIDO.md). */
/** GET /api/health (SPEC §13): entry ids and flags only, never a password. */
export interface Health {
  checked: number; // entries that have a password
  clock: boolean; // false: the device has no time, so nothing can be "old"
  weak: { id: number; level: 1 | 2 }[];
  reused: number[][]; // groups of ids that share one password
  old: { id: number; since: number }[]; // unix seconds the password was set
  /** SPEC §13.1 "change every password": started at `since`; `pending` = not changed since. */
  rotate?: { since: number; pending: number[] };
}

/** A discoverable FIDO credential (passkey) stored on Keyra (docs/FIDO.md). */
export interface Passkey {
  id: number;
  rpId: string;
  userName: string;
  displayName: string;
  created: number; // unix seconds, 0 = unknown
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
  hasSequence?: boolean; // SPEC §10.4; absent on older firmware
  /** SPEC §16: deleted after the password is typed this many more times; 0 = kept. */
  burnAfter?: number;
  updated: number;
  lastUsed: number;
}

/**
 * GET /api/entries/{id}. `revealed` false (SPEC §12.3): no password, 2FA secret or old
 * passwords until a press of Keyra's button (POST …/reveal) opens this session's grace.
 */
export interface Entry {
  id: number;
  title: string;
  url: string;
  username: string;
  revealed: boolean;
  hasPassword: boolean;
  hasTotp: boolean;
  hasSequence?: boolean; // SPEC §10.4; the sequence itself only when revealed
  password?: string;
  totp?: string;
  notes: string;
  favorite: boolean;
  created: number;
  updated: number;
  lastUsed: number;
  burnAfter?: number; // SPEC §16; absent on older firmware
  /** SPEC §10.4: only when revealed ('' = none); it may hold literal secrets. */
  sequence?: string;
  history: OldPassword[]; // newest first, at most 10 (SPEC §9.3)
}

export interface OldPassword {
  password?: string; // only when revealed
  changedAt: number; // unix seconds it was replaced, 0 = unknown
}

export interface EntryInput {
  title: string;
  url: string;
  username: string;
  password: string;
  totp: string;
  notes: string;
  favorite: boolean;
  burnAfter: number; // SPEC §16: 0 = keep
  sequence: string; // SPEC §10.4: '' = none
}

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
  bleEnabled: boolean;
  output: Output;
  bleConnect: 'on_demand' | 'always';
  osUsb: HostOs;
  /** SPEC §10.4: the order "Both" types ('' = built in: username, separator, password, optional Enter). */
  bothSequence?: string;
  /** SPEC §10.1: keyboard layout ids (GET /api/keyboard) of the computer on each output. */
  layoutUsb: string;
  layoutBle: string;
  /** SPEC §12.3-12.5 */
  protectReveal: boolean;
  lockOnUsb: boolean;
  lockOnBle: boolean;
  lastBackupAt: number; // unix seconds, 0 = never
}

export interface RecoveryInfo {
  enabled: boolean;
  created: number; // unix seconds, 0 = unknown
}

/** Where typing goes: `auto` = USB when plugged in, else the connected Bluetooth device. */
export type Output = 'auto' | 'usb' | 'ble';

export interface BlePeer {
  addr: string; // "A4:C1:38:0B:7F:3A"
  name: string; // the device's own name; '' until Keyra has read it
}

/** SPEC §10.5: decides how text survives the host's input language. '' = not set. */
export type HostOs = '' | 'mac' | 'ios' | 'windows' | 'android' | 'linux';

export interface BleBond extends BlePeer {
  lastSeen: number; // unix seconds, 0 = unknown
  os: HostOs;
}

export interface BleInfo {
  enabled: boolean;
  pairing: { active: boolean; expiresIn: number };
  connected: BlePeer | null;
  bonds: BleBond[];
}

/** GET /api/keyboard (SPEC §10.1). */
export interface KeyboardLayout {
  id: string;
  name: string; // English display name
  platform: 'any' | 'windows' | 'mac';
  experimental: boolean; // not yet confirmed on real hardware
  probe: string; // what the Layout Doctor probe leaves on a computer with this layout (SPEC §10.3)
  chars: string; // every character POST /type {text} accepts on an output set to this layout
}

export interface Keyboard {
  layouts: KeyboardLayout[];
  usb: string;
  ble: string;
}

export interface Totp {
  code: string;
  period: number;
  remaining: number;
}
