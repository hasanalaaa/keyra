import { afterEach, describe, expect, it, vi } from 'vitest';
import { DEFAULT_GEN, SYMBOLS, classes, generatePassword, generateWifiPassword, randomInt, untypeable, type RandomFill } from '../src/lib/generator';

afterEach(() => vi.restoreAllMocks());

describe('generatePassword', () => {
  it('honours the requested length across the slider range', () => {
    for (const length of [12, 20, 33, 40]) expect(generatePassword({ ...DEFAULT_GEN, length })).toHaveLength(length);
  });

  it('contains at least one character of every enabled class, and nothing else', () => {
    for (let i = 0; i < 200; i++) {
      const pw = generatePassword(DEFAULT_GEN);
      expect(pw).toMatch(/[A-Z]/);
      expect(pw).toMatch(/[a-z]/);
      expect(pw).toMatch(/[0-9]/);
      expect([...pw].some((c) => SYMBOLS.includes(c))).toBe(true);
      expect(pw).not.toMatch(/[0O1lI|]/); // look-alikes avoided by default
      expect(untypeable(pw)).toEqual([]);
    }
  });

  it('restricts to the enabled classes', () => {
    const digitsOnly = { ...DEFAULT_GEN, upper: false, lower: false, symbols: false, avoidLookAlikes: false, length: 30 };
    expect(generatePassword(digitsOnly)).toMatch(/^[0-9]{30}$/);
    const lettersOnly = { ...DEFAULT_GEN, digits: false, symbols: false, length: 30 };
    expect(generatePassword(lettersOnly)).toMatch(/^[A-Za-z]{30}$/);
  });

  it('refuses when no class is enabled', () => {
    expect(() => generatePassword({ ...DEFAULT_GEN, upper: false, lower: false, digits: false, symbols: false })).toThrow();
  });

  it('draws from crypto.getRandomValues by default', () => {
    const spy = vi.spyOn(crypto, 'getRandomValues');
    generatePassword(DEFAULT_GEN);
    expect(spy).toHaveBeenCalled();
  });

  it('never uses Math.random', () => {
    const spy = vi.spyOn(Math, 'random');
    generatePassword(DEFAULT_GEN);
    generateWifiPassword();
    expect(spy).not.toHaveBeenCalled();
  });

  it('uses every character of the alphabet over many draws (no stuck positions)', () => {
    const alphabet = classes(DEFAULT_GEN).join('');
    const seen = new Set<string>();
    for (let i = 0; i < 300; i++) for (const c of generatePassword(DEFAULT_GEN)) seen.add(c);
    expect(seen.size).toBe(alphabet.length);
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
