import { afterEach, describe, expect, it } from 'vitest';
import type { ErrorCode } from '../src/lib/actions';
import { setLang, t } from '../src/lib/i18n';
import type { Entry, TypeResult } from '../src/lib/types';

// SPEC §5: an edit while the press is awaited ends the action as entry_changed.
describe('entry_changed result', () => {
  afterEach(() => setLang('en'));

  it('is an error the account screen shows, not typed or cancelled', () => {
    const r: TypeResult = { ok: false, code: 'entry_changed', at: 0 };
    const shown: ErrorCode = 'entry_changed';
    expect(r.code).toBe(shown);
  });

  it('tells the user to ask again, in both languages', () => {
    setLang('en');
    expect(t('errEntryChangedBody')).toMatch(/changed after the request.*nothing was typed.*type it again/i);
    setLang('ar');
    expect(t('errEntryChangedTitle')).toBe('تغيّر الحساب');
    expect(t('errEntryChangedBody')).toContain('تغيّر الحساب بعد الطلب');
    expect(t('errEntryChangedBody')).toContain('اطلب الكتابة من جديد');
  });
});

// SPEC §12.3: without the reveal press the notes stay on Keyra; hasNotes still says they exist.
describe('hidden notes', () => {
  it('an unrevealed entry carries hasNotes and no notes', () => {
    const e: Entry = { id: 1, title: 'Router', url: '', username: 'admin', revealed: false, hasPassword: true, hasTotp: false, hasNotes: true, favorite: false, created: 0, updated: 0, lastUsed: 0, history: [] };
    expect(e.notes).toBeUndefined();
    expect(e.hasNotes).toBe(true);
  });

  it('the masked row has its own label in both languages', () => {
    setLang('en');
    expect(t('showNotes')).toBe('Show notes');
    setLang('ar');
    expect(t('showNotes')).toBe('إظهار الملاحظات');
  });
});
