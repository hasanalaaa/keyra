// Deterministic monogram letter + colour (DESIGN §4.3).

export const PALETTE = [
  '#8C4A2F', '#7D5A1C', '#5B6A2C', '#2F6A4E', '#2D6366', '#4A5160',
  '#5F4C7C', '#7C3D5E', '#8C3A35', '#45503F', '#6E5542', '#3A3B40',
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
