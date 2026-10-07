// Home: account list (DESIGN §5.4) with sheets/dialogs per route; desktop two-pane at ≥ 900 px.
import { useEffect, useMemo, useRef, useState } from 'preact/hooks';
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
import { EditAccount } from './Edit';
import { ImportSheet } from './Import';
import { BackupSheet } from './Backup';
import { Settings } from './Settings';
import { A2hsSheet, shouldOfferA2hs } from './A2hs';
import { GenerateSheet } from './Generate';
import { TypeTextSheet } from './TypeText';

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

  if (!desktop && route.name === 'settings') return <Settings page onA2hs={() => setA2hs(true)} />;

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
            <Group title={t('results')} items={results} q={q} selected={selected} />
          )
        ) : (
          <>
            <BackupReminder />
            {favorites.length > 0 && <Group title={t('favorites')} items={favorites} selected={selected} />}
            {recent.length > 0 && <Group title={t('recent')} items={recent} selected={selected} />}
            <h2 class="section-head">{t('all')}</h2>
            {groups.map((g) => (
              <section key={g.letter} class="letter-group">
                <h3 class="letter-head" dir="auto">
                  {g.letter}
                </h3>
                <Rows items={g.items} selected={selected} />
              </section>
            ))}
          </>
        )}
      </div>
      {showPill && pending && (
        <button type="button" class="ready-pill glass" onClick={() => go(pending.what === 'text' ? '/type' : `/a/${pending.id}`)}>
          <span class="pill-dot" aria-hidden="true" />
          <span>{t('readyPill', { title: '' })}<bdi>{pending.title ?? t('chipText')}</bdi></span>
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

function Group({ title, items, q, selected }: { title: string; items: EntrySummary[]; q?: string; selected: number | null }) {
  return (
    <section class="group">
      <h2 class="section-head">{title}</h2>
      <Rows items={items} q={q} selected={selected} />
    </section>
  );
}

function Rows({ items, q, selected }: { items: EntrySummary[]; q?: string; selected: number | null }) {
  return (
    <ul class="card rows">
      {items.map((e) => (
        <li key={e.id}>
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
