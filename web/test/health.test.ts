import { describe, expect, it } from 'vitest';
import { issueCount } from '../src/screens/Health';

describe('issueCount', () => {
  it('counts each flagged account once', () => {
    expect(issueCount({ checked: 9, clock: true, weak: [{ id: 1, level: 1 }], reused: [[1, 2], [3, 4, 5]], old: [{ id: 2, since: 1 }, { id: 6, since: 1 }] })).toBe(6);
    expect(issueCount({ checked: 3, clock: false, weak: [], reused: [], old: [] })).toBe(0);
  });
});

import { looksLikeFirmware, updateError } from '../src/screens/Update';
import { setLang } from '../src/lib/i18n';

describe('firmware update helpers', () => {
  it('accepts only something shaped like an ESP-IDF app image', async () => {
    const img = new Uint8Array(8192);
    img[0] = 0xe9;
    expect(await looksLikeFirmware(new Blob([img]))).toBe(true);
    img[0] = 0x7b; // "{" — a JSON backup picked by mistake
    expect(await looksLikeFirmware(new Blob([img]))).toBe(false);
    expect(await looksLikeFirmware(new Blob([new Uint8Array(100)]))).toBe(false);
    const huge = new Uint8Array(3 * 1024 * 1024 + 1);
    huge[0] = 0xe9;
    expect(await looksLikeFirmware(new Blob([huge]))).toBe(false);
  });
  it('explains each refusal', () => {
    setLang('en');
    expect(updateError('bad_signature')).toMatch(/signed/);
    expect(updateError('downgrade')).toMatch(/older/);
    expect(updateError('something new')).toMatch(/couldn't be written/);
  });
});
