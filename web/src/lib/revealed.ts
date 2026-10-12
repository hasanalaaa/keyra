// SPEC §12.5b: one press of Keyra's button approves one reveal of one account, used once.
// The account sheet hands what it revealed to the editor it opens, so "show, then edit"
// stays one press; nothing else can pick it up, and it is gone once taken, after
// HAND_OVER_MS, or on lock.
import type { Entry } from './types';

export const HAND_OVER_MS = 30000;

let kept: { entry: Entry; at: number } | null = null;

export function handOver(entry: Entry, now = Date.now()): void {
  kept = entry.revealed ? { entry, at: now } : null;
}

/** The revealed entry `id`, once, when it was handed over less than HAND_OVER_MS ago. */
export function takeOver(id: number, now = Date.now()): Entry | null {
  const k = kept;
  kept = null;
  return k && k.entry.id === id && now - k.at < HAND_OVER_MS ? k.entry : null;
}

export function dropHandOver(): void {
  kept = null;
}
