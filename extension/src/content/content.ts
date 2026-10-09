// In the page (SPEC §9.4): the Keyra key in login fields, the list of matching logins, the press
// card while Keyra waits for its button, a strong password for new-password fields, and the
// "Save to Keyra?" card after a form is sent. Everything lives in one closed shadow root.
import type { Failure, Hello, Login, PageConfig, SaveCard, Status, TabMsg, What } from '../messages';
import { ext } from '../ext';
import { findForms, roleOf, snapshot, whatFor, type FieldRole, type LoginForm } from '../forms';
import { hostMatches, normalizeHost } from '../host';
import { t, uiDir, uiLang, type Key } from '../i18n';
import { h, icon, logo, monogram, svgEl, type IconName } from '../ui/dom';
import { CSS } from './style';

type Reply<T> = ({ ok: true } & T) | Failure;

async function send<T>(msg: object): Promise<Reply<T>> {
  try {
    const r = (await ext.runtime.sendMessage(msg)) as Reply<T> | undefined;
    return r ?? { ok: false, code: 'error', message: 'No answer' };
  } catch (e) {
    return { ok: false, code: 'error', message: String((e as Error)?.message ?? e) };
  }
}

let config: PageConfig = { connected: false, showIcons: false, self: false };
let forms: LoginForm[] = [];
const pageHost = () => normalizeHost(location.hostname.replace(/^\[|\]$/g, ''));

// ---------- the shadow layer ----------

let host: HTMLDivElement | null = null;
let layer: HTMLDivElement;

function ensureLayer(): HTMLDivElement {
  if (host?.isConnected) return layer;
  if (!host) {
    host = document.createElement('div');
    host.style.cssText = 'all:initial;position:fixed;inset:0;width:100%;height:100%;margin:0;padding:0;border:0;background:transparent;overflow:visible;pointer-events:none;z-index:2147483647;display:block';
    const root = host.attachShadow({ mode: 'closed' });
    try {
      const sheet = new CSSStyleSheet();
      sheet.replaceSync(CSS);
      root.adoptedStyleSheets = [sheet];
    } catch {
      root.append(h('style', {}, CSS));
    }
    layer = h('div', { class: 'root', dir: uiDir(), lang: uiLang() });
    root.append(layer);
    if ('showPopover' in host) host.setAttribute('popover', 'manual');
  }
  document.documentElement.append(host);
  raise();
  return layer;
}

/** Keeps the layer above modal dialogs and popovers the page opened later (top layer). */
function raise(): void {
  if (!host || !('showPopover' in host)) return;
  try {
    if (host.matches(':popover-open')) host.hidePopover();
    host.showPopover();
  } catch {
    /* not in the document yet, or the browser has no top layer: z-index still applies */
  }
}

const ours = (n: unknown) => !!host && n === host;

// ---------- the key in the field ----------

interface Ctx {
  field: HTMLInputElement;
  form: LoginForm;
  role: FieldRole;
}

let keyBtn: HTMLButtonElement | null = null;
let keyCtx: Ctx | null = null;

/** Where the key can sit at the field's end without covering the site's own icons; null if nowhere. */
function keySpot(field: HTMLInputElement): { x: number; y: number } | null {
  const r = field.getBoundingClientRect();
  const size = 22;
  if (r.height < 20 || r.width < 80 || r.bottom < 0 || r.top > innerHeight) return null;
  const rtl = getComputedStyle(field).direction === 'rtl';
  const y = r.top + (r.height - size) / 2;
  for (let off = Math.min(10, (r.height - size) / 2 + 2); off < r.width * 0.45; off += 26) {
    const x = rtl ? r.left + off : r.right - off - size;
    const probes = [
      [x + size / 2, y + size / 2],
      [x + 2, y + 2],
      [x + size - 2, y + size - 2],
      [x + 2, y + size - 2],
      [x + size - 2, y + 2],
    ];
    const free = probes.every(([px, py]) => {
      const top = document.elementsFromPoint(px, py).find((el) => !ours(el));
      return top === field || (!!top && top.contains(field) && top !== document.body && top !== document.documentElement && top.tagName === 'LABEL');
    });
    if (free) return { x, y };
  }
  return null;
}

