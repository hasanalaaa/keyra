// Hash routes (DESIGN §5): #/welcome, #/setup/1..3, #/unlock, #/, #/a/:id, #/a/:id/edit, #/new,
// #/import, #/backup, #/settings. Hash routing keeps Back working and sheets deep-linkable.
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
  | { name: 'settings' };

export function parseRoute(hash: string): Route {
  const parts = hash.replace(/^#\/?/, '').split('/').filter(Boolean);
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
    case 'settings':
      return { name: 'settings' };
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
