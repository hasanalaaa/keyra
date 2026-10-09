// Toolbar popup (SPEC §9.4): connection state, logins for this site, search over every login
// with "Type into this page", the generator and the settings; first run connects with one press.
import { normalizeAddress, type GenOptions } from '../api';
import { ext } from '../ext';
import { t, uiDir, uiLang, type Key } from '../i18n';
import type { Failure, Hello, Login, PopupState, Reach, Settings } from '../messages';
import { h, icon, logo, monogram, type IconName } from '../ui/dom';

type Reply<T> = ({ ok: true } & T) | Failure;
const send = async <T>(msg: object): Promise<T> => (await ext.runtime.sendMessage(msg)) as T;

const app = document.getElementById('app')!;
document.documentElement.lang = uiLang();
document.documentElement.dir = uiDir();

let state: PopupState;
let tab: 'logins' | 'gen' | 'settings' = 'logins';
let page: { tabId: number; host: string; hasField: boolean } | null = null;
let showPaste = false;
let flash = '';

const DEFAULT_ADDRESS = 'http://keyra.local';

async function currentPage(): Promise<typeof page> {
  // ?tab= lets the popup be opened as a page (tests, screenshots); normally it is the active tab.
  const fromUrl = Number(new URLSearchParams(location.search).get('tab'));
  const id = fromUrl || (await ext.tabs.query({ active: true, currentWindow: true }))[0]?.id;
  if (!id) return null;
  try {
    const hello = (await ext.tabs.sendMessage(id, { t: 'hello' })) as Hello | undefined;
    return hello?.host ? { tabId: id, host: hello.host, hasField: hello.hasField } : null;
  } catch {
    return null; // a browser page, the store, or a tab loaded before the extension
  }
}

const ERR: Partial<Record<Failure['code'], Key>> = {
  locked: 'errLockedTitle',
  unreachable: 'errUnreachableTitle',
  timeout: 'errUnreachableTitle',
  invalid_token: 'invalidTitle',
  forbidden: 'errForbiddenTitle',
  rate_limited: 'errRateTitle',
  busy: 'errBusyTitle',
};
const errText = (f: Failure) => t(ERR[f.code] ?? 'errTitle');

// ---------- shell ----------

const STATUS: Record<Reach, [Key, string]> = {
  ready: ['stReady', 'ok'],
  locked: ['stLocked', 'warn'],
  unreachable: ['stOffline', 'err'],
  not_keyra: ['stOffline', 'err'],
  uninitialized: ['stOffline', 'warn'],
  invalid_token: ['stNotConnected', 'err'],
  forbidden: ['stNotConnected', 'err'],
  not_connected: ['stNotConnected', 'muted'],
};

function header(): HTMLElement {
  const [k, tone] = STATUS[state.reach];
  return h(
    'header',
    { class: 'bar glass' },
    logo(28),
    h('span', { class: 'brand' }, 'Keyra'),
    h('span', { class: `pill pill-${tone}`, role: 'status', 'data-testid': 'reach' }, h('span', { class: 'dot' }), t(k)),
  );
}

function segmented(): HTMLElement {
  const tabs: [typeof tab, Key][] = [
    ['logins', 'tabLogins'],
    ['gen', 'tabGenerator'],
    ['settings', 'tabSettings'],
  ];
  const i = tabs.findIndex(([v]) => v === tab);
  const seg = h('div', { class: 'seg', role: 'tablist', style: `--i:${i}` }, h('span', { class: 'thumb', 'aria-hidden': 'true' }));
  for (const [v, k] of tabs) {
    seg.append(
      h(
        'button',
        {
          class: 'seg-item',
          type: 'button',
          role: 'tab',
          'aria-selected': String(v === tab),
          onclick: () => {
            tab = v;
            render();
          },
        },
        t(k),
      ),
    );
  }
  return seg;
}

