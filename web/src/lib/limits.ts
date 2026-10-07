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

/** Per-field limits of a vault entry, in UTF-8 bytes (keyra/vault.hpp kMax*). */
export const ENTRY_MAX = { title: 128, url: 512, username: 256, password: 256, totp: 512, notes: 2048 } as const;

export const bytes = (s: string): number => enc.encode(s).length;

export interface FitResult<T> {
  entry: T | null; // null: cannot be imported without changing a secret
  shortened: boolean; // title, URL or notes were cut to fit
  droppedTotp: boolean; // the 2FA value was not a usable key
}

/**
 * Makes an imported entry fit what the device stores. Text that is only
 * descriptive (title, URL, notes) is cut; a username or password is never
 * changed, so one that is too long skips the entry; an unusable 2FA value is
 * dropped (the account still imports).
 */
export function fitEntry<T extends { title: string; url: string; username: string; password: string; totp: string; notes: string }>(
  e: T,
  normalizeTotp: (s: string) => string | null,
): FitResult<T> {
  if (bytes(e.username) > ENTRY_MAX.username || bytes(e.password) > ENTRY_MAX.password)
    return { entry: null, shortened: false, droppedTotp: false };
  const totp = normalizeTotp(e.totp);
  const droppedTotp = e.totp.trim() !== '' && (totp === null || bytes(totp) > ENTRY_MAX.totp);
  const out = {
    ...e,
    title: clipBytes(e.title, ENTRY_MAX.title),
    url: clipBytes(e.url, ENTRY_MAX.url),
    notes: clipBytes(e.notes, ENTRY_MAX.notes),
    totp: droppedTotp ? '' : (totp ?? ''),
  };
  const shortened = out.title !== e.title || out.url !== e.url || out.notes !== e.notes;
  return { entry: out, shortened, droppedTotp };
}
