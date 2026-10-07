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

import { fitEntry } from '../src/lib/limits';
import { normalizeTotp } from '../src/lib/totp';

describe('fitEntry', () => {
  const base = { title: 'GitHub', url: 'https://github.com', username: 'hasan', password: 'pw', totp: '', notes: '', favorite: false };
  it('leaves a normal entry alone', () => {
    const r = fitEntry(base, normalizeTotp);
    expect(r.entry).toEqual(base);
    expect(r.shortened || r.droppedTotp).toBe(false);
  });
  it('cuts long descriptive text', () => {
    const r = fitEntry({ ...base, title: 'ع'.repeat(100), notes: 'x'.repeat(3000) }, normalizeTotp);
    expect(new TextEncoder().encode(r.entry!.title).length).toBeLessThanOrEqual(128);
    expect(r.entry!.notes.length).toBe(2048);
    expect(r.shortened).toBe(true);
  });
  it('never changes a secret: a too-long password skips the entry', () => {
    expect(fitEntry({ ...base, password: 'p'.repeat(257) }, normalizeTotp).entry).toBeNull();
  });
  it('drops an unusable 2FA value but keeps the account', () => {
    const r = fitEntry({ ...base, totp: 'not a key!' }, normalizeTotp);
    expect(r.entry!.totp).toBe('');
    expect(r.droppedTotp).toBe(true);
    expect(fitEntry({ ...base, totp: 'JBSWY3DPEHPK3PXP' }, normalizeTotp).entry!.totp).not.toBe('');
  });
});