function button(label: string, cls: string, run: () => void, ic?: IconName, attrs: Record<string, string> = {}): HTMLButtonElement {
  return h('button', { class: `btn ${cls}`, type: 'button', onclick: run, ...attrs }, ic ? icon(ic, 18) : null, label);
}

function notice(ic: IconName, tone: string, title: string, body: string, action?: HTMLElement): HTMLElement {
  return h(
    'div',
    { class: `notice tone-${tone}`, role: 'alert' },
    h('span', { class: 'n-icon' }, icon(ic, 20)),
    h('div', { class: 'n-text' }, h('strong', {}, title), h('p', {}, body), action ?? null),
  );
}

function render(): void {
  const body = h('main', { class: 'body' });
  if (!state.connected) body.append(connectView());
  else {
    body.append(segmented());
    body.append(tab === 'logins' ? loginsView() : tab === 'gen' ? genView() : settingsView());
  }
  app.replaceChildren(header(), body);
  if (flash) {
    app.append(h('div', { class: 'toast', role: 'status' }, icon('check', 18), flash));
    const f = flash;
    setTimeout(() => {
      if (flash === f) {
        flash = '';
        app.querySelector('.toast')?.remove();
      }
    }, 2400);
  }
}

async function refresh(): Promise<void> {
  state = await send<PopupState>({ t: 'popup' });
  render();
}

// ---------- connect (SPEC §9.4 pairing) ----------

function connectView(): HTMLElement {
  const wrap = h('section', { class: 'connect' });
  const addr = h('input', { class: 'field', id: 'address', type: 'url', dir: 'ltr', value: state.address ?? DEFAULT_ADDRESS, spellcheck: 'false', autocomplete: 'off', inputmode: 'url' });
  const err = h('p', { class: 'help err', role: 'alert', hidden: true });
  const fail = (k: Key, vars: Record<string, string> = {}) => {
    err.textContent = t(k, vars);
    err.hidden = false;
  };

  const connect = async () => {
    err.hidden = true;
    const address = normalizeAddress(addr.value);
    if (!address) return fail('addressBad');
    // Host permission for Keyra's address only (optional_host_permissions), asked in the click.
    const granted = await ext.permissions.request({ origins: [`${address}/*`] }).catch(() => false);
    if (!granted) return fail('connectNoPermission');
    btn.disabled = true;
    const r = await send<Reply<object>>({ t: 'pair', address });
    btn.disabled = false;
    if (!r.ok) {
      if (r.code === 'not_keyra' && r.message === 'uninitialized') return fail('connectUninit');
      if (r.code === 'forbidden') return fail('connectNoPermission');
      return fail('connectNotKeyra', { address });
    }
    state.pairing = true;
    render();
  };
  const btn = button(t('connectAction'), 'primary full', () => void connect(), 'shield', { 'data-testid': 'connect' });

  wrap.append(
    h('div', { class: 'hero' }, logo(56), h('h1', {}, t('connectTitle')), h('p', {}, t('connectBody'))),
    state.reach === 'invalid_token' ? notice('shield', 'err', t('invalidTitle'), t('invalidBody')) : '',
    h('label', { class: 'label', for: 'address' }, t('addressLabel')),
    addr,
    h('p', { class: 'help' }, t('addressHelp')),
    err,
  );
  if (state.pairing) {
    wrap.append(h('div', { class: 'waiting', role: 'status' }, h('span', { class: 'spin' }), t('connectWaiting')));
  }
  wrap.append(btn, h('p', { class: 'help center' }, t('connectSteps')));

  const toggle = h('button', { class: 'link', type: 'button', 'aria-expanded': String(showPaste) }, t('pasteToggle'));
  toggle.addEventListener('click', () => {
    showPaste = !showPaste;
    render();
  });
  wrap.append(h('div', { class: 'divider' }), toggle);
  if (showPaste) {
    const tok = h('input', { class: 'field mono-in', id: 'token', type: 'password', dir: 'ltr', autocomplete: 'off', spellcheck: 'false', placeholder: 'keyra_…' });
    const perr = h('p', { class: 'help err', role: 'alert', hidden: true });
    const save = async () => {
      perr.hidden = true;
      const address = normalizeAddress(addr.value);
      if (!address) return fail('addressBad');
      if (!/^keyra_[a-z2-7]{32}$/.test(tok.value.trim())) {
        perr.textContent = t('pasteBad');
        perr.hidden = false;
        return;
      }
      const granted = await ext.permissions.request({ origins: [`${address}/*`] }).catch(() => false);
      if (!granted) return fail('connectNoPermission');
      const r = await send<Reply<object>>({ t: 'paste', address, token: tok.value });
      if (!r.ok) {
        perr.textContent = r.code === 'forbidden' ? t('errForbiddenTitle') : r.code === 'invalid_token' ? t('invalidBody') : r.code === 'locked' ? t('errLockedBody') : t('connectNotKeyra', { address });
        perr.hidden = false;
        return;
      }
      flash = t('connectDone');
      showPaste = false;
      await refresh();
    };
    wrap.append(h('label', { class: 'label', for: 'token' }, t('pasteLabel')), tok, h('p', { class: 'help' }, t('pasteHelp')), perr, button(t('pasteAction'), 'secondary full', () => void save()));
  }
  return wrap;
}

