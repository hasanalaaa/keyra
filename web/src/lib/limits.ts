// The device's input limits (firmware validate.cpp), checked before sending so
// the user sees why instead of a generic error after the round trip.
const enc = new TextEncoder();

export const PASS_MIN = 10;
export const PASS_MAX = 128; // code points

export const passphraseLength = (s: string): number => Array.from(s).length;
export const passphraseOk = (s: string): boolean => passphraseLength(s) >= PASS_MIN && passphraseLength(s) <= PASS_MAX;

/** Device and Wi-Fi names are limited in UTF-8 bytes (32), not characters. */
export function clipBytes(s: string, max = 32): string {
  let out = '';
  let n = 0;
  for (const ch of s) {
    const b = enc.encode(ch).length;
    if (n + b > max) break;
    out += ch;
    n += b;
  }
  return out;
}
