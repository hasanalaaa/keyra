import { describe, expect, it } from 'vitest';
import { batches } from '../src/lib/batch';

describe('import batches', () => {
  it('splits by count', () => {
    const b = batches(Array.from({ length: 120 }, (_, i) => ({ i })), 50, 1e9);
    expect(b.map((x) => x.length)).toEqual([50, 50, 20]);
  });
  it('splits by bytes so long notes stay under the device limit', () => {
    const big = { notes: 'ن'.repeat(1000) }; // 2000 UTF-8 bytes
    const b = batches(Array.from({ length: 50 }, () => big), 50, 48 * 1024);
    expect(b.length).toBeGreaterThan(1);
    for (const x of b) expect(new TextEncoder().encode(JSON.stringify({ entries: x })).length).toBeLessThan(64 * 1024);
  });
  it('keeps order and loses nothing', () => {
    const items = Array.from({ length: 7 }, (_, i) => i);
    expect(batches(items, 3, 1e9).flat()).toEqual(items);
    expect(batches([], 3, 1e9)).toEqual([]);
  });
});