// ---------- logins ----------

let all: Login[] | null = null;

function row(l: Login): HTMLElement {
  const typeIt = async () => {
    if (!page) return;
    const r = (await ext.tabs.sendMessage(page.tabId, { t: 'arm', login: l }).catch(() => null)) as { ok: boolean } | null;
    if (r?.ok) window.close();
    else {
      const n = app.querySelector('.type-error');
      if (n) n.removeAttribute('hidden');
    }
  };
  const el = h(
    'li',
    {},
    h(
      'button',
      { class: 'row', type: 'button', disabled: !page, title: t('typeHere'), 'data-id': l.id, onclick: () => void typeIt() },
      monogram(l.title, 36),
      h('span', { class: 'txt' }, h('bdi', { class: 't1', dir: 'auto' }, l.title), h('span', { class: 't2', dir: 'ltr' }, l.host || '—')),
      h('span', { class: 'type-chip' }, icon('keyboard', 16), h('span', { class: 'sr' }, t('typeHere'))),
    ),
  );
  return el;
}

function reachNotice(): HTMLElement | null {
  const address = state.address ?? '';
  switch (state.reach) {
    case 'ready':
      return null;
    case 'locked':
      return notice('lock', 'warn', t('errLockedTitle'), t('lockedBody'), button(t('openKeyra'), 'primary', () => void ext.tabs.create({ url: `${address}/` }), 'open'));
    case 'invalid_token':
    case 'forbidden':
      return notice('shield', 'err', t('invalidTitle'), t('invalidBody'), button(t('connectAgain'), 'primary', () => void disconnect()));
    case 'uninitialized':
      return notice('alert', 'warn', t('errUnreachableTitle'), t('connectUninit'), button(t('openKeyra'), 'secondary', () => void ext.tabs.create({ url: `${address}/` }), 'open'));
    default:
      return notice('offline', 'warn', t('errUnreachableTitle'), t('offlineBody', { address }), button(t('tryAgain'), 'secondary', () => void refresh(), 'refresh'));
  }
}

