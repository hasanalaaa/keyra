import { describe, expect, it } from 'vitest';
import { eventText } from '../src/screens/Activity';
import { setLang } from '../src/lib/i18n';

describe('activity event text', () => {
  it('names what happened without any secret', () => {
    setLang('en');
    expect(eventText({ kind: 'typed', at: 1, detail: 0, id: 5, title: 'GitHub' })).toBe('Typed “GitHub”');
    expect(eventText({ kind: 'typed', at: 1, detail: 1, id: 5, title: 'GitHub' })).toBe('Typed “GitHub” · Bluetooth');
    expect(eventText({ kind: 'failed_unlocks', at: 1, detail: 0, n: 3 })).toBe('Wrong passphrase attempts: 3');
    expect(eventText({ kind: 'lock', at: 1, detail: 2 })).toMatch(/unplugged/);
    expect(eventText({ kind: 'lock', at: 1, detail: 9 })).toBe('Locked'); // a reason this app does not know yet
    expect(eventText({ kind: 'unknown', at: 1, detail: 0 })).toBe('Other activity');
  });
  it('speaks Arabic', () => {
    setLang('ar');
    expect(eventText({ kind: 'revealed', at: 1, detail: 0, id: 2, title: 'بنك' })).toBe('أظهر كلمة سر «بنك»');
    setLang('en');
  });
});
