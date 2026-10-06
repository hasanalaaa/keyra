// Deterministic monogram letter + colour (DESIGN §4.3).

export const PALETTE = [
  '#C2410C', '#B45309', '#4D7C0F', '#15803D', '#0F766E', '#0E7490',
  '#1D4ED8', '#6D28D9', '#A21CAF', '#BE185D', '#B91C1C', '#475569',
];

export function fnv1a32(s: string): number {
  let h = 0x811c9dc5;
  for (const b of new TextEncoder().encode(s)) {
    h ^= b;
    h = Math.imul(h, 0x01000193) >>> 0;
  }
  return h >>> 0;
}

export const monogramColor = (title: string): string =>
  PALETTE[fnv1a32(title.trim().toLowerCase().normalize('NFKC')) % PALETTE.length];

/** First letter or digit; skips the Arabic article "ال"; '' means "use the key glyph". */
export function monogramLetter(title: string): string {
  const chars = Array.from(title.trim()).filter((ch) => /[\p{L}\p{N}]/u.test(ch));
  let i = 0;
  if (chars[0] === 'ا' && chars[1] === 'ل' && chars.length > 2 && /\p{Script=Arabic}/u.test(chars[2])) i = 2;
  const ch = chars[i] ?? '';
  return /[a-z]/.test(ch) ? ch.toUpperCase() : ch;
}
