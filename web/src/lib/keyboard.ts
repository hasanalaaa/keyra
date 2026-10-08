// Keyboard layouts (SPEC §10.1) and the Layout Doctor (SPEC §10.3): the probe types fixed key
// positions, and what appears on the computer names its layout. Pure helpers, unit-tested in
// test/keyboard.test.ts.
import type { HostOs, KeyboardLayout } from './types';

/** How many characters a reading may be off and still suggest a layout (a typo, a missed key). */
const MAX_TYPOS = 2;

/** Ignores what does not tell layouts apart: the Unicode form and spaces around or doubled. */
export const normalizeProbe = (s: string): string => s.normalize('NFC').trim().replace(/\s+/g, ' ');

/** Layouts a computer with this system can have; 'any' layouts (US, Dvorak, Colemak) fit every one. */
export function fitsOs(l: KeyboardLayout, os: HostOs): boolean {
  if (l.platform === 'any' || os === '') return true;
  return os === 'mac' || os === 'ios' ? l.platform === 'mac' : l.platform === 'windows';
}

/** Edit distance over code points (Arabic letters and £ count as one character). */
export function typos(a: string, b: string): number {
  const x = [...a];
  const y = [...b];
  let prev = Array.from({ length: y.length + 1 }, (_, j) => j);
  for (let i = 1; i <= x.length; i++) {
    const cur = [i];
    for (let j = 1; j <= y.length; j++) cur.push(Math.min(prev[j] + 1, cur[j - 1] + 1, prev[j - 1] + (x[i - 1] === y[j - 1] ? 0 : 1)));
    prev = cur;
  }
  return prev[y.length];
}

export interface ProbeMatch {
  /** true: what appeared is exactly these layouts' probe. */
  exact: boolean;
  /** Best first; empty when nothing is close. A Windows and a Mac layout can share a probe. */
  layouts: KeyboardLayout[];
}

/**
 * The layouts whose probe is what the user saw, else the closest within a couple of typos.
 * When several tie, the ones that fit the computer's system win (all of them if none fits).
 * null: nothing entered yet.
 */
export function matchProbe(seen: string, layouts: readonly KeyboardLayout[], os: HostOs = ''): ProbeMatch | null {
  const s = normalizeProbe(seen);
  if (!s) return null;
  let best = MAX_TYPOS;
  let hits: KeyboardLayout[] = [];
  for (const l of layouts) {
    const d = typos(s, normalizeProbe(l.probe));
    if (d > MAX_TYPOS) continue;
    if (d < best || !hits.length) [best, hits] = [d, [l]];
    else if (d === best) hits.push(l);
  }
  const fitting = hits.filter((l) => fitsOs(l, os));
  return { exact: best === 0, layouts: fitting.length ? fitting : hits };
}

/** The list for comparing by eye: layouts that fit the system first, otherwise the device's order. */
export function byFit(layouts: readonly KeyboardLayout[], os: HostOs): KeyboardLayout[] {
  return [...layouts].sort((a, b) => Number(fitsOs(b, os)) - Number(fitsOs(a, os)));
}