function showKey(ctx: Ctx): void {
  if (!config.connected || !config.showIcons) return;
  const spot = keySpot(ctx.field);
  if (!spot) return hideKey();
  ensureLayer();
  if (!keyBtn) {
    keyBtn = h('button', { class: 'key', type: 'button', 'aria-haspopup': 'menu', 'aria-expanded': 'false', 'aria-label': t('iconLabel'), title: t('iconLabel') }, logo(22));
    keyBtn.addEventListener('mousedown', (e) => e.preventDefault()); // the field keeps focus
    keyBtn.addEventListener('click', () => (menuEl ? closeMenu() : keyCtx && openMenu(keyCtx)));
  }
  keyCtx = ctx;
  keyBtn.style.left = `${spot.x}px`;
  keyBtn.style.top = `${spot.y}px`;
  if (!keyBtn.isConnected) layer.append(keyBtn);
}

function hideKey(): void {
  keyBtn?.remove();
  keyCtx = null;
}

// ---------- the dropdown ----------

let menuEl: HTMLElement | null = null;
let menuCtx: Ctx | null = null;
let menuItems: { el: HTMLElement; run: () => void }[] = [];
let menuOn = -1;

function placeMenu(): void {
  if (!menuEl || !menuCtx) return;
  const r = menuCtx.field.getBoundingClientRect();
  const w = Math.min(320, innerWidth - 16);
  const rtl = uiDir() === 'rtl';
  let x = rtl ? r.left : r.right - w;
  x = Math.max(8, Math.min(x, innerWidth - w - 8));
  const below = innerHeight - r.bottom;
  const hgt = menuEl.offsetHeight || 260;
  const y = below >= hgt + 12 || below > r.top ? r.bottom + 6 : Math.max(8, r.top - hgt - 6);
  menuEl.style.left = `${x}px`;
  menuEl.style.top = `${y}px`;
}

function setItems(list: HTMLElement, items: { el: HTMLElement; run: () => void }[]): void {
  menuItems = items;
  menuOn = -1;
  for (const [i, it] of items.entries()) {
    it.el.addEventListener('click', it.run);
    it.el.addEventListener('mousemove', () => highlight(i));
  }
  list.replaceChildren(...items.map((i) => i.el));
}

function highlight(i: number): void {
  menuItems[menuOn]?.el.classList.remove('on');
  menuOn = i;
  const it = menuItems[i];
  if (it) {
    it.el.classList.add('on');
    it.el.scrollIntoView({ block: 'nearest' });
  }
}

function loginItem(l: Login, run: () => void): { el: HTMLElement; run: () => void } {
  const el = h(
    'button',
    { class: 'item', type: 'button', role: 'menuitem', 'data-id': l.id },
    monogram(l.title, 32),
    h('span', { class: 'txt' }, h('bdi', { class: 't1', dir: 'auto' }, l.title), h('span', { class: 't2 ltr', dir: 'ltr' }, l.host || '—')),
  );
  return { el, run };
}

function specialItem(ic: IconName, title: string, sub: string, run: () => void, cls = ''): { el: HTMLElement; run: () => void } {
  const el = h(
    'button',
    { class: `item special ${cls}`, type: 'button', role: 'menuitem' },
    h('span', { class: 'badge' }, icon(ic, 18)),
    h('span', { class: 'txt' }, h('span', { class: 't1' }, title), sub ? h('span', { class: 't2' }, sub) : null),
  );
  return { el, run };
}

function menuShell(...children: (Node | null)[]): HTMLElement {
  const m = h('div', { class: 'menu', role: 'menu', 'aria-label': 'Keyra' }, ...children);
  m.addEventListener('mousedown', (e) => {
    if (!(e.target instanceof HTMLInputElement)) e.preventDefault();
  });
  return m;
}

function menuHead(): HTMLElement {
  return h('div', { class: 'menu-head' }, logo(16), 'Keyra', h('span', { class: 'host ltr', dir: 'ltr' }, pageHost()));
}

function inlineError(f: Failure): HTMLElement {
  const v = errorView(f);
  return h(
    'div',
    { class: 'empty', role: 'alert' },
    h('div', { style: 'display:flex;gap:10px;align-items:flex-start' }, h('span', { class: `badge-round tone-${v.tone}`, style: 'width:32px;height:32px' }, icon(v.icon, 18)), h('div', {}, h('div', { style: 'font-weight:600;color:var(--text)' }, v.title), h('div', {}, v.body))),
    v.action ? h('div', { class: 'actions' }, h('button', { class: 'btn secondary', type: 'button', onclick: v.action.run }, v.action.label)) : null,
  );
}

