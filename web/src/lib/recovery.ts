// Recovery kit (SPEC §12.2): how the 20-byte recovery key is written down, and its
// Shamir shares. Splitting and combining happen only in this browser, with the
// audited `shamir-secret-sharing` library (pinned); nothing here is ever sent
// to Keyra except the recombined key itself, when it is used to unlock.
import { combine, split } from 'shamir-secret-sharing';

export const KEY_BYTES = 20;
// Crockford base32: no I, L, O, U, so hand-copied keys survive bad handwriting.
const ALPHABET = '0123456789ABCDEFGHJKMNPQRSTVWXYZ';
const SHARE_VERSION = 1;

/** FNV-1a 32-bit. A typo check, not a security function. */
function fnv(bytes: Uint8Array): number {
  let h = 0x811c9dc5;
  for (const b of bytes) h = Math.imul(h ^ b, 0x01000193) >>> 0;
  return h;
}

function toBase32(bytes: Uint8Array): string {
  let out = '';
  let acc = 0;
  let bits = 0;
  for (const b of bytes) {
    acc = (acc << 8) | b;
    bits += 8;
    while (bits >= 5) {
      out += ALPHABET[(acc >>> (bits - 5)) & 31];
      bits -= 5;
    }
    acc &= (1 << bits) - 1;
  }
  if (bits > 0) out += ALPHABET[(acc << (5 - bits)) & 31];
  return out;
}

function fromBase32(text: string, bytes: number): Uint8Array | null {
  const out = new Uint8Array(bytes);
  let acc = 0;
  let bits = 0;
  let n = 0;
  for (const ch of text) {
    const v = ALPHABET.indexOf(ch);
    if (v < 0) return null;
    acc = (acc << 5) | v;
    bits += 5;
    if (bits >= 8) {
      if (n === bytes) return null;
      out[n++] = (acc >>> (bits - 8)) & 0xff;
      bits -= 8;
      acc &= (1 << bits) - 1;
    }
  }
  return n === bytes ? out : null;
}

/** 20 bits of the payload's hash as 4 characters. */
const check = (payload: Uint8Array): string => {
  const h = fnv(payload) >>> 12;
  return [15, 10, 5, 0].map((s) => ALPHABET[(h >>> s) & 31]).join('');
};

const groups = (s: string): string => s.match(/.{1,4}/g)!.join('-');

/** Uppercase, drop spaces and dashes, read the look-alikes Crockford allows. */
function normalize(text: string): string {
  return text
    .toUpperCase()
    .replace(/[\s\-–—_.]/g, '')
    .replace(/O/g, '0')
    .replace(/[IL]/g, '1');
}

export const toHex = (b: Uint8Array): string => Array.from(b, (x) => x.toString(16).padStart(2, '0')).join('');
export function fromHex(hex: string): Uint8Array {
  const out = new Uint8Array(hex.length / 2);
  for (let i = 0; i < out.length; i++) out[i] = parseInt(hex.slice(2 * i, 2 * i + 2), 16);
  return out;
}

/** "XXXX-XXXX-…" : 32 key characters + 4 check characters, 9 groups. */
export function formatKey(key: Uint8Array): string {
  if (key.length !== KEY_BYTES) throw new Error('recovery key must be 20 bytes');
  return groups(toBase32(key) + check(key));
}

export type ParseError = 'format' | 'check';

export function parseKey(text: string): Uint8Array | ParseError {
  const s = normalize(text);
  if (s.length !== 36) return 'format';
  const key = fromBase32(s.slice(0, 32), KEY_BYTES);
  if (!key) return 'format';
  return check(key) === s.slice(32) ? key : 'check';
}

export interface Share {
  threshold: number;
  bytes: Uint8Array; // the library's share: 20 bytes + its x coordinate
}

/**
 * Splits the key into `n` shares, any `k` of which rebuild it. Each share's text
 * carries a version, k, a 16-bit fingerprint of the key (so k−1 or mismatched
 * shares are caught before anything is sent) and its own check characters.
 */
export async function splitKey(key: Uint8Array, n: number, k: number): Promise<string[]> {
  if (key.length !== KEY_BYTES || k < 2 || k > n || n > 10) throw new Error('bad share settings');
  const fp = fnv(key) & 0xffff;
  const shares = await split(key, n, k);
  return shares.map((sh) => {
    const payload = new Uint8Array([SHARE_VERSION, k, fp >> 8, fp & 0xff, ...sh]);
    return groups(toBase32(payload) + check(payload));
  });
}

interface ParsedShare extends Share {
  fp: number;
}

export function parseShare(text: string): ParsedShare | ParseError {
  const s = normalize(text);
  if (s.length !== 44) return 'format';
  const payload = fromBase32(s.slice(0, 40), 25);
  if (!payload) return 'format';
  if (check(payload) !== s.slice(40)) return 'check';
  if (payload[0] !== SHARE_VERSION || payload[1] < 2) return 'format';
  return { threshold: payload[1], fp: (payload[2] << 8) | payload[3], bytes: payload.slice(4) };
}

export type CombineError = 'too_few' | 'mismatch';

/** Rebuilds the key from parsed shares; checks the fingerprint the shares carry. */
export async function combineShares(shares: ParsedShare[]): Promise<Uint8Array | CombineError> {
  const unique = shares.filter((s, i) => shares.findIndex((o) => o.bytes[KEY_BYTES] === s.bytes[KEY_BYTES]) === i);
  if (unique.length === 0) return 'too_few';
  const { threshold, fp } = unique[0];
  if (unique.some((s) => s.threshold !== threshold || s.fp !== fp)) return 'mismatch';
  if (unique.length < threshold) return 'too_few';
  const key = await combine(unique.slice(0, threshold).map((s) => s.bytes));
  return (fnv(key) & 0xffff) === fp ? key : 'mismatch';
}
