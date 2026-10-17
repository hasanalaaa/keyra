import { describe, expect, it } from 'vitest';
import { passkeyCount, setLang, t } from '../src/lib/i18n';

describe('passkey count (restore result)', () => {
  it('counts in English', () => {
    setLang('en');
    expect(passkeyCount(1)).toBe('1 passkey');
    expect(passkeyCount(3)).toBe('3 passkeys');
    // After a merge or a replace (both on the press), what Keyra holds now.
    expect(t('restoreReplacedPasskeys', { c: '12 accounts', p: passkeyCount(3) })).toBe('Restored: 12 accounts. Keyra now has 3 passkeys.');
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