async function openMenu(ctx: Ctx): Promise<void> {
  closeMenu();
  menuCtx = ctx;
  const list = h('div', { class: 'list' }, h('div', { class: 'loading' }, h('span', { class: 'spin' }), t('menuLoading')));
  const top: { el: HTMLElement; run: () => void }[] = [];
  const isNew = ctx.role === 'new' || ctx.role === 'confirm';
  const genBox = h('div', {});
  if (isNew) {
    const g = specialItem('sparkles', t('menuGenerate'), t('menuGenerateSub'), () => void fillGenerated(ctx));
    g.el.addEventListener('click', g.run);
    genBox.append(g.el, h('div', { class: 'sep' }));
    top.push(g);
  }
  menuEl = menuShell(menuHead(), genBox, list, h('div', { class: 'sep' }), h('div', { class: 'foot' }, t('menuFoot')));
  ensureLayer().append(menuEl);
  raise();
  keyBtn?.setAttribute('aria-expanded', 'true');
  placeMenu();

  const r = await send<{ entries: Login[] }>({ t: 'match' });
  if (menuCtx !== ctx || !menuEl) return;
  const other = specialItem('search', t('menuOther'), '', () => void openSearch(ctx));
  if (!r.ok) {
    list.replaceChildren(inlineError(r));
  } else {
    const items = r.entries.map((l) => loginItem(l, () => choose(l, ctx)));
    const empty = r.entries.length === 0 ? h('div', { class: 'empty' }, t('menuEmpty', { host: pageHost() })) : null;
    setItems(list, [...top, ...items, other]);
    list.replaceChildren(...(empty ? [empty] : []), ...items.map((i) => i.el), h('div', { class: 'sep' }), other.el);
    if (top.length) menuItems = [...top, ...items, other];
  }
  placeMenu();
}

async function openSearch(ctx: Ctx): Promise<void> {
  if (!menuEl) return;
  const input = h('input', { type: 'search', placeholder: t('menuSearch'), 'aria-label': t('menuSearch'), autocomplete: 'off', spellcheck: 'false' });
  const list = h('div', { class: 'list' }, h('div', { class: 'loading' }, h('span', { class: 'spin' }), t('menuLoading')));
  const backBtn = h('button', { class: 'x', type: 'button', 'aria-label': t('back'), style: 'width:28px;height:28px;border-radius:8px;display:grid;place-items:center' }, icon('back', 18, 'chev'));
  backBtn.addEventListener('click', () => void openMenu(ctx));
  const head = h('div', { class: 'menu-head' }, backBtn, t('menuOther'));
  menuEl.replaceChildren(head, h('label', { class: 'search' }, icon('search', 18), input), list);
  input.focus();
  placeMenu();
  const r = await send<{ entries: Login[] }>({ t: 'entries' });
  if (!menuEl || menuCtx !== ctx) return;
  if (!r.ok) {
    list.replaceChildren(inlineError(r));
    return;
  }
  const all = [...r.entries].sort((a, b) => a.title.localeCompare(b.title));
  const fold = (s: string) => s.toLowerCase().replace(/[ً-ْـ]/g, '').replace(/[أإآ]/g, 'ا').replace(/ى/g, 'ي').replace(/ة/g, 'ه');
  const render = () => {
    const q = fold(input.value.trim());
    const shown = all.filter((l) => !q || fold(l.title).includes(q) || l.host.includes(q)).slice(0, 50);
    if (!shown.length) {
      menuItems = [];
      list.replaceChildren(h('div', { class: 'empty' }, t('menuNoResults')));
    } else setItems(list, shown.map((l) => loginItem(l, () => choose(l, ctx))));
    placeMenu();
  };
  input.addEventListener('input', render);
  input.addEventListener('keydown', menuKeys);
  render();
}

function closeMenu(): void {
  menuEl?.remove();
  menuEl = null;
  menuCtx = null;
  menuItems = [];
  keyBtn?.setAttribute('aria-expanded', 'false');
}

function menuKeys(e: KeyboardEvent): boolean {
  if (!menuEl) return false;
  if (e.key === 'ArrowDown' || e.key === 'ArrowUp') {
    if (!menuItems.length) return true;
    const d = e.key === 'ArrowDown' ? 1 : -1;
    highlight((menuOn + d + menuItems.length) % menuItems.length);
  } else if (e.key === 'Enter' && menuOn >= 0) menuItems[menuOn]?.run();
  else if (e.key === 'Escape') {
    const f = menuCtx?.field;
    closeMenu();
    f?.focus();
  } else return false;
  e.preventDefault();
  e.stopPropagation();
  return true;
}

// ---------- typing a login ----------

let flow: { login: Login; ctx: Ctx; anyHost: boolean } | null = null;

function choose(login: Login, ctx: Ctx): void {
  closeMenu();
  if (!hostMatches(login.host, pageHost())) return showWarning(login, ctx);
  void arm(login, ctx, false);
}

