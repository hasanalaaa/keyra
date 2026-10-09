// Messages between the page (content script), the toolbar popup and the service worker.
// The service worker is the only part that talks to Keyra and holds the access token.
import type { ErrorCode, GenOptions, Login, Status, What } from './api';

export interface Settings {
  showIcons: boolean;
  offerSave: boolean;
  /** Hosts (normalised) where saving is never offered. */
  never: string[];
  gen: GenOptions;
}

export interface Failure {
  ok: false;
  code: ErrorCode;
  message?: string;
  retryAfterMs?: number;
  address?: string;
}
export type Result<T> = ({ ok: true } & T) | Failure;

/** A save or update the page offers after a form was sent (no password in it). */
export interface SaveCard {
  mode: 'create' | 'update';
  host: string;
  title: string;
  username: string;
  /** The login to update (mode "update"). */
  replace?: number;
}

export type Reach = 'not_connected' | 'unreachable' | 'not_keyra' | 'uninitialized' | 'locked' | 'ready' | 'invalid_token' | 'forbidden';

/** From a content script; the service worker takes the page host from the sender, never from the message. */
export type PageMsg =
  | { t: 'config' }
  | { t: 'match'; username?: string }
  | { t: 'entries' }
  | { t: 'type'; id: number; what: What; anyHost?: boolean }
  | { t: 'status' }
  | { t: 'cancel' }
  | { t: 'generate' }
  | { t: 'offer'; username: string; password: string; isNew: boolean; title: string }
  | { t: 'pending' }
  | { t: 'decide'; decision: 'save' | 'later' | 'never' }
  | { t: 'keepalive' }
  | { t: 'username'; username: string }
  | { t: 'openKeyra' }
  | { t: 'paired'; n: string; token: string };

export interface PageConfig {
  connected: boolean;
  showIcons: boolean;
  /** This page is Keyra's own web app: the extension stays out of it. */
  self: boolean;
  address?: string;
}

export type PopupMsg =
  | { t: 'popup' }
  | { t: 'pair'; address: string }
  | { t: 'paste'; address: string; token: string }
  | { t: 'popupMatch'; host: string }
  | { t: 'popupEntries' }
  | { t: 'popupGenerate'; gen: GenOptions }
  | { t: 'settings'; patch: Partial<Settings> }
  | { t: 'disconnect' };

export interface PopupState {
  address?: string;
  connected: boolean;
  reach: Reach;
  pairing: boolean;
  settings: Settings;
}

/** From the popup to the content script of a tab. */
export type TabMsg = { t: 'hello' } | { t: 'arm'; login: Login };
export interface Hello {
  host: string;
  hasField: boolean;
}

export type { Login, Status, What };
