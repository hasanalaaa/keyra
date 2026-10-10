import { readFileSync } from 'node:fs';
import { describe, expect, it } from 'vitest';
import { hostMatches, normalizeHost } from '../src/host';

// The "Cases" table of docs/research/HOST-MATCH.md, read from the document itself so the
// extension can never drift from the shared contract.
const doc = readFileSync(new URL('../../docs/research/HOST-MATCH.md', import.meta.url), 'utf8');
const section = doc.slice(doc.indexOf('## Cases'));
const cases = section
  .split('\n')
  .filter((l) => /^\|.*\|\s*(yes|no)\s*\|$/.test(l))
  .map((l) => {
    const [login, page, match] = l.split('|').slice(1, 4).map((c) => c.trim());
    const cell = (c: string) => (c.startsWith('*') ? '' : c.replace(/`/g, ''));
    return { login: cell(login), page: cell(page), match: match === 'yes' };
  });

describe('host rule (HOST-MATCH.md)', () => {
  it('reads every row of the table', () => {
    expect(cases.length).toBe(17);
    expect(cases.filter((c) => c.match).length).toBe(5);
  });

  it.each(cases)('$login on $page → $match', ({ login, page, match }) => {
    expect(hostMatches(login, page)).toBe(match);
    // The rule is symmetric: which side is the login does not matter.
    expect(hostMatches(page, login)).toBe(match);
  });

  it('normalises like the note says', () => {
    expect(normalizeHost('  WWW.GitHub.com. ')).toBe('github.com');
    expect(normalizeHost('www.www.example.com')).toBe('www.example.com');
    expect(hostMatches('github.com', '')).toBe(false);
    expect(hostMatches('[::1]', '::1')).toBe(false);
    expect(hostMatches('::1', '::1')).toBe(true);
    expect(hostMatches('example.co.uk', 'login.example.co.uk')).toBe(true);
    expect(hostMatches('example.uk', 'a.example.uk')).toBe(true);
  });
});
