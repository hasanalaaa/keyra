import { describe, expect, it } from 'vitest';
import { parseRoute, tabOf } from '../src/lib/router';

describe('tab routes', () => {
  it('parses the tabs and their sub-screens', () => {
    expect(parseRoute('#/')).toEqual({ name: 'list' });
    expect(parseRoute('#/typing')).toEqual({ name: 'tab', tab: 'typing', sub: null });
    expect(parseRoute('#/security/health')).toEqual({ name: 'tab', tab: 'security', sub: 'health' });
    expect(parseRoute('#/device/layout-usb')).toEqual({ name: 'tab', tab: 'device', sub: 'layout-usb' });
    expect(parseRoute('#/restore')).toEqual({ name: 'restore' });
  });

  it('the old Settings route is gone (falls back to the list)', () => {
    expect(parseRoute('#/settings')).toEqual({ name: 'list' });
  });

  it('puts each sheet in its tab', () => {
    expect(tabOf(parseRoute('#/a/3'))).toBe('accounts');
    expect(tabOf(parseRoute('#/new'))).toBe('accounts');
    expect(tabOf(parseRoute('#/generate'))).toBe('typing');
    expect(tabOf(parseRoute('#/type'))).toBe('typing');
    expect(tabOf(parseRoute('#/backup'))).toBe('device');
    expect(tabOf(parseRoute('#/restore'))).toBe('device');
    expect(tabOf(parseRoute('#/import'))).toBe('device');
    expect(tabOf(parseRoute('#/security/passkeys'))).toBe('security');
  });
});
