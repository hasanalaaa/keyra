import { describe, expect, it } from 'vitest';
import { deviceLabel, newBond, sortBonds } from '../src/lib/ble';

const bond = (addr: string, name = '', lastSeen = 0) => ({ addr, name, lastSeen });

describe('deviceLabel', () => {
  it('uses the device name, trimmed', () => expect(deviceLabel({ name: "  Hasan's iPad " }, 'X')).toBe("Hasan's iPad"));
  it('falls back while unnamed', () => {
    expect(deviceLabel({ name: '' }, 'Bluetooth device')).toBe('Bluetooth device');
    expect(deviceLabel(null, 'Bluetooth device')).toBe('Bluetooth device');
  });
});

describe('newBond', () => {
  it('finds the device that just paired', () => {
    expect(newBond(['AA:00:00:00:00:01'], [bond('AA:00:00:00:00:01'), bond('AA:00:00:00:00:02', 'Mac')])?.name).toBe('Mac');
  });
  it('is null when nothing new paired (or one was forgotten)', () => {
    expect(newBond(['AA:00:00:00:00:01'], [bond('AA:00:00:00:00:01')])).toBeNull();
    expect(newBond(['AA:00:00:00:00:01', 'AA:00:00:00:00:02'], [bond('AA:00:00:00:00:02')])).toBeNull();
  });
});

describe('sortBonds', () => {
  it('puts the connected device first, then by last seen', () => {
    const list = [bond('A', 'old', 100), bond('B', 'new', 300), bond('C', 'live', 200), bond('D', 'never', 0)];
    expect(sortBonds(list, 'C').map((b) => b.name)).toEqual(['live', 'new', 'old', 'never']);
    expect(sortBonds(list, null).map((b) => b.name)).toEqual(['new', 'live', 'old', 'never']);
  });
});
