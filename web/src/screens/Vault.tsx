// Home: account list (DESIGN §5.4) with sheets/dialogs per route; desktop two-pane at ≥ 900 px.
import { useEffect, useLayoutEffect, useMemo, useRef, useState } from 'preact/hooks';
import { Icon, KeyGlyph } from '../components/Icon';
import { Button, IconButton, Monogram, Notice } from '../components/ui';
import { Sheet, useMedia, type SheetCtl } from '../components/Sheet';
import { accountCount, t } from '../lib/i18n';
import { back, go, type Route } from '../lib/router';
import { lockNow, useApp } from '../lib/store';
import { hostOf } from '../lib/csv';
import { groupByLetter, highlightRange, search } from '../lib/search';
import type { EntrySummary } from '../lib/types';
import { AccountView } from './Account';
import { A2hsSheet, shouldOfferA2hs } from './A2hs';
import { lazy } from '../components/Lazy';

// Everything but the list and an account loads on first use (DESIGN Appendix A).
const EditAccount = lazy(() => import('./Edit').then((m) => m.EditAccount));
const ImportSheet = lazy(() => import('./Import').then((m) => m.ImportSheet));
const BackupSheet = lazy(() => import('./Backup').then((m) => m.BackupSheet));
const Settings = lazy(() => import('./Settings').then((m) => m.Settings));
const GenerateSheet = lazy(() => import('./Generate').then((m) => m.GenerateSheet));
const TypeTextSheet = lazy(() => import('./TypeText').then((m) => m.TypeTextSheet));
const ConnectSheet = lazy(() => import('./Tokens').then((m) => m.ConnectSheet));

let a2hsChecked = false;

export function Vault({ route }: { route: Route }) {
  const desktop = useMedia('(min-width: 900px)');
  const [a2hs, setA2hs] = useState(false);

  useEffect(() => {
    if (a2hsChecked) return;
    a2hsChecked = true;
    if (!shouldOfferA2hs()) return;
    const h = setTimeout(() => setA2hs(true), 1200);
    return () => clearTimeout(h);
  }, []);

  const selected = route.name === 'account' || route.name === 'edit' ? route.id : null;
  const closeToList = () => back('/');

  if (!desktop && route.name === 'settings')
    return (
      <>
        <Settings page onA2hs={() => setA2hs(true)} />
        {a2hs && <A2hsSheet onClose={() => setA2hs(false)} />}
      </>
    );

  return (
    <div class={desktop ? 'two-pane' : 'one-pane'}>
      <ListPane selected={desktop ? selected : null} desktop={desktop} />
      {desktop && (
        <main class="detail-pane" id="detail">
          {selected ? (
            <AccountView key={selected} id={selected} mode="pane" />
          ) : (
            <div class="placeholder">
              <KeyGlyph size={64} />
              <p>{t('pickAccount')}</p>
            </div>
          )}
        </main>
      )}
      {!desktop && route.name === 'account' && (
        <AccountSheet id={route.id} onClose={closeToList} />
      )}
      {route.name === 'edit' && <EditAccount key={`e${route.id}`} id={route.id} />}
      {route.name === 'new' && <EditAccount key="new" />}
      {route.name === 'import' && <ImportSheet />}
      {route.name === 'backup' && <BackupSheet />}
      {route.name === 'generate' && <GenerateSheet />}
      {route.name === 'typeText' && <TypeTextSheet />}
      {route.name === 'connect' && <ConnectSheet ext={route.ext} n={route.n} />}
      {desktop && route.name === 'settings' && <Settings onA2hs={() => setA2hs(true)} />}
      {a2hs && <A2hsSheet onClose={() => setA2hs(false)} />}
    </div>
  );
}

function AccountSheet({ id, onClose }: { id: number; onClose: () => void }) {
  const app = useApp();
  const title = app.entries?.find((e) => e.id === id)?.title ?? '';
  return (
    <Sheet title={title} hideTitle onClose={onClose} size="md">
      <AccountView id={id} mode="sheet" />
    </Sheet>
  );
}

