import { describe, expect, it } from 'vitest';
import { HAND_OVER_MS, dropHandOver, handOver, takeOver } from '../src/lib/revealed';
import { keepRevealed } from '../src/screens/Health';
import { errorText } from '../src/lib/errors';
import { ApiError } from '../src/lib/api';
import { setLang, t } from '../src/lib/i18n';
import type { Entry, Health } from '../src/lib/types';

const entry = (id: number, revealed = true): Entry =>
  ({ id, title: 'Mail', url: '', username: 'me', password: revealed ? 'pw' : undefined, revealed, favorite: false, history: [] }) as unknown as Entry;

// SPEC §12.5b: one press reveals one account once; "show, then edit" reuses what the sheet holds.
describe('reveal hand-over to the editor', () => {
  it('gives the revealed entry to the editor of that account, once', () => {
    handOver(entry(5), 1000);
    expect(takeOver(5, 1001)?.password).toBe('pw');
    expect(takeOver(5, 1002)).toBeNull();
  });

  it('never to another account, after the time, after lock, or when nothing was revealed', () => {
    handOver(entry(5), 1000);
    expect(takeOver(6, 1001)).toBeNull();
    expect(takeOver(5, 1002)).toBeNull(); // the miss used it up
    handOver(entry(5), 1000);
    expect(takeOver(5, 1000 + HAND_OVER_MS)).toBeNull();
    handOver(entry(5), 1000);
    dropHandOver();
    expect(takeOver(5, 1001)).toBeNull();
    handOver(entry(5, false), 1000);
    expect(takeOver(5, 1001)).toBeNull();
  });
});

describe('health after a rotate toggle', () => {
  const shown: Health = { checked: 3, clock: true, revealed: true, weak: [{ id: 1, level: 1 }], reused: [[2, 3]], old: [] };
  const plain: Health = { checked: 3, clock: true, revealed: false, weak: [], reused: [], old: [{ id: 4, since: 1 }], rotate: { since: 9, pending: [4] } };

  it('keeps weak and reused that one press already showed', () => {
    const h = keepRevealed(shown, plain);
    expect(h.revealed).toBe(true);
    expect(h.weak).toEqual(shown.weak);
    expect(h.reused).toEqual(shown.reused);
    expect(h.rotate).toEqual(plain.rotate);
  });

  it('takes the new answer otherwise', () => {
    expect(keepRevealed(null, plain)).toBe(plain);
    expect(keepRevealed(plain, shown)).toBe(shown);
  });
});

describe('reveal of an edited account', () => {
  it('says the account changed', () => {
    setLang('en');
    expect(errorText(new ApiError(409, 'entry_changed', 'x'))).toBe(t('revealChanged'));
  });
});
