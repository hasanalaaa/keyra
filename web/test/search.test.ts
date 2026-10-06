import { describe, expect, it } from 'vitest';
import { groupByLetter, highlightRange, letterOf, normalize, search } from '../src/lib/search';
import type { EntrySummary } from '../src/lib/types';

let nextId = 1;
const entry = (title: string, username = '', url = ''): EntrySummary => ({
  id: nextId++,
  title,
  url,
  username,
  favorite: false,
  hasPassword: true,
  hasTotp: false,
  updated: 0,
  lastUsed: 0,
});

const vault = [
  entry('GitHub', 'hasanalaaa', 'github.com'),
  entry('Google', 'hasan@gmail.com', 'accounts.google.com'),
  entry('My Git server', 'admin', 'git.home.lan'),
  entry('Legit Shop', 'me', 'legit.example'),
  entry('Work mail', 'gitter@corp.com', 'mail.corp.com'),
  entry('بنك الرافدين', '0771', 'rafidain-bank.gov.iq'),
  entry('آسيا سيل', '0770', 'asiacell.com'),
  entry('مكتبة', 'reader', ''),
];

const titles = (q: string) => search(vault, q).map((m) => m.entry.title);

describe('search ranking', () => {
  it('orders title prefix > word prefix > substring > other fields', () => {
    expect(titles('git')).toEqual(['GitHub', 'My Git server', 'Legit Shop', 'Work mail']);
    expect(search(vault, 'git').map((m) => m.rank)).toEqual([0, 1, 2, 3]);
  });

  it('is case-insensitive and ignores surrounding spaces', () => {
    expect(titles('  GOOG ')).toEqual(['Google']);
  });

  it('matches url host and username', () => {
    expect(titles('rafidain')).toEqual(['بنك الرافدين']);
    expect(titles('gmail')).toEqual(['Google']);
  });

  it('returns nothing for an empty query or no match', () => {
    expect(search(vault, '   ')).toEqual([]);
    expect(search(vault, 'zzz')).toEqual([]);
  });

  it('normalises Arabic: alef forms, taa marbuta, tashkeel, tatweel', () => {
    expect(titles('اسيا')).toEqual(['آسيا سيل']);
    expect(titles('مكتبه')).toEqual(['مكتبة']);
    expect(titles('بَنْك')).toEqual(['بنك الرافدين']);
    expect(titles('الرافـدين')).toEqual(['بنك الرافدين']);
  });

  it('maps Arabic-Indic digits to Western', () => {
    expect(normalize('٠٧٧١')).toBe('0771');
    expect(titles('٠٧٧١')).toEqual(['بنك الرافدين']);
  });
});

describe('highlightRange', () => {
  it('finds the match in the original text', () => {
    expect(highlightRange('My Git server', 'git')).toEqual([3, 6]);
    expect(highlightRange('GitHub', 'nope')).toBeNull();
  });

  it('maps through normalisation back to original positions', () => {
    const text = 'آسيا سيل';
    const r = highlightRange(text, 'اسيا')!;
    expect(text.slice(r[0], r[1])).toBe('آسيا');
  });
});

describe('letters and groups', () => {
  it('skips the Arabic article and upper-cases Latin', () => {
    expect(letterOf('البنك')).toBe('ب');
    expect(letterOf('github')).toBe('G');
    expect(letterOf('1Password')).toBe('#');
    expect(letterOf('أمازون')).toBe('ا');
  });

  it('puts the UI language script first and "#" last', () => {
    const items = [entry('Zain'), entry('1Password'), entry('بنك'), entry('Apple'), entry('ثقة')];
    expect(groupByLetter(items, 'ar').map((g) => g.letter)).toEqual(['ب', 'ث', 'A', 'Z', '#']);
    expect(groupByLetter(items, 'en').map((g) => g.letter)).toEqual(['A', 'Z', 'ب', 'ث', '#']);
  });
});