function loginsView(): HTMLElement {
  const v = h('section', { class: 'logins' });
  const n = reachNotice();
  if (n) {
    v.append(n);
    return v;
  }
  v.append(h('p', { class: 'help err type-error', role: 'alert', hidden: true }, t('noField')));
  const here = h('ul', { class: 'card rows', 'data-testid': 'here' }, h('li', { class: 'loading' }, h('span', { class: 'spin' })));
  v.append(h('h2', { class: 'section', dir: 'auto' }, page ? t('onThisSite', { host: page.host }) : t('tabLogins')), here);
  if (!page) here.replaceChildren(h('li', { class: 'empty' }, t('noPage')));
  else
    void send<Reply<{ entries: Login[] }>>({ t: 'popupMatch', host: page.host }).then((r) => {
      if (!r.ok) return here.replaceChildren(h('li', { class: 'empty' }, errText(r)));
      here.replaceChildren(...(r.entries.length ? r.entries.map(row) : [h('li', { class: 'empty' }, t('noLoginsHere'))]));
    });

  const q = h('input', { type: 'search', placeholder: t('menuSearch'), 'aria-label': t('menuSearch'), autocomplete: 'off', spellcheck: 'false', enterkeyhint: 'search' });
  const results = h('ul', { class: 'card rows', 'data-testid': 'results', hidden: true });
  const fold = (s: string) => s.toLowerCase().replace(/[ً-ْـ]/g, '').replace(/[أإآ]/g, 'ا').replace(/ى/g, 'ي').replace(/ة/g, 'ه');
  const show = () => {
    const s = fold(q.value.trim());
    if (!s || !all) {
      results.hidden = true;
      return;
    }
    const hits = all.filter((l) => fold(l.title).includes(s) || l.host.includes(s)).slice(0, 30);
    results.hidden = false;
    results.replaceChildren(...(hits.length ? hits.map(row) : [h('li', { class: 'empty' }, t('menuNoResults'))]));
  };
  q.addEventListener('focus', async () => {
    if (all) return;
    const r = await send<Reply<{ entries: Login[] }>>({ t: 'popupEntries' });
    all = r.ok ? [...r.entries].sort((a, b) => a.title.localeCompare(b.title)) : [];
    show();
  });
  q.addEventListener('input', show);
  v.append(h('h2', { class: 'section' }, t('allLogins')), h('label', { class: 'search' }, icon('search', 18), q), results);
  v.append(h('p', { class: 'foot' }, page ? t('menuFoot') : ''));
  return v;
}

// ---------- generator ----------

let lastGen: { password: string; entropyBits: number } | null = null;

function colored(pw: string): HTMLElement {
  const out = h('p', { class: 'pw', dir: 'ltr', 'data-testid': 'generated' });
  for (const ch of pw) out.append(h('span', { class: /\d/.test(ch) ? 'd' : /[A-Za-z]/.test(ch) ? '' : 's' }, ch));
  return out;
}

function genView(): HTMLElement {
  const v = h('section', { class: 'gen' });
  const n = reachNotice();
  if (n) {
    v.append(n);
    return v;
  }
  const gen: GenOptions = { ...state.settings.gen };
  const preview = h('div', { class: 'preview card' }, lastGen ? colored(lastGen.password) : h('span', { class: 'spin' }));
  const bits = h('p', { class: 'bits' }, lastGen ? t('genBits', { bits: lastGen.entropyBits }) : '');
  const run = async () => {
    preview.classList.add('dim');
    const r = await send<Reply<{ password: string; entropyBits: number }>>({ t: 'popupGenerate', gen });
    preview.classList.remove('dim');
    if (!r.ok) {
      preview.replaceChildren(h('span', { class: 'help err' }, errText(r)));
      return;
    }
    lastGen = { password: r.password, entropyBits: r.entropyBits };
    state.settings.gen = { ...gen };
    preview.replaceChildren(colored(r.password));
    bits.textContent = t('genBits', { bits: r.entropyBits });
  };
  const copyBtn = button(t('genCopy'), 'secondary', async () => {
    if (!lastGen) return;
    await navigator.clipboard.writeText(lastGen.password).catch(() => undefined);
    copyBtn.replaceChildren(icon('check', 18), t('genCopied'));
    setTimeout(() => copyBtn.replaceChildren(icon('copy', 18), t('genCopy')), 1400);
  }, 'copy');
  const len = h('input', { type: 'range', min: 8, max: 64, value: gen.length, 'aria-label': t('genLength') });
  const lenVal = h('output', { class: 'mono-num' }, String(gen.length));
  let timer = 0;
  len.addEventListener('input', () => {
    gen.length = Number(len.value);
    lenVal.textContent = len.value;
    clearTimeout(timer);
    timer = window.setTimeout(() => void run(), 250);
  });
  const sw = (label: string, on: boolean, set: (v: boolean) => void) => {
    const input = h('input', { type: 'checkbox', role: 'switch', checked: on });
    input.addEventListener('change', () => {
      set(input.checked);
      void run();
    });
    return h('label', { class: 'set-row' }, h('span', {}, label), input);
  };
  v.append(
    preview,
    bits,
    h('div', { class: 'row-btns' }, button(t('genNew'), 'secondary', () => void run(), 'refresh'), copyBtn),
    h(
      'div',
      { class: 'card list' },
      h('div', { class: 'set-row' }, h('span', {}, t('genLength')), h('span', { class: 'len' }, len, lenVal)),
      sw(t('genDigits'), gen.digits, (x) => {
        gen.digits = x;
        gen.minDigits = x ? 1 : 0;
      }),
      sw(t('genSymbols'), gen.symbols, (x) => {
        gen.symbols = x;
        gen.minSymbols = x ? 1 : 0;
      }),
    ),
    h('p', { class: 'foot' }, t('genNote')),
  );
  if (!lastGen) void run();
  return v;
}

