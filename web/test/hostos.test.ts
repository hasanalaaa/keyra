import { beforeEach, describe, expect, it } from 'vitest';
import { guessOs, osOf, otherLang, resolveTarget, setOtherLang, switchesLang, wantsSwitch } from '../src/lib/hostos';
import type { BleInfo } from '../src/lib/types';

// Node has no localStorage; the helpers only need get/set/remove.
const mem = new Map<string, string>();
globalThis.localStorage = {
  getItem: (k: string) => mem.get(k) ?? null,
  setItem: (k: string, v: string) => void mem.set(k, v),
  removeItem: (k: string) => void mem.delete(k),
  clear: () => mem.clear(),
  key: () => null,
  length: 0,
} as Storage;

const ble: BleInfo = {
  enabled: true,
  pairing: { active: false, expiresIn: 0 },
  connected: null,
  bonds: [
    { addr: 'AA:AA:AA:AA:AA:AA', name: "Hasan's iPhone", lastSeen: 20, os: 'ios' },
    { addr: 'BB:BB:BB:BB:BB:BB', name: 'DESKTOP-7F3', lastSeen: 10, os: '' },
  ],
};

describe('guessOs', () => {
  it('reads common host names', () => {
    expect(guessOs("Hasan's iPhone")).toBe('ios');
    expect(guessOs('iPad Pro')).toBe('ios');
    expect(guessOs('MacBook Air')).toBe('mac');
    expect(guessOs('Galaxy S24')).toBe('android');
    expect(guessOs('DESKTOP-7F3KQ')).toBe('windows');
  });
  it('stays unknown rather than guess wrong', () => {
    expect(guessOs('')).toBe('');
    expect(guessOs('Machine room')).toBe('');
    expect(guessOs('Keyboard')).toBe('');
  });
});

describe('osOf / resolveTarget', () => {
  it('uses the USB setting for usb', () => expect(osOf('usb', 'mac', ble)).toBe('mac'));
  it('uses the bond for an address', () => {
    expect(osOf('AA:AA:AA:AA:AA:AA', '', ble)).toBe('ios');
    expect(osOf('BB:BB:BB:BB:BB:BB', '', ble)).toBe('');
    expect(osOf('CC:CC:CC:CC:CC:CC', '', ble)).toBe('');
  });
  it('falls back to the device choice', () => {
    expect(resolveTarget(undefined, 'ble', ble)).toBe('AA:AA:AA:AA:AA:AA');
    expect(resolveTarget('usb', 'ble', ble)).toBe('usb');
    expect(resolveTarget(null, null, ble)).toBe(null);
  });
});

describe('language switch', () => {
  beforeEach(() => localStorage.clear());
  it('only on Apple systems', () => {
    expect(switchesLang('mac') && switchesLang('ios')).toBe(true);
    expect(switchesLang('windows') || switchesLang('android') || switchesLang('')).toBe(false);
  });
  it('is remembered per computer', () => {
    expect(otherLang('usb')).toBe(false);
    setOtherLang('usb', true);
    expect(otherLang('usb')).toBe(true);
    expect(otherLang('AA:AA:AA:AA:AA:AA')).toBe(false);
    expect(wantsSwitch('usb', 'mac')).toBe(true);
    expect(wantsSwitch('usb', 'windows')).toBe(false); // Windows uses Alt codes instead
    setOtherLang('usb', false);
    expect(wantsSwitch('usb', 'mac')).toBe(false);
    expect(otherLang(null)).toBe(false);
  });
});