function showWarning(login: Login, ctx: Ctx): void {
  const page = pageHost();
  showCard({
    badge: badge('alert', 'warn'),
    title: t('warnTitle'),
    body: t('warnBody', { title: login.title, login: login.host || '—', page }),
    extra: [
      h(
        'div',
        { class: 'hosts' },
        h('span', {}, t('saveSite')),
        h('b', { class: 'ltr', dir: 'ltr' }, page),
        h('span', {}, login.title),
        h('b', { class: 'ltr', dir: 'ltr' }, login.host || '—'),
      ),
    ],
    actions: [
      { label: t('warnContinue'), cls: 'danger', run: () => void arm(login, ctx, true) },
      { label: t('cancel'), cls: 'secondary', run: closeCard },
    ],
    stack: true,
  });
}

/** Sets a field's value the way typing would, so page frameworks notice. */
function setValue(el: HTMLInputElement, v: string): void {
  const setter = Object.getOwnPropertyDescriptor(HTMLInputElement.prototype, 'value')?.set;
  if (setter) setter.call(el, v);
  else el.value = v;
  el.dispatchEvent(new Event('input', { bubbles: true, composed: true }));
  el.dispatchEvent(new Event('change', { bubbles: true }));
}

const CHIP: Record<What, Key> = { username: 'chipUsername', password: 'chipPassword', both: 'chipBoth' };

async function arm(login: Login, ctx: Ctx, anyHost: boolean): Promise<void> {
  const w = whatFor(ctx.form, ctx.role);
  if (!w) return;
  flow = { login, ctx, anyHost };
  // Keyra types after what is there; start from empty fields so the result is exactly the login.
  if (w.what !== 'password' && ctx.form.username?.value) setValue(ctx.form.username, '');
  if (w.what !== 'username') {
    const pw = w.focus.type === 'password' || w.what === 'password' ? w.focus : ctx.form.current;
    if (pw?.value) setValue(pw, '');
  }
  w.focus.focus({ preventScroll: false });
  showCard({ badge: spinnerBadge(), title: t('menuLoading') });
  const r = await send<{ expiresIn: number }>({ t: 'type', id: login.id, what: w.what, anyHost });
  if (!r.ok) {
    if (r.code === 'host_mismatch') return showWarning(login, ctx);
    return showError(r, () => void arm(login, ctx, anyHost));
  }
  w.focus.focus({ preventScroll: true });
  pressCard({ chip: `${t(CHIP[w.what])} · ${login.title}`, body: t('pressBody'), expiresIn: r.expiresIn, target: w.focus, mode: 'type', anyHost });
}

// ---------- cards ----------

let cardEl: HTMLElement | null = null;
let timers: number[] = [];
let targetEl: HTMLElement | null = null;
let targetField: HTMLElement | null = null;

function clearTimers(): void {
  for (const id of timers) clearTimeout(id), clearInterval(id);
  timers = [];
}

function closeCard(): void {
  clearTimers();
  cardEl?.remove();
  cardEl = null;
  setTarget(null);
}

function badge(ic: IconName, tone: 'ok' | 'warn' | 'err' | 'info'): HTMLElement {
  return h('span', { class: `badge-round tone-${tone}` }, icon(ic, 24));
}

function spinnerBadge(): HTMLElement {
  return h('span', { class: 'badge-round tone-info' }, h('span', { class: 'spin' }));
}

interface CardOpts {
  badge: Node;
  title: string;
  body?: string;
  extra?: Node[];
  actions?: { label: string; cls: string; run: () => void; ic?: IconName }[];
  stack?: boolean;
  closable?: boolean;
  onClose?: () => void;
  role?: 'status' | 'alert' | 'dialog';
  autoClose?: number;
}

function showCard(o: CardOpts): HTMLElement {
  clearTimers();
  const close = h('button', { class: 'x', type: 'button', 'aria-label': t('close') }, icon('x', 18));
  close.addEventListener('click', o.onClose ?? closeCard);
  const el = h(
    'section',
    { class: 'card', role: o.role ?? 'dialog', 'aria-live': o.role === 'dialog' ? null : 'polite', 'aria-label': o.title },
    h('div', { class: 'card-top' }, o.badge, h('div', { class: 'card-body' }, h('h2', { dir: 'auto' }, o.title), o.body ? h('p', { dir: 'auto' }, o.body) : null), o.closable === false ? null : close),
    ...(o.extra ?? []),
  );
  if (o.actions?.length) {
    const row = h('div', { class: `actions${o.stack ? ' stack' : ''}` });
    for (const a of o.actions) {
      const b = h('button', { class: `btn ${a.cls}`, type: 'button' }, a.ic ? icon(a.ic, 18) : null, a.label);
      b.addEventListener('mousedown', (e) => e.preventDefault());
      b.addEventListener('click', a.run);
      row.append(b);
    }
    el.append(row);
  }
  el.addEventListener('mousedown', (e) => {
    if (!(e.target instanceof HTMLInputElement)) e.preventDefault(); // the page field keeps focus
  });
  if (cardEl) cardEl.replaceWith(el);
  else ensureLayer().append(el);
  cardEl = el;
  raise();
  if (o.autoClose) timers.push(window.setTimeout(closeCard, o.autoClose));
  return el;
}

