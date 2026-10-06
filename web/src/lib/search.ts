// Instant client-side search and A–Z grouping (DESIGN §4.4, §5.4).
import { hostOf } from './csv';
import type { EntrySummary } from './types';
import type { Lang } from './i18n';

/** Lower-case; strip tashkeel + tatweel; unify alef forms, ى→ي, ة→ه; Arabic-Indic digits → Western. */
export function normalize(s: string): string {
  return s
    .toLowerCase()
    .replace(/[ً-ْـ]/g, '')
    .replace(/[أإآ]/g, 'ا')
    .replace(/ى/g, 'ي')
    .replace(/ة/g, 'ه')
    .replace(/[٠-٩]/g, (d) => String(d.charCodeAt(0) - 0x660));
}

export interface Match {
  entry: EntrySummary;
  rank: number; // 0 title prefix, 1 word prefix, 2 substring (title), 3 other fields
}

export function search(entries: EntrySummary[], q: string): Match[] {
  const nq = normalize(q.trim());
  if (!nq) return [];
  const out: Match[] = [];
  for (const e of entries) {
    const title = normalize(e.title);
    let rank = -1;
    if (title.startsWith(nq)) rank = 0;
    else if (title.split(/[\s\-_.@]+/).some((w) => w.startsWith(nq))) rank = 1;
    else if (title.includes(nq)) rank = 2;
    else if (normalize(hostOf(e.url)).includes(nq) || normalize(e.username).includes(nq)) rank = 3;
    if (rank >= 0) out.push({ entry: e, rank });
  }
  return out.sort((a, b) => a.rank - b.rank || compareTitles(a.entry.title, b.entry.title));
}

/**
 * Where `q` matches inside `text` (by normalized comparison), as [start, end) in `text`'s UTF-16 units.
 * Normalization only removes or maps single code units, so we walk both strings in step.
 */
export function highlightRange(text: string, q: string): [number, number] | null {
  const nq = normalize(q.trim());
  if (!nq) return null;
  const map: number[] = [];
  let norm = '';
  for (let i = 0; i < text.length; i++) {
    const n = normalize(text[i]);
    for (let k = 0; k < n.length; k++) map.push(i);
    norm += n;
  }
  const at = norm.indexOf(nq);
  if (at < 0) return null;
  return [map[at], map[at + nq.length - 1] + 1];
}

const isArabic = (ch: string) => /[؀-ۿ]/.test(ch);
const isLatin = (ch: string) => /[a-z]/i.test(ch);

/** The section letter for an account: normalized first letter; Arabic "ال" prefix skipped; else "#". */
export function letterOf(title: string): string {
  const chars = Array.from(normalize(title.trim()));
  let i = 0;
  if (chars[0] === 'ا' && chars[1] === 'ل' && chars.length > 2 && isArabic(chars[2])) i = 2;
  const ch = chars[i] ?? '';
  if (isLatin(ch)) return ch.toUpperCase();
  if (isArabic(ch)) return ch;
  return '#';
}

const collator = new Intl.Collator(['ar', 'en'], { sensitivity: 'base', numeric: true });
export const compareTitles = (a: string, b: string): number => collator.compare(normalize(a), normalize(b));

/** Groups for the "All" section, ordered by script: UI language first, then the other, then "#". */
export function groupByLetter(entries: EntrySummary[], lang: Lang): { letter: string; items: EntrySummary[] }[] {
  const map = new Map<string, EntrySummary[]>();
  for (const e of [...entries].sort((a, b) => compareTitles(a.title, b.title))) {
    const l = letterOf(e.title);
    const list = map.get(l);
    if (list) list.push(e);
    else map.set(l, [e]);
  }
  const scriptRank = (l: string) => (l === '#' ? 2 : isArabic(l) === (lang === 'ar') ? 0 : 1);
  return [...map.entries()]
    .sort(([a], [b]) => scriptRank(a) - scriptRank(b) || collator.compare(a, b))
    .map(([letter, items]) => ({ letter, items }));
}
