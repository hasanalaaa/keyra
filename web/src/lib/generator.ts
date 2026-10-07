// Password generator (DESIGN §4.8) and onboarding Wi-Fi password (§5.2). Printable US-ASCII only,
// because that is all Keyra can type. Never falls back to Math.random.

export type RandomFill = (buf: Uint32Array<ArrayBuffer>) => unknown;

export const cryptoFill: RandomFill = (buf) => crypto.getRandomValues(buf);

/** Uniform integer in [0, n) by rejection sampling (no modulo bias). */
export function randomInt(n: number, fill: RandomFill = cryptoFill): number {
  if (!Number.isInteger(n) || n <= 0 || n > 2 ** 32) throw new RangeError('n');
  const limit = 2 ** 32 - (2 ** 32 % n);
  const buf = new Uint32Array(1);
  for (;;) {
    fill(buf);
    if (buf[0] < limit) return buf[0] % n;
  }
}

export interface GenOptions {
  length: number;
  upper: boolean;
  lower: boolean;
  digits: boolean;
  symbols: boolean;
  avoidLookAlikes: boolean;
}

export const DEFAULT_GEN: GenOptions = {
  length: 20,
  upper: true,
  lower: true,
  digits: true,
  symbols: true,
  avoidLookAlikes: true,
};

export const SYMBOLS = '!@#$%^&*-_=+?';
const LOOK_ALIKES = new Set('0O1lI|');

export function classes(o: GenOptions): string[] {
  const sets: string[] = [];
  if (o.upper) sets.push('ABCDEFGHIJKLMNOPQRSTUVWXYZ');
  if (o.lower) sets.push('abcdefghijklmnopqrstuvwxyz');
  if (o.digits) sets.push('0123456789');
  if (o.symbols) sets.push(SYMBOLS);
  return o.avoidLookAlikes ? sets.map((s) => [...s].filter((ch) => !LOOK_ALIKES.has(ch)).join('')) : sets;
}

export function generatePassword(o: GenOptions, fill: RandomFill = cryptoFill): string {
  const sets = classes(o);
  if (sets.length === 0) throw new Error('At least one character class is required');
  const length = Math.max(o.length, sets.length);
  const all = sets.join('');
  // One guaranteed character per enabled class, the rest from the union, then shuffle.
  const chars = sets.map((s) => s[randomInt(s.length, fill)]);
  while (chars.length < length) chars.push(all[randomInt(all.length, fill)]);
  for (let i = chars.length - 1; i > 0; i--) {
    const j = randomInt(i + 1, fill);
    [chars[i], chars[j]] = [chars[j], chars[i]];
  }
  return chars.join('');
}

const WIFI_ALPHABET = 'ABCDEFGHJKLMNPQRSTUVWXYZabcdefghjkmnpqrstuvwxyz23456789';

/** 12 random characters shown as xxxx-xxxx-xxxx (14 chars, the hyphens are part of the password). */
export function generateWifiPassword(fill: RandomFill = cryptoFill): string {
  let s = '';
  for (let i = 0; i < 12; i++) {
    if (i > 0 && i % 4 === 0) s += '-';
    s += WIFI_ALPHABET[randomInt(WIFI_ALPHABET.length, fill)];
  }
  return s;
}

/** Characters Keyra cannot type (anything outside printable US-ASCII), de-duplicated. */
// Phone keyboards insert look-alikes Keyra cannot type on a US layout: iOS
// "Smart Punctuation" curly quotes and dashes, Arabic punctuation and digits.
// Credentials are converted to the ASCII character the user meant.
const LOOKALIKES: Record<string, string> = {
  '\u2018': "'", '\u2019': "'", '\u201A': "'", '\u201B': "'", '\u2032': "'",
  '\u201C': '"', '\u201D': '"', '\u201E': '"', '\u2033': '"',
  '\u2013': '-', '\u2014': '-', '\u2212': '-', '\u2026': '...',
  '\u00A0': ' ', '\u060C': ',', '\u061B': ';', '\u061F': '?', '\u066A': '%',
  '\u066B': '.', '\u066C': ',', '\u06D4': '.',
};

export function toTypeable(s: string): string {
  let out = '';
  for (const ch of s) {
    const c = ch.codePointAt(0)!;
    if (c >= 0x0660 && c <= 0x0669) out += String(c - 0x0660); // Arabic-Indic digits
    else if (c >= 0x06f0 && c <= 0x06f9) out += String(c - 0x06f0); // Persian digits
    else out += LOOKALIKES[ch] ?? ch;
  }
  return out;
}

export function untypeable(s: string): string[] {
  const bad = new Set<string>();
  for (const ch of s) {
    const c = ch.codePointAt(0)!;
    if (c < 0x20 || c > 0x7e) bad.add(ch);
  }
  return [...bad];
}