function placeTarget(): void {
  if (!targetEl || !targetField) return;
  const r = targetField.getBoundingClientRect();
  Object.assign(targetEl.style, { left: `${r.left - 4}px`, top: `${r.top - 4}px`, width: `${r.width + 8}px`, height: `${r.height + 8}px` });
}

function setTarget(field: HTMLElement | null): void {
  targetField = field;
  if (!field) {
    targetEl?.remove();
    targetEl = null;
    return;
  }
  targetEl ??= h('div', { class: 'target', 'aria-hidden': 'true' });
  ensureLayer().prepend(targetEl);
  placeTarget();
}

function ring(expiresIn: number, total: number): HTMLElement {
  const svg = svgEl('svg', { viewBox: '0 0 48 48', width: 48, height: 48 });
  const c = (cls: string) => svgEl('circle', { cx: 24, cy: 24, r: 18, fill: 'none', 'stroke-width': 4, 'stroke-linecap': 'round', class: cls });
  svg.append(c('halo'), c('track'), c('prog'));
  const prog = svg.lastChild as SVGElement;
  // Drains clockwise over the 60 s, started where the device's clock already is (DESIGN §4.11).
  prog.setAttribute('style', `animation-duration:${total}ms;animation-delay:-${Math.max(0, total - expiresIn)}ms;--d:${total}ms`);
  return h('span', { class: 'ring' }, svg, h('span', { class: 'glyph' }, logo(24, false)));
}

const fmt = (ms: number) => {
  const s = Math.max(0, Math.ceil(ms / 1000));
  return `${Math.floor(s / 60)}:${String(s % 60).padStart(2, '0')}`;
};

interface PressOpts {
  chip: string;
  body: string;
  expiresIn: number;
  target: HTMLElement | null;
  mode: 'type' | 'save' | 'update';
  anyHost?: boolean;
}

function pressCard(o: PressOpts): void {
  const total = Math.max(60_000, o.expiresIn);
  const deadline = Date.now() + o.expiresIn;
  const rg = ring(o.expiresIn, total);
  const count = h('span', { class: 'count', 'aria-hidden': 'true' }, fmt(o.expiresIn));
  const sr = h('span', { class: 'sr', 'aria-live': 'polite' });
  const titleRow = h('div', { style: 'display:flex;gap:8px;align-items:baseline' });
  showCard({
    badge: rg,
    title: t('pressTitle'),
    body: o.body,
    closable: false,
    role: 'status',
    extra: [
      h('div', { style: 'display:flex;align-items:center;gap:8px' }, h('span', { class: 'chip' }, h('bdi', { dir: 'auto' }, o.chip)), count),
      o.anyHost ? h('div', { class: 'notice' }, icon('alert', 16), h('span', {}, t('pressFor', { host: pageHost() }))) : titleRow,
      h('p', { class: 'hint' }, t('pressHold')),
      sr,
    ],
    actions: [{ label: t('cancel'), cls: 'secondary', run: () => void cancelPress(o.mode) }],
  });
  setTarget(o.target);
  timers.push(
    window.setInterval(() => {
      const left = deadline - Date.now();
      count.textContent = fmt(left);
      const warn = left <= 10_000;
      count.classList.toggle('warn', warn);
      rg.classList.toggle('warn', warn);
      const s = Math.ceil(left / 1000);
      if (s === 30 || s === 10 || s === 5) sr.textContent = fmt(left);
    }, 250),
  );
  keepAlive();
  schedulePoll(o, 2000);
}

function keepAlive(): void {
  timers.push(window.setInterval(() => void send({ t: 'keepalive' }), 20_000));
}

function schedulePoll(o: PressOpts, ms: number): void {
  timers.push(window.setTimeout(() => void poll(o), ms));
}

