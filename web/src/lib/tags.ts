// NFC tap tags (SPEC §18): where an NTAG 424 DNA mirrors its SUN data in the NDEF file.

/**
 * SDM offsets for a secure tag's URL template ("http://keyra.local/t/<id>?p=<32 zeros>&m=<16 zeros>"),
 * written as one NDEF URI record with the "http://" prefix code: the file holds NLEN (2) · header (1) ·
 * type length (1) · payload length (1) · "U" (1) · prefix code (1), then the URL without "http://".
 * The MAC covers nothing (input offset = MAC offset), as Keyra checks it. Null for anything else.
 */
export function sdmOffsets(url: string): { picc: number; mac: number } | null {
  const m = /^http:\/\/(keyra\.local\/t\/\d{1,10}\?p=)0{32}&m=0{16}$/.exec(url);
  if (!m) return null;
  const picc = 7 + m[1].length;
  return { picc, mac: picc + 32 + 3 };
}

/** "0x1A (26)": TagWriter asks for hex, people count in decimal. */
export const hexDec = (n: number) => `0x${n.toString(16).toUpperCase().padStart(2, '0')} (${n})`;
