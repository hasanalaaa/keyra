// Validates what users paste into the 2FA field: a base32 setup key (spaces tolerated) or an otpauth:// URI.
import { parseOtpauth } from './qrImport';

const BASE32 = /^[A-Z2-7]+=*$/;

export function normalizeTotp(input: string): string | null {
  const s = input.trim();
  if (!s) return '';
  if (/^otpauth:\/\//i.test(s)) {
    // Same rules as the firmware: HOTP and unsupported algorithm/digits/period are rejected here, not at code time.
    return parseOtpauth(s).ok ? s : null;
  }
  const key = s.replace(/[\s-]+/g, '').toUpperCase();
  return key.length >= 16 && BASE32.test(key) ? key : null;
}
