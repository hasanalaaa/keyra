import { describe, expect, it } from 'vitest';
import { searchActions } from '../src/lib/actionIndex';
import { setLang } from '../src/lib/i18n';

describe('searchActions', () => {
  it('offers the Backup screen for "backup", in either UI language', () => {
    for (const lang of ['en', 'ar'] as const) {
      setLang(lang);
      const paths = searchActions('backup').map((a) => a.path);
      expect(paths[0]).toBe('/backup');
      expect(paths).toContain('/restore');
    }
  });

  it('matches Arabic labels and ignores tashkeel', () => {
    expect(searchActions('النسخ').map((a) => a.path)[0]).toBe('/backup');
    expect(searchActions('استعادة').map((a) => a.path)).toContain('/restore');
    expect(searchActions('بلوتوث').map((a) => a.path)).toContain('/device/pair');
  });

  it('matches word starts only, and needs two characters', () => {
    expect(searchActions('b')).toEqual([]);
    expect(searchActions('ckup')).toEqual([]);
    expect(searchActions('firm').map((a) => a.path)).toEqual(['/device/update']);
  });

  it('caps the list', () => {
    expect(searchActions('a', 4).length).toBe(0);
    expect(searchActions('pa', 2).length).toBeLessThanOrEqual(2);
  });
});
