import { describe, expect, it } from 'vitest';
import { bits, strength } from '../src/lib/strength';

describe('strength', () => {
  it('is 0 for empty input', () => {
    expect(strength('')).toBe(0);
    expect(bits('')).toBe(0);
  });

  it('forces Weak for common passwords regardless of case', () => {
    expect(strength('password')).toBe(1);
    expect(strength('Keyra1234')).toBe(1);
    expect(strength('QWERTY')).toBe(1);
  });

  it('rates short and simple passwords Weak', () => {
    expect(strength('abc')).toBe(1);
    expect(strength('hello')).toBe(1);
  });

  it('grows with length and character classes', () => {
    expect(strength('tigrisriver')).toBe(2); // 11 × log2(26) ≈ 51.7 bits
    expect(strength('Tigris-River-42')).toBe(4);
    expect(strength('b9#Lw2-qPz7&Rk')).toBe(4);
  });

  it('rates a long passphrase as Strong', () => {
    expect(strength('the quiet blue key on my desk')).toBe(4);
  });

  it('penalises runs and sequences', () => {
    expect(bits('aaaaaaaaaa')).toBeLessThan(bits('qmzrtwxkpv'));
    expect(bits('abcdefghij')).toBeLessThan(bits('qmzrtwxkpv'));
    expect(bits('9876543210')).toBeLessThan(bits('8305179246'));
  });

  it('gives Arabic letters their own pool', () => {
    expect(bits('كلمةسرطويلة')).toBeGreaterThan(bits('abcdefghijk') + 1);
    expect(strength('خزنتي الصغيرة تكتب عني')).toBe(4);
  });

  it('is monotonic in length for random-looking input', () => {
    const s = 'Xk7#pQ2!vM9@wR4$';
    let prev = 0;
    for (let n = 1; n <= s.length; n++) {
      const b = bits(s.slice(0, n));
      expect(b).toBeGreaterThanOrEqual(prev);
      prev = b;
    }
  });
});

// The device flags weak passwords with a C++ copy of this estimate
// (firmware/components/keyra_api/src/health.cpp); these are the values its
// host test pins, so the phone's meter and Password health always agree.
describe('strength matches the device (health_test.cpp)', () => {
  it.each([
    ['password', 1],
    ['PassWord', 1],
    ['abc', 1],
    ['hasan2000', 2],
    ['Hasan2019!', 3],
    ['Hasan2000!', 2],
    ['k7#Qv9!pL2@xW4$z', 4],
    ['مرحبا', 1],
  ] as const)('%s → %i', (pw, level) => {
    expect(strength(pw)).toBe(level);
  });
});
