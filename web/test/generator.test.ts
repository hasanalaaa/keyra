import { afterEach, describe, expect, it, vi } from 'vitest';
import {
  GEN_DEFAULTS,
  SYMBOLS,
  acceptance,
  cleanSymbolSet,
  entropyBits,
  generateRequest,
  generateWifiPassword,
  loadGenSettings,
  maxMinimum,
  normalize,
  randomInt,
  saveGenSettings,
  untypeable,
  valid,
  type GenSettings,
  type RandomFill,
} from '../src/lib/generator';

afterEach(() => vi.restoreAllMocks());

// The device generates (SPEC §9.1); the browser mirrors its rules to show exact entropy and to
// offer only settings the device accepts (firmware keyra_api/src/generator.cpp).
describe('generator settings', () => {
  const base: GenSettings = { ...GEN_DEFAULTS };

  /** Brute force over every candidate of a tiny alphabet (the classes only matter by size). */
  function enumerate(sizes: number[], mins: number[], L: number): number {
    const n = sizes.reduce((a, b) => a + b, 0);
    const classOf: number[] = [];
    sizes.forEach((s, k) => classOf.push(...new Array(s).fill(k)));
    let good = 0;
    for (let v = 0; v < n ** L; v++) {
      const counts = new Array(sizes.length).fill(0);
      for (let i = 0, x = v; i < L; i++, x = Math.floor(x / n)) counts[classOf[x % n]]++;
      if (counts.every((c, k) => c >= mins[k])) good++;
    }
    return good / n ** L;
  }

  it('computes the exact acceptance the device uses', () => {
    // digits (8 with look-alikes avoided) + symbols (13), at least 2 of each, 5 long.
    const s = { ...base, lower: false, upper: false, minDigits: 2, minSymbols: 2, length: 5 };
    expect(acceptance(s)).toBeCloseTo(enumerate([8, 13], [2, 2], 5), 12);
    // upper (24) + digits (8) + symbols (13), 4 long, at least 2 digits.
    const t2 = { ...base, lower: false, minDigits: 2, minSymbols: 1, length: 4 };
    expect(acceptance(t2)).toBeCloseTo(enumerate([24, 8, 13], [1, 2, 1], 4), 12);
  });

  it('reports entropy as log2 of the passwords it can produce', () => {
    const digits = { ...base, lower: false, upper: false, symbols: false, avoidAmbiguous: false, length: 10 };
    expect(entropyBits(digits)).toBeCloseTo(10 * Math.log2(10), 9);
    const all = { ...base, avoidAmbiguous: false };
    expect(entropyBits(all)).toBeLessThan(20 * Math.log2(75));
    expect(entropyBits(all)).toBeGreaterThan(20 * Math.log2(75) - 1);
  });

  it('accepts the same edges as the device and refuses impossible minimums', () => {
    expect(valid({ ...base, length: 8 })).toBe(true);
    expect(valid({ ...base, length: 128 })).toBe(true);
    expect(valid({ ...base, length: 7 })).toBe(false);
    expect(valid({ ...base, length: 129 })).toBe(false);
    expect(valid({ ...base, lower: false, upper: false, digits: false, symbols: false })).toBe(false);
    // Floors 1+1+3+3 = 8 at length 8: acceptance ≈ 1.7e-3, just inside the device's limit.
    expect(valid({ ...base, length: 8, minDigits: 3, minSymbols: 3, avoidAmbiguous: false })).toBe(true);
    expect(valid({ ...base, length: 8, minDigits: 3, minSymbols: 4 })).toBe(false); // 9 > 8
    expect(valid({ ...base, length: 128, minDigits: 60 })).toBe(false); // hopeless
  });

  it('caps the steppers at what the device accepts, and normalizes stored settings', () => {
    const s = { ...base, length: 8, avoidAmbiguous: false };
    expect(maxMinimum(s, 'minDigits')).toBeGreaterThanOrEqual(3);
    expect(valid({ ...s, minDigits: maxMinimum(s, 'minDigits') })).toBe(true);
    expect(valid({ ...s, minDigits: maxMinimum(s, 'minDigits') + 1 })).toBe(false);
    const n = normalize({ ...base, length: 500, lower: false, upper: false, digits: false, symbols: false, minDigits: 99 });
    expect(n.length).toBe(128);
    expect(n.lower).toBe(true);
    expect(valid(n)).toBe(true);
    expect(normalize({ ...base, length: 8, minDigits: 7 }).minDigits).toBe(maxMinimum({ ...base, length: 8 }, 'minDigits'));
  });

  it('sends minimums only for enabled classes', () => {
    expect(generateRequest({ ...base, symbols: false, minSymbols: 5 })).toMatchObject({ symbols: false, minSymbols: 0, minDigits: 1 });
  });

  it('sends a custom symbol set and the layout-safe option only when chosen (SPEC §9.1, §10.2)', () => {
    const plain = generateRequest(base, ['us', 'de']);
    expect(plain).not.toHaveProperty('symbolSet');
    expect(plain).not.toHaveProperty('layoutSafe');
    expect(plain).not.toHaveProperty('layouts');
    expect(generateRequest({ ...base, symbolSet: '-_.' })).toMatchObject({ symbolSet: '-_.' });
    // Both outputs on one layout: named once.
    expect(generateRequest({ ...base, layoutSafe: true }, ['de', 'de'])).toMatchObject({ layoutSafe: true, layouts: ['de'] });
    expect(generateRequest({ ...base, layoutSafe: true }, ['us', 'fr'])).toMatchObject({ layouts: ['us', 'fr'] });
    // Unknown: the device uses its outputs' layouts, and "layouts": [] would be refused.
    const unknown = generateRequest({ ...base, layoutSafe: true });
    expect(unknown).toMatchObject({ layoutSafe: true });
    expect(unknown).not.toHaveProperty('layouts');
  });

  it('keeps a symbol set to what the device accepts', () => {
    expect(cleanSymbolSet('!!@ a1#é$')).toBe('!@#$');
    expect(cleanSymbolSet('')).toBe('');
    const all = '!"#$%&\'()*+,-./:;<=>?@[\\]^_`{|}~';
    expect(all).toHaveLength(32);
    expect(cleanSymbolSet(all + all)).toBe(all);
    // Its size drives the exact entropy, like the device's alphabet.
    const digitsAndSet = { ...base, lower: false, upper: false, avoidAmbiguous: false, minDigits: 1, minSymbols: 1 };
    expect(entropyBits({ ...digitsAndSet, symbolSet: '-_' })).toBeLessThan(entropyBits({ ...digitsAndSet, symbolSet: '' }));
    expect(acceptance({ ...digitsAndSet, symbolSet: SYMBOLS })).toBeCloseTo(acceptance({ ...digitsAndSet, symbolSet: '' }), 12);
    // Only look-alikes would leave no symbols while they are avoided: back to the default set.
    expect(normalize({ ...base, symbolSet: '|`' }).symbolSet).toBe('');
    expect(normalize({ ...base, avoidAmbiguous: false, symbolSet: '|`' }).symbolSet).toBe('|`');
    expect(normalize({ ...base, symbolSet: 'aa--' }).symbolSet).toBe('-');
  });

  it('remembers settings per browser, and survives blocked or bad storage', () => {
    const store = new Map<string, string>();
    vi.stubGlobal('localStorage', { getItem: (k: string) => store.get(k) ?? null, setItem: (k: string, v: string) => store.set(k, v) });
    saveGenSettings({ ...base, length: 32, symbols: false });
    expect(loadGenSettings()).toMatchObject({ length: 32, symbols: false });
    saveGenSettings({ ...base, symbolSet: '-_', layoutSafe: true });
    expect(loadGenSettings()).toMatchObject({ symbolSet: '-_', layoutSafe: true });
    // Settings stored before these options existed get the defaults.
    store.set('keyra.gen', '{"length":24}');
    expect(loadGenSettings()).toMatchObject({ length: 24, symbolSet: '', layoutSafe: false });
    expect(store.get('keyra.gen')).not.toMatch(/password/);
    store.set('keyra.gen', '{"length":"x","digits":1}');
    expect(loadGenSettings()).toEqual(GEN_DEFAULTS);
    store.set('keyra.gen', 'not json');
    expect(loadGenSettings()).toEqual(GEN_DEFAULTS);
    vi.stubGlobal('localStorage', { getItem: () => { throw new Error('blocked'); }, setItem: () => { throw new Error('blocked'); } });
    expect(loadGenSettings()).toEqual(GEN_DEFAULTS);
    expect(() => saveGenSettings(base)).not.toThrow();
    vi.unstubAllGlobals();
  });
});

