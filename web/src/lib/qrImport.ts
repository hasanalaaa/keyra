// What a scanned QR holds: a plain otpauth://totp/… link or a Google Authenticator export
// (otpauth-migration://offline?data=…, a base64 protobuf). Pure functions, no DOM.
// Support matches the firmware (keyra_vault totp_core.cpp): TOTP only, SHA-1/256/512, 6 or 8 digits, 30 or 60 s.

export type Algorithm = 'SHA1' | 'SHA256' | 'SHA512';

export interface OtpAccount {
  issuer: string;
  /** The account part of the label (usually an email or user name). */
  account: string;
  /** RFC 4648 base32, upper case, no padding. */
  secret: string;
  algorithm: Algorithm;
  digits: 6 | 8;
  period: 30 | 60;
}

export type OtpError = 'notOtp' | 'hotp' | 'algorithm' | 'digits' | 'period' | 'secret' | 'badData';

export type Parsed<T> = { ok: true; value: T } | { ok: false; error: OtpError };

export interface Migration {
  accounts: OtpAccount[];
  /** Entries Keyra can't make codes for (HOTP, MD5, empty key…). */
  skipped: number;
  /** Google splits big exports over several QRs that share a batchId. */
  batchId: number;
  batchSize: number;
  batchIndex: number;
}

export type QrContent = { kind: 'totp'; account: OtpAccount } | { kind: 'migration'; migration: Migration };

const BASE32_ALPHABET = 'ABCDEFGHIJKLMNOPQRSTUVWXYZ234567';
const BASE32 = /^[A-Z2-7]+=*$/;
const ALGORITHMS: Record<string, Algorithm> = { sha1: 'SHA1', sha256: 'SHA256', sha512: 'SHA512' };

const fail = <T>(error: OtpError): Parsed<T> => ({ ok: false, error });
const ok = <T>(value: T): Parsed<T> => ({ ok: true, value });

function decode(s: string): string | null {
  try {
    return decodeURIComponent(s);
  } catch {
    return null;
  }
}

/** Splits a query string, decoding names and values; a bad escape yields null. */
function query(qs: string): Map<string, string> | null {
  const out = new Map<string, string>();
  for (const pair of qs.split('&')) {
    if (!pair) continue;
    const eq = pair.indexOf('=');
    if (eq < 0) return null;
    const k = decode(pair.slice(0, eq));
    const v = decode(pair.slice(eq + 1));
    if (k === null || v === null) return null;
    out.set(k.toLowerCase(), v);
  }
  return out;
}

/** "Issuer:account" label → parts. The issuer parameter, when present, wins over the label prefix. */
function splitLabel(label: string, issuerParam: string): { issuer: string; account: string } {
  const colon = label.indexOf(':');
  const prefix = colon >= 0 ? label.slice(0, colon).trim() : '';
  const rest = (colon >= 0 ? label.slice(colon + 1) : label).trim();
  const issuer = issuerParam.trim() || prefix;
  return { issuer, account: rest };
}

export function base32Encode(bytes: Uint8Array): string {
  let out = '';
  let buffer = 0;
  let bits = 0;
  for (const b of bytes) {
    buffer = (buffer << 8) | b;
    bits += 8;
    while (bits >= 5) {
      out += BASE32_ALPHABET[(buffer >>> (bits - 5)) & 31];
      bits -= 5;
    }
    buffer &= (1 << bits) - 1;
  }
  if (bits > 0) out += BASE32_ALPHABET[(buffer << (5 - bits)) & 31];
  return out;
}

