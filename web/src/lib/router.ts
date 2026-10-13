// Hash routes (DESIGN §5): #/welcome, #/setup/1..3, #/unlock, #/ (Accounts), #/a/:id, #/a/:id/edit, #/new,
// #/import, #/backup, #/restore, #/generate, #/type (SPEC §9.1–9.2), and the tabs #/typing, #/security, #/device
// with their sub-screens (#/security/health …). Hash routing keeps Back working and sheets deep-linkable.
import { useEffect, useState } from 'preact/hooks';

export type Route =
  | { name: 'welcome' }
  | { name: 'setup'; step: number }
  | { name: 'unlock' }
  | { name: 'list' }
  | { name: 'account'; id: number }
  | { name: 'edit'; id: number }
  | { name: 'new' }
  | { name: 'import' }
  | { name: 'backup' }
  | { name: 'restore' }
  | { name: 'tab'; tab: Tab; sub: string | null }
  | { name: 'generate' }
  | { name: 'typeText' }
  | { name: 'connect'; ext: string; n: string };

export type Tab = 'accounts' | 'typing' | 'security' | 'device';

/** The tab a route lives in: sheets open over their own tab, so Back lands where they came from. */
export function tabOf(r: Route): Tab {
  if (r.name === 'tab') return r.tab;
  if (r.name === 'generate' || r.name === 'typeText') return 'typing';
  if (r.name === 'import' || r.name === 'backup' || r.name === 'restore') return 'device';
  return 'accounts';
}

export const TAB_PATH: Record<Tab, string> = { accounts: '/', typing: '/typing', security: '/security', device: '/device' };

export function parseRoute(hash: string): Route {
  const [path, query = ''] = hash.replace(/^#\/?/, '').split('?');
  const parts = path.split('/').filter(Boolean);
  const [a, b, c] = parts;
  const id = Number(b);
  switch (a) {
    case 'welcome':
      return { name: 'welcome' };
    case 'setup':
      return { name: 'setup', step: Math.min(3, Math.max(1, Number(b) || 1)) };
    case 'unlock':
      return { name: 'unlock' };
    case 'a':
      if (Number.isInteger(id) && id > 0) return c === 'edit' ? { name: 'edit', id } : { name: 'account', id };
      return { name: 'list' };
    case 'new':
      return { name: 'new' };
    case 'import':
      return { name: 'import' };
    case 'backup':
      return { name: 'backup' };
    case 'restore':
      return { name: 'restore' };
    case 'typing':
    case 'security':
    case 'device':
      return { name: 'tab', tab: a, sub: b ?? null };
    case 'generate':
      return { name: 'generate' };
    case 'type':
      return { name: 'typeText' };
    case 'connect': {
      // SPEC §9.4 pairing: #/connect?ext=<name>&n=<nonce> (128-bit nonce, base64url).
      const q = new URLSearchParams(query);
      return { name: 'connect', ext: q.get('ext') ?? '', n: q.get('n') ?? '' };
    }
    default:
      return { name: 'list' };
  }
}

// history.state.k counts the in-app entries below this one, so "back" never leaves the app.
const depth = (): number => (history.state as { k?: number } | null)?.k ?? 0;

export function go(path: string): void {
  if (location.hash === `#${path}`) return;
  history.pushState({ k: depth() + 1 }, '', `#${path}`);
  window.dispatchEvent(new HashChangeEvent('hashchange'));
}

export function replace(path: string): void {
  history.replaceState({ k: depth() }, '', `#${path}`);
  window.dispatchEvent(new HashChangeEvent('hashchange'));
}

export function back(fallback = '/'): void {
  if (depth() > 0) history.back();
  else replace(fallback);
}

export function useRoute(): Route {
  const [route, setRoute] = useState(() => parseRoute(location.hash));
  useEffect(() => {
    const on = () => setRoute(parseRoute(location.hash));
    window.addEventListener('hashchange', on);
    return () => window.removeEventListener('hashchange', on);
  }, []);
  return route;
}
