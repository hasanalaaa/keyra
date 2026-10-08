// Generator settings for the device's hardware-RNG generator (SPEC §9.1) and the onboarding
// Wi-Fi password (DESIGN §5.2), which the browser makes itself because setup has no session yet.
// Printable US-ASCII only, because that is all Keyra can type. Never falls back to Math.random.

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

// ---------- device generator settings (SPEC §9.1) ----------
// Passwords are generated on Keyra from its hardware RNG (POST /api/generate).
// This mirrors the device's rules so the sheet can show entropy instantly and
// never offer settings the device would refuse (firmware keyra_api/src/generator.cpp).

export interface GenSettings {
  length: number;
  lower: boolean;
  upper: boolean;
  digits: boolean;
  symbols: boolean;
  minDigits: number;
  minSymbols: number;
  avoidAmbiguous: boolean;
  /** SPEC §9.1 `symbolSet`: the symbols to draw from; '' = the device's default set. */
  symbolSet: string;
  /** SPEC §10.2: only characters every computer Keyra types into gets right. */
  layoutSafe: boolean;
}

export const MIN_LENGTH = 8;
export const MAX_LENGTH = 128;
export const SYMBOLS = '!@#$%^&*-_=+?';
export const AMBIGUOUS = '0Oo1lI|`\'"';
/** Settings the device refuses: candidates would meet the minimums less often than this. */
export const MIN_ACCEPTANCE = 1e-3;

export const GEN_DEFAULTS: GenSettings = {
  length: 20,
  lower: true,
  upper: true,
  digits: true,
  symbols: true,
  minDigits: 1,
  minSymbols: 1,
  avoidAmbiguous: true,
  symbolSet: '',
  layoutSafe: false,
};