/** Parses `otpauth://totp/…` with the same accept/reject rules as the firmware. */
export function parseOtpauth(uri: string): Parsed<OtpAccount> {
  const s = uri.trim();
  if (!/^otpauth:\/\//i.test(s)) return fail('notOtp');
  const rest = s.slice('otpauth://'.length);
  const slash = rest.search(/[/?]/);
  const type = (slash < 0 ? rest : rest.slice(0, slash)).toLowerCase();
  if (type === 'hotp') return fail('hotp');
  if (type !== 'totp') return fail('notOtp');
  const q = rest.indexOf('?');
  if (q < 0) return fail('secret');
  const params = query(rest.slice(q + 1));
  if (!params) return fail('badData');

  const secret = (params.get('secret') ?? '').replace(/\s+/g, '').toUpperCase();
  if (!secret || !BASE32.test(secret)) return fail('secret');

  const alg = params.get('algorithm');
  const algorithm = alg === undefined ? 'SHA1' : ALGORITHMS[alg.toLowerCase()];
  if (!algorithm) return fail('algorithm');
  const dig = params.get('digits');
  if (dig !== undefined && dig !== '6' && dig !== '8') return fail('digits');
  const per = params.get('period');
  if (per !== undefined && per !== '30' && per !== '60') return fail('period');

  const path = slash >= 0 && rest[slash] === '/' ? rest.slice(slash + 1, q) : '';
  const label = decode(path);
  if (label === null) return fail('badData');
  return ok({
    ...splitLabel(label, params.get('issuer') ?? ''),
    secret: secret.replace(/=+$/, ''),
    algorithm,
    digits: dig === '8' ? 8 : 6,
    period: per === '60' ? 60 : 30,
  });
}

/** The canonical link stored in an entry's 2FA field (the firmware reads it back with the same rules). */
export function toOtpauth(a: OtpAccount): string {
  const label = a.issuer ? `${encodeURIComponent(a.issuer)}:${encodeURIComponent(a.account)}` : encodeURIComponent(a.account);
  let uri = `otpauth://totp/${label}?secret=${a.secret}`;
  if (a.issuer) uri += `&issuer=${encodeURIComponent(a.issuer)}`;
  if (a.algorithm !== 'SHA1') uri += `&algorithm=${a.algorithm}`;
  if (a.digits !== 6) uri += `&digits=${a.digits}`;
  if (a.period !== 30) uri += `&period=${a.period}`;
  return uri;
}

/** Entry title: the service name, or the account when the QR names no service. */
export const titleOf = (a: OtpAccount): string => a.issuer || a.account;

/** Stable identity of an account across the several QRs of one export. */
export const accountKey = (a: OtpAccount): string => `${a.secret}\u0000${a.issuer}\u0000${a.account}`;

// ---------- Google Authenticator export ----------

class Reader {
  pos = 0;
  constructor(private readonly b: Uint8Array) {}
  get done(): boolean {
    return this.pos >= this.b.length;
  }
  /** Unsigned varint up to 2^53; longer values are corrupt for this format. */
  varint(): number {
    let result = 0;
    let scale = 1;
    for (let i = 0; i < 8; i++) {
      if (this.pos >= this.b.length) throw new Error('truncated');
      const byte = this.b[this.pos++];
      result += (byte & 0x7f) * scale;
      if (!(byte & 0x80)) return result;
      scale *= 128;
    }
    throw new Error('varint too long');
  }
  bytes(): Uint8Array {
    const n = this.varint();
    if (n > this.b.length - this.pos) throw new Error('truncated');
    const out = this.b.subarray(this.pos, this.pos + n);
    this.pos += n;
    return out;
  }
  skip(wire: number): void {
    if (wire === 0) this.varint();
    else if (wire === 1) this.advance(8);
    else if (wire === 2) this.bytes();
    else if (wire === 5) this.advance(4);
    else throw new Error('unsupported wire type');
  }
  private advance(n: number): void {
    if (n > this.b.length - this.pos) throw new Error('truncated');
    this.pos += n;
  }
}

const utf8 = new TextDecoder('utf-8', { fatal: true });

interface RawOtp {
  secret: Uint8Array;
  name: string;
  issuer: string;
  algorithm: number;
  digits: number;
  type: number;
}

function readOtp(b: Uint8Array): RawOtp {
  const r = new Reader(b);
  const o: RawOtp = { secret: new Uint8Array(0), name: '', issuer: '', algorithm: 0, digits: 0, type: 0 };
  while (!r.done) {
    const tag = r.varint();
    const field = Math.floor(tag / 8);
    const wire = tag % 8;
    if (field === 1 && wire === 2) o.secret = r.bytes();
    else if (field === 2 && wire === 2) o.name = utf8.decode(r.bytes());
    else if (field === 3 && wire === 2) o.issuer = utf8.decode(r.bytes());
    else if (field === 4 && wire === 0) o.algorithm = r.varint();
    else if (field === 5 && wire === 0) o.digits = r.varint();
    else if (field === 6 && wire === 0) o.type = r.varint();
    else r.skip(wire); // counter (7) and anything newer
  }
  return o;
}

/** protobuf enums: Algorithm 0 unspecified·1 SHA1·2 SHA256·3 SHA512·4 MD5; DigitCount 0·1 six·2 eight; OtpType 0·1 HOTP·2 TOTP. */
function toAccount(o: RawOtp): OtpAccount | null {
  const algorithm = [undefined, 'SHA1', 'SHA256', 'SHA512'][o.algorithm] as Algorithm | undefined;
  if (o.type !== 2 || (!algorithm && o.algorithm !== 0) || o.secret.length === 0 || o.digits > 2) return null;
  const { issuer, account } = splitLabel(o.name, o.issuer);
  return { issuer, account, secret: base32Encode(o.secret), algorithm: algorithm ?? 'SHA1', digits: o.digits === 2 ? 8 : 6, period: 30 };
}

function base64Bytes(b64: string): Uint8Array | null {
  // Standard or URL-safe alphabet, padding optional; spaces appear when '+' was left unescaped in the link.
  const s = b64.replace(/ /g, '+').replace(/-/g, '+').replace(/_/g, '/').replace(/=+$/, '');
  if (!/^[A-Za-z0-9+/]*$/.test(s) || s.length % 4 === 1) return null;
  try {
    const bin = atob(s + '='.repeat((4 - (s.length % 4)) % 4));
    return Uint8Array.from(bin, (c) => c.charCodeAt(0));
  } catch {
    return null;
  }
}

export function parseMigration(uri: string): Parsed<Migration> {
  const s = uri.trim();
  if (!/^otpauth-migration:\/\//i.test(s)) return fail('notOtp');
  const q = s.indexOf('?');
  const params = q < 0 ? null : query(s.slice(q + 1));
  const data = params?.get('data');
  const payload = data ? base64Bytes(data) : null;
  if (!payload) return fail('badData');

  const m: Migration = { accounts: [], skipped: 0, batchId: 0, batchSize: 1, batchIndex: 0 };
  try {
    const r = new Reader(payload);
    while (!r.done) {
      const tag = r.varint();
      const field = Math.floor(tag / 8);
      const wire = tag % 8;
      if (field === 1 && wire === 2) {
        const a = toAccount(readOtp(r.bytes()));
        if (a) m.accounts.push(a);
        else m.skipped++;
      } else if (field === 3 && wire === 0) m.batchSize = Math.max(1, r.varint());
      else if (field === 4 && wire === 0) m.batchIndex = r.varint();
      else if (field === 5 && wire === 0) m.batchId = r.varint();
      else r.skip(wire); // version (2)
    }
  } catch {
    return fail('badData');
  }
  if (m.accounts.length === 0 && m.skipped === 0) return fail('badData');
  return ok(m);
}

/** Classifies the text of a decoded QR. */
export function parseQrText(text: string): Parsed<QrContent> {
  if (/^otpauth-migration:/i.test(text.trim())) {
    const m = parseMigration(text);
    return m.ok ? ok({ kind: 'migration', migration: m.value }) : m;
  }
  const a = parseOtpauth(text);
  return a.ok ? ok({ kind: 'totp', account: a.value }) : a;
}
