// Password-export CSV import (DESIGN §5.7): RFC 4180 parsing + source detection, all in the browser.
import type { EntryInput } from './types';

export type Source = 'apple' | 'chrome' | 'bitwarden' | '1password';

/** RFC 4180: quoted fields, "" escapes, CRLF/LF/CR line breaks (also inside quotes), leading BOM. */
export function parseCsv(text: string): string[][] {
  if (text.charCodeAt(0) === 0xfeff) text = text.slice(1);
  const rows: string[][] = [];
  let row: string[] = [];
  let field = '';
  let quoted = false;
  let i = 0;
  const n = text.length;
  const endRow = () => {
    row.push(field);
    rows.push(row);
    row = [];
    field = '';
  };
  while (i < n) {
    const c = text[i];
    if (quoted) {
      if (c === '"') {
        if (text[i + 1] === '"') {
          field += '"';
          i += 2;
          continue;
        }
        quoted = false;
      } else field += c;
      i++;
      continue;
    }
    if (c === '"' && field === '') quoted = true;
    else if (c === ',') {
      row.push(field);
      field = '';
    } else if (c === '\r' || c === '\n') {
      endRow();
      if (c === '\r' && text[i + 1] === '\n') i++;
    } else field += c;
    i++;
  }
  if (field !== '' || row.length > 0) endRow();
  return rows;
}

export type ImportEntry = Pick<EntryInput, 'title' | 'url' | 'username' | 'password' | 'totp' | 'notes' | 'favorite'>;

export interface ParsedImport {
  source: Source;
  entries: ImportEntry[];
}

const truthy = (v: string) => /^(1|true|yes|y)$/i.test(v.trim());

export function hostOf(url: string): string {
  const s = url.trim();
  if (!s) return '';
  try {
    return new URL(/^[a-z][a-z0-9+.-]*:\/\//i.test(s) ? s : `https://${s}`).hostname.replace(/^www\./, '');
  } catch {
    return s;
  }
}

/** Detects the exporter from the header row (case-insensitive) and maps rows to entries. Null = not a password export. */
export function parseExport(text: string): ParsedImport | null {
  const rows = parseCsv(text);
  if (rows.length === 0) return null;
  const header = rows[0].map((h) => h.trim().toLowerCase());
  const col = (...names: string[]) => {
    for (const nm of names) {
      const idx = header.indexOf(nm);
      if (idx >= 0) return idx;
    }
    return -1;
  };

  let source: Source;
  let c: Record<keyof ImportEntry | 'type', number>;
  if (header.includes('login_password') || header.includes('login_username')) {
    source = 'bitwarden';
    c = {
      title: col('name'),
      url: col('login_uri'),
      username: col('login_username'),
      password: col('login_password'),
      totp: col('login_totp'),
      notes: col('notes'),
      favorite: col('favorite'),
      type: col('type'),
    };
  } else if (header.includes('title') && header.includes('password')) {
    // Apple Passwords and 1Password share these columns; 1Password adds Favorite/Archived/Tags.
    source = header.includes('favorite') || header.includes('archived') || header.includes('tags') ? '1password' : 'apple';
    c = {
      title: col('title'),
      url: col('url', 'website'),
      username: col('username'),
      password: col('password'),
      totp: col('otpauth', 'one-time password'),
      notes: col('notes'),
      favorite: col('favorite'),
      type: -1,
    };
  } else if (header.includes('name') && header.includes('password')) {
    source = 'chrome';
    c = {
      title: col('name'),
      url: col('url'),
      username: col('username'),
      password: col('password'),
      totp: -1,
      notes: col('note', 'notes'),
      favorite: -1,
      type: -1,
    };
  } else return null;

  const get = (r: string[], idx: number) => (idx >= 0 && idx < r.length ? r[idx].trim() : '');
  const entries: ImportEntry[] = [];
  for (const r of rows.slice(1)) {
    if (r.every((v) => v.trim() === '')) continue;
    if (c.type >= 0 && get(r, c.type) && get(r, c.type).toLowerCase() !== 'login') continue;
    const url = get(r, c.url);
    const e: ImportEntry = {
      title: get(r, c.title) || hostOf(url),
      url,
      username: get(r, c.username),
      password: r[c.password] ?? '', // passwords keep surrounding spaces
      totp: get(r, c.totp),
      notes: c.notes >= 0 ? (r[c.notes] ?? '').trim() : '',
      favorite: c.favorite >= 0 && truthy(get(r, c.favorite)),
    };
    if (!e.title && !e.username && !e.password) continue;
    entries.push(e);
  }
  return { source, entries };
}

export const dupKey = (e: { title: string; username: string; url: string }): string =>
  `${e.title}\u0000${e.username}\u0000${e.url}`;
