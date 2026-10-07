import { describe, expect, it } from 'vitest';
import { combineShares, formatKey, parseKey, parseShare, splitKey, toHex, fromHex, KEY_BYTES } from '../src/lib/recovery';

const key = Uint8Array.from({ length: KEY_BYTES }, (_, i) => (i * 37 + 11) & 0xff);

function parsed(texts: string[]) {
  return texts.map((t) => {
    const p = parseShare(t);
    if (typeof p === 'string') throw new Error(p);
    return p;
  });
}

describe('recovery key text', () => {
  it('round-trips through the printed form', () => {
    const text = formatKey(key);
    expect(text).toMatch(/^([0-9A-HJKMNP-TV-Z]{4}-){8}[0-9A-HJKMNP-TV-Z]{4}$/);
    expect(parseKey(text)).toEqual(key);
    // Lowercase, spaces instead of dashes, O/I/L look-alikes are all read back.
    const sloppy = text.toLowerCase().replace(/-/g, ' ').replace(/0/g, 'o').replace(/1/g, 'l');
    expect(parseKey(sloppy)).toEqual(key);
  });
  it('catches typos and wrong lengths', () => {
    const text = formatKey(key);
    const i = text.search(/[2-9A-H]/);
    const typo = text.slice(0, i) + (text[i] === '2' ? '3' : '2') + text.slice(i + 1);
    expect(parseKey(typo)).toBe('check');
    expect(parseKey(text.slice(0, -1))).toBe('format');
    expect(parseKey(text.replace(/^./, 'U'))).toBe('format');
  });
  it('hex helpers round-trip', () => {
    expect(toHex(key)).toHaveLength(40);
    expect(fromHex(toHex(key))).toEqual(key);
  });
});

describe('Shamir shares', () => {
  it('any k of n rebuild the key', async () => {
    const shares = await splitKey(key, 5, 3);
    expect(shares).toHaveLength(5);
    expect(new Set(shares).size).toBe(5);
    for (const pick of [[0, 1, 2], [4, 2, 0], [1, 3, 4], [0, 1, 2, 3, 4]]) {
      expect(await combineShares(parsed(pick.map((i) => shares[i])))).toEqual(key);
    }
  });
  it('k-1 shares, duplicates or shares of another key do not', async () => {
    const shares = await splitKey(key, 3, 2);
    expect(await combineShares(parsed([shares[1]]))).toBe('too_few');
    expect(await combineShares(parsed([shares[1], shares[1]]))).toBe('too_few');
    const other = await splitKey(Uint8Array.from(key, (b) => b ^ 0xff), 3, 2);
    expect(await combineShares(parsed([shares[0], other[1]]))).toBe('mismatch');
    const five = await splitKey(key, 5, 3);
    expect(await combineShares(parsed([shares[0], five[1], five[2]]))).toBe('mismatch');
  });
  it('different shares with the same x coordinate are a mismatch, not too few', async () => {
    const [a] = parsed((await splitKey(key, 3, 2)).slice(0, 1));
    const [b] = parsed((await splitKey(Uint8Array.from(key, (v) => v ^ 0xff), 3, 2)).slice(0, 1));
    const sameX = { ...b, bytes: Uint8Array.from(b.bytes) };
    sameX.bytes[sameX.bytes.length - 1] = a.bytes[a.bytes.length - 1];
    expect(await combineShares([a, sameX])).toBe('mismatch');
  });
  it('a damaged share is detected', async () => {
    const [s] = await splitKey(key, 3, 2);
    const i = s.search(/[2-9A-H]/);
    expect(parseShare(s.slice(0, i) + (s[i] === '2' ? '3' : '2') + s.slice(i + 1))).toBe('check');
    expect(parseShare(s.slice(4))).toBe('format');
    expect(parseShare(formatKey(key))).toBe('format'); // a key is not a share
  });
  it('refuses nonsense settings', async () => {
    await expect(splitKey(key, 3, 1)).rejects.toThrow();
    await expect(splitKey(key, 2, 3)).rejects.toThrow();
    await expect(splitKey(key.slice(1), 3, 2)).rejects.toThrow();
  });
});