async function poll(o: PressOpts): Promise<void> {
  const r = await send<{ status: Status }>({ t: 'status' });
  if (!cardEl) return;
  if (!r.ok) {
    if (r.code === 'rate_limited' || r.code === 'timeout' || r.code === 'unreachable') return schedulePoll(o, Math.max(2000, r.retryAfterMs ?? 0));
    return showError(r, retryFor(o));
  }
  const s = r.status;
  switch (s.state) {
    case 'armed':
      return schedulePoll(o, 2000);
    case 'waiting': {
      const h2 = cardEl.querySelector('h2');
      if (h2 && o.mode === 'type') h2.textContent = t('typing');
      return schedulePoll(o, 1000);
    }
    case 'typed':
      setTarget(null);
      return void showCard({ badge: badge('check', 'ok'), title: t('typedTitle'), body: t('typedBody'), role: 'status', autoClose: 1900 });
    case 'saved':
      return void showCard({
        badge: badge('check', 'ok'),
        title: o.mode === 'update' ? t('updatedTitle') : t('savedTitle'),
        body: o.mode === 'update' ? t('updatedBody') : t('savedBody'),
        role: 'status',
        autoClose: 2600,
      });
    case 'cancelled':
      return void showCard({ badge: badge('x', 'info'), title: t('cancelledTitle'), role: 'status', autoClose: 2400 });
    case 'expired':
      return void showCard({ badge: badge('clock', 'warn'), title: t('expiredTitle'), body: t('expiredBody'), role: 'alert', actions: retryActions(retryFor(o)) });
    case 'failed':
      return failedCard(s.code ?? 'failed', retryFor(o));
    default:
      return closeCard();
  }
}

function retryFor(o: PressOpts): (() => void) | undefined {
  if (o.mode !== 'type' || !flow) return undefined;
  const f = flow;
  return () => void arm(f.login, f.ctx, f.anyHost);
}

function retryActions(retry?: () => void) {
  return retry
    ? [
        { label: t('tryAgain'), cls: 'primary', run: retry },
        { label: t('close'), cls: 'secondary', run: closeCard },
      ]
    : [{ label: t('close'), cls: 'secondary', run: closeCard }];
}

function failedCard(code: string, retry?: () => void): void {
  const map: Record<string, [Key, Key, IconName]> = {
    no_usb: ['failNoUsbTitle', 'failNoUsbBody', 'plug'],
    no_host: ['failNoHostTitle', 'failNoHostBody', 'alert'],
    host_changed: ['failHostChangedTitle', 'failHostChangedBody', 'plug'],
    unsupported_char: ['failUnsupportedTitle', 'failUnsupportedBody', 'alert'],
  };
  const m = map[code];
  setTarget(null);
  showCard({
    badge: badge(m?.[2] ?? 'alert', code === 'no_usb' || code === 'host_changed' ? 'warn' : 'err'),
    title: m ? t(m[0]) : t('failTitle'),
    body: m ? t(m[1]) : t('failBody', { code }),
    role: 'alert',
    actions: retryActions(retry),
  });
}

async function cancelPress(mode: PressOpts['mode']): Promise<void> {
  clearTimers();
  await send({ t: 'cancel', mode });
  setTarget(null);
  showCard({ badge: badge('x', 'info'), title: t('cancelledTitle'), role: 'status', autoClose: 2000 });
}

interface ErrorViewT {
  icon: IconName;
  tone: 'warn' | 'err' | 'info';
  title: string;
  body: string;
  action?: { label: string; run: () => void };
}

function errorView(f: Failure): ErrorViewT {
  const openKeyra = { label: t('openKeyra'), run: () => void send({ t: 'openKeyra' }) };
  switch (f.code) {
    case 'locked':
      return { icon: 'lock', tone: 'warn', title: t('errLockedTitle'), body: t('errLockedBody'), action: openKeyra };
    case 'unreachable':
    case 'timeout':
    case 'not_keyra':
      return { icon: 'offline', tone: 'warn', title: t('errUnreachableTitle'), body: t('errUnreachableBody', { address: f.address ?? 'http://keyra.local' }) };
    case 'invalid_token':
      return { icon: 'shield', tone: 'err', title: t('errTokenTitle'), body: t('errTokenBody') };
    case 'forbidden':
      return { icon: 'shield', tone: 'err', title: t('errForbiddenTitle'), body: t('errForbiddenBody') };
    case 'busy':
      return { icon: 'clock', tone: 'warn', title: t('errBusyTitle'), body: t('errBusyBody') };
    case 'rate_limited':
      return { icon: 'clock', tone: 'warn', title: t('errRateTitle'), body: t('errRateBody', { s: Math.max(1, Math.ceil((f.retryAfterMs ?? 1000) / 1000)) }) };
    case 'not_found':
      return { icon: 'alert', tone: 'err', title: t('errNotFoundTitle'), body: t('errNotFoundBody') };
    case 'not_connected':
      return { icon: 'shield', tone: 'info', title: t('errNotConnectedTitle'), body: t('errNotConnectedBody') };
    default:
      return { icon: 'alert', tone: 'err', title: t('errTitle'), body: f.message ?? f.code };
  }
}