describe('generateWifiPassword never uses Math.random', () => {
  it('draws from crypto.getRandomValues', () => {
    const spy = vi.spyOn(Math, 'random');
    const c = vi.spyOn(crypto, 'getRandomValues');
    generateWifiPassword();
    expect(spy).not.toHaveBeenCalled();
    expect(c).toHaveBeenCalled();
  });
});

describe('randomInt', () => {
  it('rejects values in the biased tail instead of taking a modulo', () => {
    // n = 3: limit = 2^32 - (2^32 % 3) = 4294967295, so 4294967295 must be re-drawn.
    const draws = [4294967295, 7];
    const fill: RandomFill = (buf) => {
      buf[0] = draws.shift()!;
    };
    expect(randomInt(3, fill)).toBe(1);
    expect(draws).toEqual([]);
  });

  it('validates its bound', () => {
    expect(() => randomInt(0)).toThrow(RangeError);
    expect(() => randomInt(1.5)).toThrow(RangeError);
  });
});

describe('generateWifiPassword', () => {
  it('is 12 unambiguous characters shown as xxxx-xxxx-xxxx', () => {
    const pw = generateWifiPassword();
    expect(pw).toMatch(/^[A-HJ-NP-Za-km-np-z2-9]{4}-[A-HJ-NP-Za-km-np-z2-9]{4}-[A-HJ-NP-Za-km-np-z2-9]{4}$/);
  });
});

describe('untypeable', () => {
  it('lists characters outside printable US-ASCII once each', () => {
    expect(untypeable('café-éé-ع\t')).toEqual(['é', 'ع', '\t']);
    expect(untypeable('Plain ASCII ~!')).toEqual([]);
  });
});

import { toTypeable as _toTypeable } from '../src/lib/generator';
describe('toTypeable', () => {
  it('maps phone look-alikes to the ASCII the user meant', () => {
    expect(_toTypeable('it’s “ok” — 1…2')).toBe('it\'s "ok" - 1...2');
    expect(_toTypeable('١٢٣؟،٪')).toBe('123?,%');
    expect(_toTypeable('P@ss-w0rd!')).toBe('P@ss-w0rd!');
  });
});
