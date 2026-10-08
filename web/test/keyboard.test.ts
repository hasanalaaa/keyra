import { describe, expect, it } from 'vitest';
import { byFit, fitsOs, matchProbe, normalizeProbe, typos } from '../src/lib/keyboard';
import type { KeyboardLayout } from '../src/lib/types';

// Probe strings as GET /api/keyboard sends them (firmware keyra_hid probeText(), layouts.txt).
const L = (id: string, platform: KeyboardLayout['platform'], probe: string): KeyboardLayout => ({ id, name: id, platform, experimental: id !== 'us', probe });
const LAYOUTS = [
  L('us', 'any', 'qwyz ;@#/'),
  L('uk', 'windows', 'qwyz ;"£/'),
  L('uk-mac', 'mac', 'qwyz ;@£/'),
  L('de', 'windows', 'qwzy ö"§-'),
  L('de-mac', 'mac', 'qwzy ö"§-'),
  L('fr', 'windows', 'azyw m23!'),
  L('fr-mac', 'mac', 'azyw m23='),
  L('ar', 'windows', 'ضصغئ ك@#ظ'),
  L('ar-mac', 'mac', 'ضصغظ ك@#/'),
  L('ar-pc-mac', 'mac', 'ضصغئ ك@#ظ'),
];
const ids = (m: ReturnType<typeof matchProbe>) => m?.layouts.map((l) => l.id);

describe('Layout Doctor (SPEC §10.3)', () => {
  it('names the layout whose probe appeared', () => {
    expect(matchProbe('qwyz ;@#/', LAYOUTS)).toEqual({ exact: true, layouts: [LAYOUTS[0]] });
    expect(ids(matchProbe('azyw m23!', LAYOUTS))).toEqual(['fr']);
    expect(ids(matchProbe('ضصغظ ك@#/', LAYOUTS))).toEqual(['ar-mac']);
  });

  it('ignores spaces around or doubled, and the Unicode form', () => {
    expect(ids(matchProbe('  qwyz   ;@#/\n', LAYOUTS))).toEqual(['us']);
    expect(normalizeProbe('qwzy ö"§-')).toBe('qwzy ö"§-'); // ö typed as o + combining diaeresis
    expect(matchProbe('qwzy ö"§-', LAYOUTS, 'windows')).toEqual({ exact: true, layouts: [LAYOUTS[3]] });
  });

  it('lets the computer’s system decide between a Windows and a Mac layout with the same probe', () => {
    expect(ids(matchProbe('qwzy ö"§-', LAYOUTS))).toEqual(['de', 'de-mac']);
    expect(ids(matchProbe('qwzy ö"§-', LAYOUTS, 'mac'))).toEqual(['de-mac']);
    expect(ids(matchProbe('qwzy ö"§-', LAYOUTS, 'ios'))).toEqual(['de-mac']);
    expect(ids(matchProbe('qwzy ö"§-', LAYOUTS, 'linux'))).toEqual(['de']);
    expect(ids(matchProbe('ضصغئ ك@#ظ', LAYOUTS, 'mac'))).toEqual(['ar-pc-mac']);
    // A system no tied layout fits: show them all rather than nothing.
    expect(ids(matchProbe('azyw m23!', LAYOUTS, 'mac'))).toEqual(['fr']);
  });

  it('suggests the closest layouts for a typo, never a far one', () => {
    expect(matchProbe('qwyz ;@#', LAYOUTS)).toEqual({ exact: false, layouts: [LAYOUTS[0]] }); // missed the last key
    expect(matchProbe('qwyz ;@£/', LAYOUTS, 'windows')).toEqual({ exact: true, layouts: [LAYOUTS[2]] });
    expect(matchProbe('hello world', LAYOUTS)).toEqual({ exact: false, layouts: [] });
    expect(matchProbe('', LAYOUTS)).toBeNull();
    expect(matchProbe('   ', LAYOUTS)).toBeNull();
    expect(matchProbe('qwyz ;@#/', [])).toEqual({ exact: false, layouts: [] });
  });

  it('counts edits per character, Arabic letters and £ included', () => {
    expect(typos('', '')).toBe(0);
    expect(typos('abc', '')).toBe(3);
    expect(typos('£', '#')).toBe(1);
    expect(typos('ضصغئ', 'ضصغظ')).toBe(1);
    expect(typos('qwyz', 'qwzy')).toBe(2);
  });

  it('lists the layouts that fit the system first, otherwise in the device’s order', () => {
    expect(byFit(LAYOUTS, 'mac').map((l) => l.id)).toEqual(['us', 'uk-mac', 'de-mac', 'fr-mac', 'ar-mac', 'ar-pc-mac', 'uk', 'de', 'fr', 'ar']);
    expect(byFit(LAYOUTS, '').map((l) => l.id)).toEqual(LAYOUTS.map((l) => l.id));
    expect(fitsOs(LAYOUTS[0], 'windows') && fitsOs(LAYOUTS[0], 'mac')).toBe(true);
    expect(fitsOs(LAYOUTS[3], 'android')).toBe(true);
    expect(fitsOs(LAYOUTS[4], 'android')).toBe(false);
  });
});

import { layoutName } from '../src/lib/keyboard';
import { setLang } from '../src/lib/i18n';

describe('layout names', () => {
  it('shows Arabic names in the Arabic UI and the firmware name otherwise', () => {
    const de = { id: 'de', name: 'German', platform: 'windows', experimental: true, probe: '' } as const;
    const dv = { id: 'dvorak', name: 'Dvorak', platform: 'any', experimental: true, probe: '' } as const;
    setLang('ar');
    expect(layoutName(de)).toBe('الألمانية');
    expect(layoutName(dv)).toBe('Dvorak');
    setLang('en');
    expect(layoutName(de)).toBe('German');
  });
});