function ListPane({ selected, desktop }: { selected: number | null; desktop: boolean }) {
  const app = useApp();
  const [q, setQ] = useState('');
  const [fabHidden, setFabHidden] = useState(false);
  const [menu, setMenu] = useState(false);
  const [atTop, setAtTop] = useState(true);
  const pane = useRef<HTMLDivElement>(null);
  const [skeleton, setSkeleton] = useState(false);
  const searchRef = useRef<HTMLInputElement>(null);
  const lastY = useRef(0);
  const entries = app.entries;
  const d = app.device;
  // Phones scroll the window (null); the desktop list pane scrolls itself.
  const scroller = desktop ? pane : null;

  useEffect(() => {
    if (entries) return;
    const h = setTimeout(() => setSkeleton(true), 150);
    return () => clearTimeout(h);
  }, [entries === null]);

  // Toasts lift above the phone FAB (DESIGN §4.10).
  useEffect(() => {
    if (desktop) return;
    document.documentElement.classList.add('has-fab');
    return () => document.documentElement.classList.remove('has-fab');
  }, [desktop]);

  // Phones scroll the window; the desktop list pane scrolls itself.
  useEffect(() => {
    const el = pane.current;
    const onScroll = () => {
      const y = desktop ? (el?.scrollTop ?? 0) : window.scrollY;
      if (y > lastY.current + 4 && y > 80) setFabHidden(true);
      else if (y < lastY.current - 4) setFabHidden(false);
      lastY.current = y;
      setAtTop(y < 4);
    };
    const target = desktop ? el : window;
    target?.addEventListener('scroll', onScroll, { passive: true });
    return () => target?.removeEventListener('scroll', onScroll);
  }, [desktop]);

  const results = useMemo(() => (entries && q.trim() ? search(entries, q).map((m) => m.entry) : null), [entries, q]);
  const favorites = useMemo(() => (entries ?? []).filter((e) => e.favorite), [entries]);
  const recent = useMemo(
    () =>
      (entries ?? [])
        .filter((e) => !e.favorite && e.lastUsed > 0)
        .sort((a, b) => b.lastUsed - a.lastUsed)
        .slice(0, 3),
    [entries],
  );
  const groups = useMemo(() => groupByLetter(entries ?? [], app.lang), [entries, app.lang]);
  const flat = results ?? [...favorites, ...recent, ...groups.flatMap((g) => g.items)];

  // Keyboard: "/" focuses search, ↑/↓ move the selection, Enter opens, "n" adds.
  useEffect(() => {
    const onKey = (e: KeyboardEvent) => {
      const el = e.target as HTMLElement;
      const typing = el.matches('input, textarea, select') || el.isContentEditable;
      if (document.querySelector('.layer [aria-modal="true"]')) return;
      if (e.key === '/' && !typing) {
        e.preventDefault();
        searchRef.current?.focus();
      } else if (e.key === 'n' && !typing && !e.metaKey && !e.ctrlKey) {
        go('/new');
      } else if (desktop && (e.key === 'ArrowDown' || e.key === 'ArrowUp') && (!typing || el === searchRef.current)) {
        if (flat.length === 0) return;
        e.preventDefault();
        const i = selected === null ? -1 : flat.findIndex((x) => x.id === selected);
        const n = Math.min(flat.length - 1, Math.max(0, i + (e.key === 'ArrowDown' ? 1 : -1)));
        go(`/a/${flat[n].id}`);
        document.querySelector<HTMLElement>(`[data-id="${flat[n].id}"]`)?.scrollIntoView({ block: 'nearest' });
      }
    };
    document.addEventListener('keydown', onKey);
    return () => document.removeEventListener('keydown', onKey);
  });

  const pending = d?.pending;
  const showPill = pending && pending.what !== 'test' && (desktop ? selected !== pending.id : true);

  return (
    <div class={`list-pane${atTop ? ' at-top' : ''}`} ref={pane}>
      {desktop && (
        <a class="skip-link" href="#accounts" onClick={(e) => {
          e.preventDefault();
          document.querySelector<HTMLElement>('#accounts button')?.focus();
        }}>
          {t('skipToAccounts')}
        </a>
      )}
      <header class="top-bar glass">
        <span class={`chip ${d?.host.output ? 'chip-ok' : 'chip-neutral'}`}>
          <Icon name={d?.host.output === 'ble' || (d && !d.host.output && d.host.usb) ? 'bluetooth' : 'usb'} size={16} />
          {d?.host.output === 'ble' ? t('bleOn') : d?.host.output === 'usb' ? t('usbOn') : d?.host.usb ? t('bleNone') : t('usbOff')}
        </span>
        <span class="spacer" />
        <IconButton icon="wand-sparkles" label={t('generate')} class="gen-btn" onClick={() => go('/generate')} />
        <IconButton icon="ellipsis" label={t('moreMenu')} class="more-btn" onClick={() => setMenu(true)} />
        <IconButton icon="settings" label={t('settings')} onClick={() => go('/settings')} />
        <IconButton icon="lock" label={t('lock')} onClick={() => void lockNow()} />
      </header>
      <div class="large-title">
        <h1 class="t1">{t('accounts')}</h1>
        {entries && <p class="count">{accountCount(entries.length)}</p>}
      </div>
      <div class="search-wrap glass">
        <label class="search">
          <Icon name="search" size={20} />
          <input
            ref={searchRef}
            type="search"
            inputMode="search"
            enterkeyhint="search"
            autocomplete="off"
            spellcheck={false}
            placeholder={t('searchPlaceholder')}
            aria-label={t('searchPlaceholder')}
            value={q}
            onInput={(e) => setQ(e.currentTarget.value)}
          />
          {q && <IconButton icon="x" label={t('clearSearch')} onClick={() => {
            setQ('');
            searchRef.current?.focus();
          }} size={20} />}
        </label>
      </div>
      <div class="list" id="accounts">
        {!entries ? (
          skeleton && <Skeleton />
        ) : entries.length === 0 ? (
          <EmptyVault />
        ) : results ? (
          results.length === 0 ? (
            <div class="empty">
              <Orbit />
              <h2 class="t2">{t('noMatchTitle')}</h2>
              <p class="callout">{t('noMatchBody', { q: q.trim() })}</p>
              <Button variant="ghost" onClick={() => setQ('')}>
                {t('clearSearch')}
              </Button>
            </div>
          ) : (
            <Group title={t('results')} items={results} q={q} selected={selected} scroller={scroller} />
          )
        ) : (
          <>
            <BackupReminder />
            {favorites.length > 0 && <Group title={t('favorites')} items={favorites} selected={selected} scroller={scroller} />}
            {recent.length > 0 && <Group title={t('recent')} items={recent} selected={selected} scroller={scroller} />}
            <h2 class="section-head">{t('all')}</h2>
            {groups.map((g) => (
              <section key={g.letter} class="letter-group">
                <h3 class="letter-head" dir="auto">
                  {g.letter}
                </h3>
                <Rows items={g.items} selected={selected} scroller={scroller} />
              </section>
            ))}
          </>
        )}
      </div>
      {showPill && pending && (
        <button type="button" class="ready-pill glass" onClick={() => go(pending.what === 'text' ? '/type' : pending.what === 'probe' || pending.what === 'test' ? '/settings' : `/a/${pending.id}`)}>
          <span class="pill-dot" aria-hidden="true" />
          <span class="pill-text">
            {t('readyPill', { title: '' })}
            <bdi>{pending.title ?? t('chipText')}</bdi>
            {pending.by && (
              <>
                {' '}
                <bdi>{t('readyBy', { name: pending.by })}</bdi>
              </>
            )}
            {pending.host && (
              <>
                {' · '}
                <bdi dir="ltr">{pending.host}</bdi>
              </>
            )}
          </span>
        </button>
      )}
      {menu && <VaultMenu onClose={() => setMenu(false)} />}
      {!desktop && (
        <button type="button" class={`fab${fabHidden ? ' hidden' : ''}`} aria-label={t('addAccount')} onClick={() => go('/new')}>
          <Icon name="plus" size={24} />
        </button>
      )}
      {desktop && entries && entries.length > 0 && (
        <div class="pane-foot">
          <Button icon="plus" full onClick={() => go('/new')}>
            {t('addAccount')}
          </Button>
        </div>
      )}
    </div>
  );
}

