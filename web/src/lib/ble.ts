// Small pure helpers for the Bluetooth screens (unit-tested in test/ble.test.ts).
import type { BleBond, BlePeer, BleInfo } from './types';

/** A device's own name, or the fallback while Keyra hasn't read it yet. */
export const deviceLabel = (p: Pick<BlePeer, 'name'> | null | undefined, fallback: string): string =>
  p && p.name.trim() ? p.name.trim() : fallback;

/** The bond that appeared since `before` (addresses), i.e. the device that just paired. */
export function newBond(before: readonly string[], after: readonly BleBond[]): BleBond | null {
  return after.find((b) => !before.includes(b.addr)) ?? null;
}

/** Most recently seen first; never-seen devices last, in the device's order. */
export function sortBonds(bonds: readonly BleBond[], connected: string | null): BleBond[] {
  return [...bonds].sort((a, b) => Number(b.addr === connected) - Number(a.addr === connected) || b.lastSeen - a.lastSeen);
}

const TARGET_KEY = 'keyra.target';

/** This browser's last choice in the account sheet's "Type into" picker ("usb" or an address). */
export function storedTarget(): string | null {
  try {
    return localStorage.getItem(TARGET_KEY);
  } catch {
    return null;
  }
}

export function storeTarget(v: string): void {
  try {
    localStorage.setItem(TARGET_KEY, v);
  } catch {
    // Private mode: the choice just isn't remembered.
  }
}

/** A remembered choice that still exists (USB always does; a device must still be paired). */
export function validTarget(stored: string | null, ble: BleInfo | null): string | null {
  if (stored === 'usb') return stored;
  if (!stored || !ble?.enabled) return null;
  return ble.bonds.some((b) => b.addr === stored) ? stored : null;
}

/** What the device picks by itself (SPEC §8.1): the picker highlights it until the user chooses. */
export function defaultTarget(output: 'usb' | 'ble' | null, ble: BleInfo | null): string | null {
  if (output === 'usb') return 'usb';
  if (output !== 'ble' || !ble) return null;
  return sortBonds(ble.bonds, ble.connected?.addr ?? null)[0]?.addr ?? null;
}