// ---------- settings ----------

async function setSettings(patch: Partial<Settings>): Promise<void> {
  await send({ t: 'settings', patch });
  state.settings = { ...state.settings, ...patch };
}

async function disconnect(): Promise<void> {
  await send({ t: 'disconnect' });
  all = null;
  await refresh();
}

function settingsView(): HTMLElement {
  const s = state.settings;
  const sw = (label: string, on: boolean, key: 'showIcons' | 'offerSave') => {
    const input = h('input', { type: 'checkbox', role: 'switch', checked: on, 'data-testid': key });
    input.addEventListener('change', () => void setSettings({ [key]: input.checked }));
    return h('label', { class: 'set-row' }, h('span', {}, label), input);
  };
  const never = h('ul', { class: 'card rows', 'data-testid': 'never' });
  const fill = () =>
    never.replaceChildren(
      ...(state.settings.never.length
        ? state.settings.never.map((host) =>
            h(
              'li',
              { class: 'set-row' },
              h('span', { dir: 'ltr', class: 'host' }, host),
              h(
                'button',
                {
                  class: 'icon-btn',
                  type: 'button',
                  'aria-label': t('setRemove', { host }),
                  onclick: async () => {
                    await setSettings({ never: state.settings.never.filter((x) => x !== host) });
                    fill();
                  },
                },
                icon('x', 18),
              ),
            ),
          )
        : [h('li', { class: 'empty' }, t('setNeverNone'))]),
    );
  fill();
  return h(
    'section',
    { class: 'settings' },
    h('div', { class: 'card list' }, h('div', { class: 'set-row' }, h('span', {}, t('setAddress')), h('span', { class: 'value', dir: 'ltr' }, state.address ?? '')), sw(t('setIcons'), s.showIcons, 'showIcons'), sw(t('setSave'), s.offerSave, 'offerSave')),
    h('h2', { class: 'section' }, t('setNever')),
    never,
    button(t('setDisconnect'), 'danger full', () => void disconnect(), 'trash', { 'data-testid': 'disconnect' }),
    h('p', { class: 'foot' }, t('setDisconnectNote')),
    h('p', { class: 'foot' }, t('setPrivacy')),
    h('p', { class: 'foot center' }, t('version', { v: ext.runtime.getManifest().version })),
  );
}

// ---------- start ----------

ext.storage.onChanged.addListener((changes, area) => {
  if (area === 'local' && changes.token) {
    if (changes.token.newValue && !changes.token.oldValue) flash = t('connectDone');
    void refresh();
  }
});

void (async () => {
  [page] = await Promise.all([currentPage()]);
  state = await send<PopupState>({ t: 'popup' });
  render();
})();