const BACKUP_DAYS = 30;
const LATER_KEY = 'keyra.backupLater';

/** SPEC §12.5: nudge when the last backup is older than 30 days (or never made); "Later" hides it for this tab. */
function BackupReminder() {
  const app = useApp();
  const [later, setLater] = useState(() => {
    try {
      return sessionStorage.getItem(LATER_KEY) === '1';
    } catch {
      return false;
    }
  });
  const at = app.backupAt;
  if (later || at === null) return null;
  const stale = at === 0 || Date.now() / 1000 - at > BACKUP_DAYS * 86400;
  if (!stale) return null;
  return (
    <div class="backup-reminder">
      <Notice
        tone="warn"
        icon="download"
        action={
          <span class="reminder-actions">
            <Button size="sm" variant="tinted" onClick={() => go('/backup')}>
              {t('backupNow')}
            </Button>
            <Button
              size="sm"
              variant="ghost"
              onClick={() => {
                try {
                  sessionStorage.setItem(LATER_KEY, '1');
                } catch {
                  // Storage blocked: hidden until the page reloads.
                }
                setLater(true);
              }}
            >
              {t('later')}
            </Button>
          </span>
        }
      >
        {at === 0 ? t('backupNever') : t('backupReminder')}
      </Notice>
    </div>
  );
}