function showError(f: Failure, retry?: () => void): void {
  const v = errorView(f);
  setTarget(null);
  const actions = v.action ? [{ label: v.action.label, cls: 'primary', run: v.action.run, ic: 'open' as IconName }, { label: t('close'), cls: 'secondary', run: closeCard }] : retryActions(f.code === 'invalid_token' || f.code === 'forbidden' || f.code === 'not_connected' ? undefined : retry);
  showCard({ badge: badge(v.icon, v.tone), title: v.title, body: v.body, role: 'alert', actions });
}

// ---------- strong password for new-password fields ----------

async function fillGenerated(ctx: Ctx): Promise<void> {
  closeMenu();
  showCard({ badge: spinnerBadge(), title: t('menuLoading') });
  const r = await send<{ password: string }>({ t: 'generate' });
  if (!r.ok) return showError(r, () => void fillGenerated(ctx));
  const f = ctx.form;
  for (const el of [f.newPassword, f.confirm]) if (el) setValue(el, r.password);
  (f.newPassword ?? ctx.field).focus();
  showCard({ badge: badge('sparkles', 'ok'), title: t('generatedTitle'), body: t('generatedBody'), role: 'status', autoClose: 4000 });
}

// ---------- save or update ----------

let offered = '';

function siteTitle(): string {
  const meta = (sel: string) => document.querySelector<HTMLMetaElement>(sel)?.content?.trim();
  const name = meta('meta[property="og:site_name"]') || meta('meta[name="application-name"]') || pageHost();
  return Array.from(name).slice(0, 64).join('');
}

async function trigger(root: Element | null): Promise<void> {
  if (!config.connected || !root) return;
  forms = findForms(document);
  const form = forms.find((f) => f.root === root || f.root.contains(root) || root.contains(f.root));
  if (!form) return;
  if (form.kind === 'username') {
    const u = form.username?.value.trim();
    if (u) void send({ t: 'username', username: u });
    return;
  }
  const snap = snapshot(form);
  if (!snap) return;
  const key = `${snap.username}\u0000${snap.password}`;
  if (key === offered) return;
  offered = key;
  const r = await send<{ card: SaveCard | null }>({ t: 'offer', ...snap, title: siteTitle() });
  if (r.ok && r.card) showSave(r.card);
}

function showSave(card: SaveCard): void {
  const update = card.mode === 'update';
  showCard({
    badge: h('span', { class: 'badge-round tone-info' }, logo(28)),
    title: update ? t('updateTitle', { title: card.title }) : t('saveTitle'),
    body: update ? t('updateBody') : undefined,
    onClose: () => void decide('later'),
    extra: [
      h(
        'div',
        { class: 'kv' },
        h('div', {}, h('span', { class: 'k', title: t('saveSite') }, icon('globe', 16)), h('span', { class: 'v ltr', dir: 'ltr' }, card.host)),
        h('div', {}, h('span', { class: 'k', title: t('saveUser') }, icon('user', 16)), card.username ? h('span', { class: 'v ltr', dir: 'ltr' }, card.username) : h('span', { class: 'v none' }, t('saveNoUser'))),
      ),
    ],
    actions: [
      { label: update ? t('updateAction') : t('saveAction'), cls: 'primary', ic: 'save', run: () => void decide('save', card) },
      { label: t('notNow'), cls: 'secondary', run: () => void decide('later') },
      { label: t('never'), cls: 'ghost', run: () => void decide('never') },
    ],
  });
  keepAlive();
}

async function decide(decision: 'save' | 'later' | 'never', card?: SaveCard): Promise<void> {
  if (decision !== 'save' || !card) {
    closeCard();
    await send({ t: 'decide', decision });
    return;
  }
  showCard({ badge: spinnerBadge(), title: t('menuLoading') });
  const r = await send<{ expiresIn: number; mode: 'create' | 'update' }>({ t: 'decide', decision: 'save' });
  if (!r.ok) return showError(r);
  const update = r.mode === 'update';
  pressCard({ chip: `${update ? t('chipUpdate') : t('chipSave')} · ${card.title}`, body: t('pressSaveBody'), expiresIn: r.expiresIn, target: null, mode: update ? 'update' : 'save' });
}

// ---------- page wiring ----------

function ctxFor(el: EventTarget | null): Ctx | null {
  if (!(el instanceof HTMLInputElement)) return null;
  let r = roleOf(forms, el);
  if (!r) {
    forms = findForms(document);
    r = roleOf(forms, el);
  }
  return r && r.role !== 'confirm' ? { field: el, form: r.form, role: r.role } : null;
}

