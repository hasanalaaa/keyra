import { describe, expect, it } from 'vitest';
import { ar, en } from '../src/strings';
import { monogramLetter } from '../src/ui/dom';

const vars = (s: string) => [...s.matchAll(/\{(\w+)\}/g)].map((m) => m[1]).sort();

describe('strings', () => {
  it('Arabic has every English key with the same placeholders', () => {
    expect(Object.keys(ar).sort()).toEqual(Object.keys(en).sort());
    for (const k of Object.keys(en) as (keyof typeof en)[]) expect([k, vars(ar[k])]).toEqual([k, vars(en[k])]);
  });

  it('keys are valid chrome.i18n message names and fit the store limits', () => {
    for (const k of Object.keys(en)) expect(k).toMatch(/^[A-Za-z0-9_]+$/);
    expect(en.extName.length).toBeLessThanOrEqual(45);
    expect(en.extDescription.length).toBeLessThanOrEqual(132);
    expect(ar.extDescription.length).toBeLessThanOrEqual(132);
  });

  it('Arabic copy uses Western digits (DESIGN §3.1)', () => {
    for (const v of Object.values(ar)) expect(v).not.toMatch(/[٠-٩]/);
  });
});

describe('monogram', () => {
  it('takes the first letter, skipping the Arabic article', () => {
    expect(monogramLetter('github')).toBe('G');
    expect(monogramLetter('البنك')).toBe('ب');
    expect(monogramLetter('  ')).toBe('');
  });
});
