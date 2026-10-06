// Small pure helpers for the home Wi‑Fi screens (SPEC §8.2).

/** 1–4 bars, the way phones draw Wi‑Fi strength. */
export function signalBars(rssi: number): 1 | 2 | 3 | 4 {
  if (rssi >= -55) return 4;
  if (rssi >= -67) return 3;
  if (rssi >= -78) return 2;
  return 1;
}

/** Same rule as the device: a WPA2/WPA3 passphrase is 8–63 printable ASCII characters. */
export const validHomePassword = (s: string): boolean => s.length >= 8 && s.length <= 63 && /^[\x20-\x7e]+$/.test(s);

/** Unix seconds → short date with Western digits (DESIGN §3.1); 0 = unknown. */
export function shortDate(unixSec: number, lang: 'ar' | 'en'): string {
  if (!unixSec) return '—';
  return new Date(unixSec * 1000).toLocaleDateString(lang === 'ar' ? 'ar-u-nu-latn' : 'en', { day: 'numeric', month: 'short', year: 'numeric' });
}
