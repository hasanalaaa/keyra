import { describe, expect, it } from 'vitest';
import { hexDec, sdmOffsets } from '../src/lib/tags';
import { eventText } from '../src/screens/Activity';
import { setLang } from '../src/lib/i18n';

describe('NFC tag SDM offsets', () => {
  it('points at the zeros the tag overwrites', () => {
    const url = 'http://keyra.local/t/123?p=00000000000000000000000000000000&m=0000000000000000';
    const o = sdmOffsets(url)!;
    // The file: 7 bytes before the URL text (which starts after "http://").
    const file = '\0'.repeat(7) + url.slice('http://'.length);
    expect(file.slice(o.picc, o.picc + 32)).toBe('0'.repeat(32));
    expect(file.slice(o.picc - 2, o.picc)).toBe('p=');
    expect(file.slice(o.mac - 3, o.mac)).toBe('&m=');
    expect(file.slice(o.mac)).toBe('0'.repeat(16));
    expect(o).toEqual({ picc: 27, mac: 62 });
  });

  it('grows with the id and refuses anything else', () => {
    expect(sdmOffsets('http://keyra.local/t/4294967295?p=00000000000000000000000000000000&m=0000000000000000')).toEqual({ picc: 34, mac: 69 });
    expect(sdmOffsets('http://keyra.local/t/12/abcdefghijklmnopqrstuvwx')).toBeNull();
    expect(sdmOffsets('https://keyra.local/t/1?p=00000000000000000000000000000000&m=0000000000000000')).toBeNull();
  });

  it('writes offsets as hex and decimal', () => {
    expect(hexDec(27)).toBe('0x1B (27)');
    expect(hexDec(5)).toBe('0x05 (5)');
  });
});

describe('NFC tag activity lines', () => {
  it('names the tag and the account', () => {
    setLang('en');
    expect(eventText({ kind: 'tag_tapped', at: 0, id: 4, title: 'Desk', detail: 2 }, () => 'GitHub')).toBe('Tag “Desk” tapped for GitHub');
    expect(eventText({ kind: 'tag_created', at: 0, detail: 0, title: 'Desk' })).toBe('NFC tag “Desk” created');
    expect(eventText({ kind: 'tag_refused', at: 0, title: 'Desk', detail: 1, n: 3 })).toBe('Tag “Desk” refused: an old tap was replayed ×3');
    setLang('ar');
    expect(eventText({ kind: 'tag_revoked', at: 0, detail: 0, title: 'مكتب' })).toBe('أُلغي وسم NFC «مكتب»');
    setLang('en');
  });
});