type Scroller = { current: HTMLElement | null } | null;

interface RowsProps {
  items: EntrySummary[];
  q?: string;
  selected: number | null;
  scroller: Scroller;
}

function Group({ title, ...rows }: RowsProps & { title: string }) {
  return (
    <section class="group">
      <h2 class="section-head">{title}</h2>
      <Rows {...rows} />
    </section>
  );
}

// Long lists render only the rows near the viewport (thousands of accounts stay smooth); spacer items
// keep the full height, so scrolling, the scrollbar and sticky letters behave as if every row were there.
// Short lists (every vault under WINDOW_MIN per section) render whole.
const WINDOW_MIN = 60;
const OVERSCAN = 8; // rows past each edge: Tab and ↑/↓ reach the next row before it is needed
// Every account row is one height (single-line title and subtitle). It is measured from the first row drawn
// and shared, so every section's spacers agree: a section scrolled into view never changes the list height.
let rowH = 64; // .acc-row's minimum height, until a drawn row is measured
const rowHListeners = new Set<() => void>();
function learnRowHeight(li: HTMLElement | null | undefined): void {
  const h = li?.getBoundingClientRect().height ?? 0;
  if (h > 0 && Math.abs(h - rowH) > 0.01) {
    rowH = h;
    rowHListeners.forEach((f) => f());
  }
}

/** [first, end) of the rows to draw, the row height the spacers use, and a way to keep a row's neighbours drawn. */
function useWindow(items: unknown[], list: { current: HTMLUListElement | null }, scroller: Scroller): [number, number, number, (i: number) => void] {
  const n = items.length;
  const on = n > WINDOW_MIN;
  const [range, setRange] = useState<[number, number]>([0, 0]);
  const [, setH] = useState(rowH);
  const update = useRef(() => {});
  update.current = () => {
    const ul = list.current;
    if (!ul) return;
    const pane = scroller?.current;
    const view = pane ? pane.getBoundingClientRect() : { top: 0, bottom: innerHeight };
    const top = ul.getBoundingClientRect().top;
    const s = Math.min(n, Math.max(0, Math.floor((view.top - top) / rowH) - OVERSCAN));
    const e = Math.min(n, Math.max(s, Math.ceil((view.bottom - top) / rowH) + OVERSCAN));
    setRange((p) => (p[0] === s && p[1] === e ? p : [s, e]));
  };
  useLayoutEffect(() => {
    if (!on) return;
    const target = scroller?.current ?? window;
    const run = () => update.current();
    const remeasured = () => {
      setH(rowH);
      run();
    };
    run();
    target.addEventListener('scroll', run, { passive: true });
    addEventListener('resize', run);
    rowHListeners.add(remeasured);
    return () => {
      target.removeEventListener('scroll', run);
      removeEventListener('resize', run);
      rowHListeners.delete(remeasured);
    };
  }, [on, items, scroller]);
  useLayoutEffect(() => {
    if (on) learnRowHeight(list.current?.querySelector<HTMLElement>('li[aria-posinset]'));
  });
  // A focused row keeps OVERSCAN rows drawn on each side at once, so the next Tab or ↓ always has a row
  // to land on, however fast the keys come (the scroll that follows would draw them a frame later).
  const reveal = (i: number) =>
    i >= 0 &&
    setRange((p) => (i - OVERSCAN >= p[0] && i + OVERSCAN < p[1] ? p : [Math.max(0, Math.min(p[0], i - OVERSCAN)), Math.min(n, Math.max(p[1], i + OVERSCAN + 1))]));
  return on ? [Math.min(range[0], n), Math.min(range[1], n), rowH, reveal] : [0, n, rowH, reveal];
}

