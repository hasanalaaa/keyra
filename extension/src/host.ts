// Which stored login is offered on which page (SPEC §9.4). One rule shared with the device and
// the Android app; docs/research/HOST-MATCH.md has the reasons and the table test/host.test.ts runs.

/** Second labels that, under a two-letter country label, are a registry rather than a site. */
const COUNTRY_SECOND_LEVEL = new Set(['ac', 'co', 'com', 'edu', 'gob', 'gov', 'go', 'mil', 'ne', 'net', 'or', 'org', 'sch']);

/** Lowercase, trim spaces and one trailing dot, drop one leading `www.`. */
export function normalizeHost(host: string): string {
  return host.trim().toLowerCase().replace(/\.$/, '').replace(/^www\./, '');
}

export const isIpHost = (h: string): boolean => h.includes(':') || /^\d{1,3}(\.\d{1,3}){3}$/.test(h);

/** At least two labels and not a country registry like `co.uk` or `gov.iq`. */
function siteLike(h: string): boolean {
  const labels = h.split('.');
  if (labels.length < 2 || labels.some((l) => !l)) return false;
  return !(labels.length === 2 && labels[1].length === 2 && COUNTRY_SECOND_LEVEL.has(labels[0]));
}

/** True when a login saved for `login` may be offered on a page at `page`. */
export function hostMatches(login: string, page: string): boolean {
  const a = normalizeHost(login);
  const b = normalizeHost(page);
  if (!a || !b) return false;
  if (a === b) return true;
  if (isIpHost(a) || isIpHost(b)) return false;
  const [short, long] = a.length <= b.length ? [a, b] : [b, a];
  return long.endsWith(`.${short}`) && siteLike(short);
}