/** SPEC §9.1: a custom symbol set is 1-32 distinct ASCII punctuation characters. */
export const MAX_SYMBOL_SET = 32;
const PUNCTUATION = /[!-/:-@[-`{-~]/;

/** What the user typed as a symbol set, reduced to what the device accepts (in order, no repeats). */
export function cleanSymbolSet(input: string): string {
  let out = '';
  for (const ch of input) if (PUNCTUATION.test(ch) && !out.includes(ch) && out.length < MAX_SYMBOL_SET) out += ch;
  return out;
}

const withoutAmbiguous = (chars: string) => [...chars].filter((c) => !AMBIGUOUS.includes(c)).join('');

interface Klass {
  size: number;
  min: number;
}

function klasses(s: GenSettings): Klass[] {
  const drop = (chars: string) => (s.avoidAmbiguous ? withoutAmbiguous(chars) : chars).length;
  const out: Klass[] = [];
  if (s.lower) out.push({ size: drop('abcdefghijklmnopqrstuvwxyz'), min: 1 });
  if (s.upper) out.push({ size: drop('ABCDEFGHIJKLMNOPQRSTUVWXYZ'), min: 1 });
  if (s.digits) out.push({ size: drop('0123456789'), min: Math.max(1, s.minDigits) });
  if (s.symbols) out.push({ size: drop(s.symbolSet || SYMBOLS), min: Math.max(1, s.minSymbols) });
  return out;
}

/** P(length uniform draws meet every class minimum), exactly: L!·[x^L] Π Σ_{c≥min}(q x)^c/c!. */
export function acceptance(s: GenSettings): number {
  const ks = klasses(s);
  const n = ks.reduce((a, k) => a + k.size, 0);
  const L = s.length;
  if (!n || L <= 0) return 0;
  let acc = new Array<number>(L + 1).fill(0);
  acc[0] = 1;
  for (const k of ks) {
    const q = k.size / n;
    const term: number[] = [];
    let t = 1;
    for (let c = 0; c <= L; c++) {
      if (c > 0) t *= q / c;
      term.push(c >= k.min ? t : 0);
    }
    const next = new Array<number>(L + 1).fill(0);
    for (let a = 0; a <= L; a++) if (acc[a]) for (let c = 0; a + c <= L; c++) next[a + c] += acc[a] * term[c];
    acc = next;
  }
  let f = 1;
  for (let i = 2; i <= L; i++) f *= i;
  return Math.min(1, acc[L] * f);
}

/** The device would accept these settings. */
export function valid(s: GenSettings): boolean {
  const ks = klasses(s);
  if (s.length < MIN_LENGTH || s.length > MAX_LENGTH || ks.length === 0) return false;
  if (ks.reduce((a, k) => a + k.min, 0) > s.length) return false;
  return acceptance(s) >= MIN_ACCEPTANCE;
}

/**
 * log2 of how many passwords these settings can produce (all equally likely). With `layoutSafe` the
 * device drops characters the browser cannot know about, so only its own `entropyBits` is exact then.
 */
export function entropyBits(s: GenSettings): number {
  const p = acceptance(s);
  if (!p) return 0;
  const n = klasses(s).reduce((a, k) => a + k.size, 0);
  return s.length * Math.log2(n) + Math.log2(p);
}

/** Largest minimum the device still accepts for that class, given the rest (≥ 1). */
export function maxMinimum(s: GenSettings, key: 'minDigits' | 'minSymbols'): number {
  let best = 1;
  for (let v = 2; v <= s.length; v++) {
    if (!valid({ ...s, [key]: v })) break;
    best = v;
  }
  return best;
}

/** Brings any combination (a stored one, or after a toggle/length change) back to one the device accepts. */
export function normalize(s: GenSettings): GenSettings {
  const n: GenSettings = { ...s, length: Math.round(Math.min(MAX_LENGTH, Math.max(MIN_LENGTH, Number(s.length) || GEN_DEFAULTS.length))) };
  if (!n.lower && !n.upper && !n.digits && !n.symbols) n.lower = true;
  n.symbolSet = cleanSymbolSet(String(n.symbolSet ?? ''));
  // A set made only of look-alikes would leave the symbols empty: the device refuses that.
  if (n.avoidAmbiguous && !withoutAmbiguous(n.symbolSet)) n.symbolSet = '';
  n.layoutSafe = n.layoutSafe === true;
  n.minDigits = Math.max(1, Math.round(Number(n.minDigits) || 1));
  n.minSymbols = Math.max(1, Math.round(Number(n.minSymbols) || 1));
  n.minDigits = Math.min(n.minDigits, maxMinimum({ ...n, minDigits: 1 }, 'minDigits'));
  n.minSymbols = Math.min(n.minSymbols, maxMinimum(n, 'minSymbols'));
  return n;
}

/**
 * Body of POST /api/generate. `layouts`: the computers' layout ids for `layoutSafe` (SPEC §10.2);
 * when unknown the device uses the ones set for its outputs, which is the same thing.
 */
export function generateRequest(s: GenSettings, layouts: readonly string[] = []) {
  const ids = [...new Set(layouts)];
  return {
    length: s.length,
    lower: s.lower,
    upper: s.upper,
    digits: s.digits,
    symbols: s.symbols,
    minDigits: s.digits ? s.minDigits : 0,
    minSymbols: s.symbols ? s.minSymbols : 0,
    avoidAmbiguous: s.avoidAmbiguous,
    ...(s.symbolSet ? { symbolSet: s.symbolSet } : {}),
    ...(s.layoutSafe ? { layoutSafe: true, ...(ids.length ? { layouts: ids } : {}) } : {}),
  };
}

const GEN_KEY = 'keyra.gen';

/** Last settings in this browser (never the password itself). */
export function loadGenSettings(): GenSettings {
  try {
    const raw = JSON.parse(localStorage.getItem(GEN_KEY) ?? 'null') as Partial<GenSettings> | null;
    if (!raw || typeof raw !== 'object') return GEN_DEFAULTS;
    const s = { ...GEN_DEFAULTS };
    for (const k of Object.keys(GEN_DEFAULTS) as (keyof GenSettings)[]) {
      if (typeof raw[k] === typeof GEN_DEFAULTS[k]) (s as Record<string, unknown>)[k] = raw[k];
    }
    return normalize(s);
  } catch {
    return GEN_DEFAULTS;
  }
}

export function saveGenSettings(s: GenSettings): void {
  try {
    localStorage.setItem(GEN_KEY, JSON.stringify(s));
  } catch {
    // Blocked storage (private mode): the settings just aren't remembered.
  }
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

export function toTypeable(s: string, chars?: string): string {
  let out = '';
  for (const ch of s) {
    const c = ch.codePointAt(0)!;
    if (chars?.includes(ch)) out += ch; // the layout has it (e.g. Arabic digits on an Arabic Mac)
    else if (c >= 0x0660 && c <= 0x0669) out += String(c - 0x0660); // Arabic-Indic digits
    else if (c >= 0x06f0 && c <= 0x06f9) out += String(c - 0x06f0); // Persian digits
    else out += LOOKALIKES[ch] ?? ch;
  }
  return out;
}

/**
 * Characters Keyra cannot type, de-duplicated: outside `chars` (a layout's GET /api/keyboard
 * "chars", what POST /type {text} accepts there) or, without it, outside printable US-ASCII.
 */
export function untypeable(s: string, chars?: string): string[] {
  const bad = new Set<string>();
  for (const ch of s) {
    const c = ch.codePointAt(0)!;
    if (chars !== undefined ? !chars.includes(ch) : c < 0x20 || c > 0x7e) bad.add(ch);
  }
  return [...bad];
}