let frame = 0;
function relayout(): void {
  if (frame) return;
  frame = requestAnimationFrame(() => {
    frame = 0;
    if (keyCtx) {
      if (!keyCtx.field.isConnected || document.activeElement !== keyCtx.field) {
        if (!menuEl) hideKey();
      } else showKey(keyCtx);
    }
    placeMenu();
    placeTarget();
  });
}

let mutateTimer = 0;
function watch(): void {
  document.addEventListener(
    'focusin',
    (e) => {
      const ctx = ctxFor(e.target);
      if (menuCtx && e.target !== menuCtx.field) closeMenu();
      if (ctx) showKey(ctx);
      else if (!ours(e.target)) hideKey();
    },
    true,
  );
  document.addEventListener(
    'focusout',
    () => {
      setTimeout(() => {
        const a = document.activeElement;
        if (!menuEl && !ours(a) && keyCtx && a !== keyCtx.field) hideKey();
      }, 120);
    },
    true,
  );
  document.addEventListener(
    'pointerdown',
    (e) => {
      if (menuEl && !ours(e.target) && e.target !== menuCtx?.field) closeMenu();
    },
    true,
  );
  document.addEventListener(
    'keydown',
    (e) => {
      if (menuEl && menuCtx && e.target === menuCtx.field && menuKeys(e)) return;
      if (e.key === 'ArrowDown' && e.altKey && keyCtx && e.target === keyCtx.field) {
        e.preventDefault();
        void openMenu(keyCtx);
        return;
      }
      if (e.key === 'Enter' && e.target instanceof HTMLInputElement) {
        const f = forms.find((x) => x.root.contains(e.target as Node));
        if (f) void trigger(f.root);
      }
    },
    true,
  );
  document.addEventListener('submit', (e) => void trigger(e.target as Element), true);
  document.addEventListener(
    'click',
    (e) => {
      const el = (e.target as Element | null)?.closest?.('button, input[type=submit], input[type=button], input[type=image], [role=button], a');
      if (!el) return;
      const f = forms.find((x) => x.root.contains(el));
      if (f) void trigger(f.root);
    },
    true,
  );
  addEventListener('scroll', relayout, { capture: true, passive: true });
  addEventListener('resize', relayout, { passive: true });
  new MutationObserver(() => {
    clearTimeout(mutateTimer);
    mutateTimer = window.setTimeout(() => {
      forms = findForms(document);
      relayout();
    }, 250);
  }).observe(document.documentElement, { childList: true, subtree: true, attributes: true, attributeFilter: ['type', 'autocomplete', 'style', 'class', 'hidden'] });
}

ext.runtime.onMessage.addListener((msg: TabMsg, sender, sendResponse) => {
  if (sender.id !== ext.runtime.id || config.self) return false;
  if (msg.t === 'hello') {
    forms = findForms(document);
    sendResponse({ host: pageHost(), hasField: forms.some((f) => f.username || f.current || f.newPassword) } satisfies Hello);
    return false;
  }
  if (msg.t === 'arm') {
    forms = findForms(document);
    const focused = ctxFor(document.activeElement);
    const form = focused?.form ?? forms.find((f) => f.current) ?? forms.find((f) => f.username) ?? forms.find((f) => f.newPassword);
    if (!form) {
      sendResponse({ ok: false });
      return false;
    }
    const ctx: Ctx = focused ?? (form.username ? { field: form.username, form, role: 'username' } : form.current ? { field: form.current, form, role: 'current' } : { field: form.newPassword!, form, role: 'new' });
    choose(msg.login, ctx);
    sendResponse({ ok: true });
    return false;
  }
  return false;
});

async function loadConfig(): Promise<void> {
  const c = (await ext.runtime.sendMessage({ t: 'config' }).catch(() => null)) as PageConfig | null;
  if (c) config = c;
  if (!config.connected || !config.showIcons) hideKey();
}

async function start(): Promise<void> {
  await loadConfig();
  if (config.self) return; // Keyra's own web app
  ext.storage.onChanged.addListener((_c, area) => area === 'local' && void loadConfig());
  forms = findForms(document);
  watch();
  const ctx = ctxFor(document.activeElement);
  if (ctx) showKey(ctx);
  if (config.connected) {
    const r = await send<{ card: SaveCard | null }>({ t: 'pending' });
    if (r.ok && r.card) showSave(r.card);
  }
}

if (window.top === window && document.documentElement instanceof HTMLHtmlElement) void start();