function Rows({ items, q, selected, scroller }: RowsProps) {
  const list = useRef<HTMLUListElement>(null);
  const n = items.length;
  const [start, end, h, reveal] = useWindow(items, list, scroller);
  const windowed = n > WINDOW_MIN;
  const gap = (key: string, rows: number) => rows > 0 && <li key={key} class="rows-gap" aria-hidden="true" style={{ blockSize: `${rows * h}px` }} />;
  return (
    <ul
      class="card rows"
      ref={list}
      onFocusIn={windowed ? (e) => reveal(Number((e.target as HTMLElement).closest('li')?.getAttribute('aria-posinset')) - 1) : undefined}
    >
      {windowed && gap('gap-start', start)}
      {items.slice(start, end).map((e, i) => (
        <li key={e.id} aria-setsize={windowed ? n : undefined} aria-posinset={windowed ? start + i + 1 : undefined}>
          <button
            type="button"
            class={`row acc-row${selected === e.id ? ' selected' : ''}`}
            data-id={e.id}
            aria-current={selected === e.id ? 'true' : undefined}
            onClick={() => go(`/a/${e.id}`)}
          >
            <Monogram title={e.title} />
            <span class="row-text">
              <span class="row-title" dir="auto">
                <Highlight text={e.title} q={q} />
              </span>
              <span class="row-sub" dir="ltr">
                {e.username ? <Highlight text={e.username} q={q} /> : hostOf(e.url)}
              </span>
            </span>
            {e.favorite && <Icon name="star" size={16} class="i-fill row-star" />}
            <Icon name="chevron-right" size={16} class="row-chev" />
          </button>
        </li>
      ))}
      {windowed && gap('gap-end', n - end)}
    </ul>
  );
}

function Highlight({ text, q }: { text: string; q?: string }) {
  const r = q ? highlightRange(text, q) : null;
  if (!r) return <>{text}</>;
  return (
    <>
      {text.slice(0, r[0])}
      <mark>{text.slice(r[0], r[1])}</mark>
      {text.slice(r[1])}
    </>
  );
}

function Skeleton() {
  return (
    <ul class="card rows" aria-hidden="true">
      {[0, 1, 2, 3, 4, 5].map((i) => (
        <li key={i} class="row skel-row">
          <span class="skel skel-mono" />
          <span class="row-text">
            <span class="skel skel-a" />
            <span class="skel skel-b" />
          </span>
        </li>
      ))}
    </ul>
  );
}

function Orbit() {
  return (
    <div class="orbit" aria-hidden="true">
      <span class="orbit-dots">
        <i />
        <i />
        <i />
      </span>
      <KeyGlyph size={40} />
    </div>
  );
}

function EmptyVault() {
  return (
    <div class="empty">
      <Orbit />
      <h2 class="t2">{t('emptyTitle')}</h2>
      <p class="callout">{t('emptyBody')}</p>
      <Button icon="plus" onClick={() => go('/new')}>
        {t('addAccount')}
      </Button>
      <Button variant="ghost" icon="upload" onClick={() => go('/import')}>
        {t('import')}
      </Button>
    </div>
  );
}

/** The top bar's "More" menu: vault-wide actions that are not one-tap buttons. */
function VaultMenu({ onClose }: { onClose: () => void }) {
  const ctl = useRef<SheetCtl | null>(null);
  const next = useRef<string | null>(null);
  const pick = (path: string) => {
    next.current = path;
    ctl.current?.close();
  };
  const item = (icon: 'keyboard' | 'upload' | 'download', label: string, path: string) => (
    <button type="button" class="row nav-row menu-row" onClick={() => pick(path)}>
      <Icon name={icon} size={22} class="row-icon" />
      <span class="row-label">{label}</span>
      <Icon name="chevron-right" size={16} class="row-chev" />
    </button>
  );
  return (
    <Sheet
      title={t('moreMenu')}
      size="sm"
      ctl={ctl}
      onClose={() => {
        onClose();
        if (next.current) go(next.current);
      }}
    >
      <div class="card">
        {item('keyboard', t('typeText'), '/type')}
        {item('upload', t('import'), '/import')}
        {item('download', t('backupTitle'), '/backup')}
      </div>
    </Sheet>
  );
}
