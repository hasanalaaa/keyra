// Small pure helpers for the Bluetooth screens (unit-tested in test/ble.test.ts).
import type { BleBond, BlePeer } from './types';

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
