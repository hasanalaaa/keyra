import { describe, expect, it } from 'vitest';
import { clipBytes, passphraseOk } from '../src/lib/limits';

describe('limits', () => {
  it('passphrase is 10-128 characters', () => {
    expect(passphraseOk('a'.repeat(9))).toBe(false);
    expect(passphraseOk('a'.repeat(10))).toBe(true);
    expect(passphraseOk('ع'.repeat(128))).toBe(true);
    expect(passphraseOk('a'.repeat(129))).toBe(false);
  });
  it('names are cut at 32 bytes on a character boundary', () => {
    expect(clipBytes('Keyra')).toBe('Keyra');
    expect(clipBytes('كيرا'.repeat(10))).toBe('كيرا'.repeat(4)); // 2 bytes each → 16 characters
    expect(new TextEncoder().encode(clipBytes('😀'.repeat(20))).length).toBe(32);
  });
});
