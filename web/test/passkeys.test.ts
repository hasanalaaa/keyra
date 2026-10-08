import { describe, expect, it } from 'vitest';
import { passkeyCount, setLang, t } from '../src/lib/i18n';

describe('passkey count (restore result)', () => {
  it('counts in English', () => {
    setLang('en');
    expect(passkeyCount(1)).toBe('1 passkey');
    expect(passkeyCount(3)).toBe('3 passkeys');
    expect(t('restoreResultPasskeys', { a: 0, u: 12, p: passkeyCount(3) })).toBe('Added 0, updated 12, plus 3 passkeys');
  });
  it('uses the Arabic plural forms', () => {
    setLang('ar');
    expect(passkeyCount(0)).toBe('لا مفاتيح مرور');
    expect(passkeyCount(1)).toBe('مفتاح مرور واحد');
    expect(passkeyCount(2)).toBe('مفتاحا مرور');
    expect(passkeyCount(3)).toBe('3 مفاتيح مرور');
    expect(passkeyCount(10)).toBe('10 مفاتيح مرور');
    expect(passkeyCount(11)).toBe('11 مفتاح مرور');
    expect(passkeyCount(50)).toBe('50 مفتاح مرور');
    setLang('en');
  });
});
