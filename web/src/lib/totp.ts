// Validates what users paste into the 2FA field: a base32 setup key (spaces tolerated) or an otpauth:// URI.

const BASE32 = /^[A-Z2-7]+=*$/;

export function normalizeTotp(input: string): string | null {
  const s = input.trim();
  if (!s) return '';
  if (/^otpauth:\/\//i.test(s)) {
    try {
      const secret = new URL(s).searchParams.get('secret') ?? '';
      return BASE32.test(secret.toUpperCase().replace(/\s+/g, '')) ? s : null;
    } catch {
      return null;
    }
  }
  const key = s.replace(/[\s-]+/g, '').toUpperCase();
  return key.length >= 16 && BASE32.test(key) ? key : null;
}
