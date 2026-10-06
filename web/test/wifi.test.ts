import { describe, expect, it } from 'vitest';
import { shortDate, signalBars, validHomePassword } from '../src/lib/wifi';

describe('signalBars', () => {
  it('maps RSSI to 1–4 bars', () => {
    expect(signalBars(-40)).toBe(4);
    expect(signalBars(-55)).toBe(4);
    expect(signalBars(-56)).toBe(3);
    expect(signalBars(-67)).toBe(3);
    expect(signalBars(-70)).toBe(2);
    expect(signalBars(-79)).toBe(1);
    expect(signalBars(-100)).toBe(1);
  });
});

describe('validHomePassword', () => {
  it('follows the WPA passphrase rule, including the factory Keyra password', () => {
    expect(validHomePassword('1234567')).toBe(false);
    expect(validHomePassword('12345678')).toBe(true);
    expect(validHomePassword('keyra1234')).toBe(true);
    expect(validHomePassword('a'.repeat(63))).toBe(true);
    expect(validHomePassword('a'.repeat(64))).toBe(false);
    expect(validHomePassword('café au lait')).toBe(false);
  });
});

describe('shortDate', () => {
  it('shows a dash for unknown times and Western digits in Arabic', () => {
    expect(shortDate(0, 'en')).toBe('—');
    expect(shortDate(1790000000, 'ar')).toMatch(/2026/);
    expect(shortDate(1790000000, 'en')).toMatch(/2026/);
  });
});
