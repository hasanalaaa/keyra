import { describe, expect, it } from 'vitest';
import { presenceSettled } from '../src/lib/actions';
import type { Presence } from '../src/lib/types';

const armedAt = 10_000;
const idle: Presence = { awaiting: false, op: null, expiresIn: 0, result: null };

describe('presenceSettled (leaving a sheet withdraws only what still waits)', () => {
  it('is settled when the op finished after it was armed', () => {
    const p: Presence = { ...idle, result: { op: 'factory_reset', ok: true, code: 'done', at: 500 } };
    expect(presenceSettled(p, 12_000, 'factory_reset', armedAt)).toBe(true);
  });

  it('is not settled while the device still waits for this op', () => {
    const p: Presence = { awaiting: true, op: 'factory_reset', expiresIn: 50_000, result: { op: 'factory_reset', ok: false, code: 'cancelled', at: 100 } };
    expect(presenceSettled(p, 12_000, 'factory_reset', armedAt)).toBe(false);
  });

  it('ignores an outcome from before it was armed, another op, or a poll older than the arm', () => {
    const old: Presence = { ...idle, result: { op: 'factory_reset', ok: true, code: 'done', at: 5_000 } };
    expect(presenceSettled(old, 12_000, 'factory_reset', armedAt)).toBe(false);
    const other: Presence = { ...idle, result: { op: 'restore', ok: true, code: 'done', at: 100 } };
    expect(presenceSettled(other, 12_000, 'factory_reset', armedAt)).toBe(false);
    const p: Presence = { ...idle, result: { op: 'factory_reset', ok: true, code: 'done', at: 0 } };
    expect(presenceSettled(p, 9_000, 'factory_reset', armedAt)).toBe(false);
    expect(presenceSettled(undefined, 12_000, 'factory_reset', armedAt)).toBe(false);
  });
});
