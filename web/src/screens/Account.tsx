// Account detail (DESIGN §5.5): type actions → Ready, copy, reveal, TOTP, favorite, edit.
import { useEffect, useRef, useState } from 'preact/hooks';
import { Icon } from '../components/Icon';
import { Button, ColoredSecret, CopyButton, IconButton, MiniRing, Monogram } from '../components/ui';
import { ErrorCard, Ready, UsbNotice } from '../components/Ready';
import { ApiError, api } from '../lib/api';
import { useTypeAction, type ErrorCode } from '../lib/actions';
import { copyText } from '../lib/clipboard';
import { t, type Key } from '../lib/i18n';
import { go } from '../lib/router';
import { setState, toast, useApp } from '../lib/store';
import { hostOf } from '../lib/csv';
import type { Entry, Totp, TypeWhat } from '../lib/types';

const CHIP: Record<TypeWhat | 'test', Key> = {
  username: 'chipUsername',
  password: 'chipPassword',
  both: 'chipBoth',
  totp: 'chipCode',
  test: 'typeTest',
};

export function AccountView({ id, mode }: { id: number; mode: 'sheet' | 'pane' }) {
  const app = useApp();
  const summary = app.entries?.find((e) => e.id === id) ?? null;
  const [entry, setEntry] = useState<Entry | null>(null);
  const [missing, setMissing] = useState(false);
  const action = useTypeAction(id);
  const totpRef = useRef<Totp | null>(null);

  useEffect(() => {
    let live = true;
    api
      .entry(id)
      .then((e) => live && setEntry(e))
      .catch((e) => live && e instanceof ApiError && e.status === 404 && setMissing(true));
    // Secrets live only in this component's state, so they are gone when the sheet closes.
    return () => {
      live = false;
    };
  }, [id, summary?.updated]);

  const e = summary
    ? summary
    : entry
      ? { ...entry, hasPassword: entry.password !== '', hasTotp: entry.totp !== '' }
      : null;
  if (missing || !e) return missing ? <p class="callout center pad">{t('genericError')}</p> : <div class="detail-skel" />;

  const host = hostOf(e.url);
  const toggleFav = async () => {
    const fav = !e.favorite;
    const prev = app.entries;
    setState({ entries: prev?.map((x) => (x.id === id ? { ...x, favorite: fav } : x)) ?? null });
    if (entry) setEntry({ ...entry, favorite: fav });
    try {
      await api.update(id, { favorite: fav });
    } catch {
      setState({ entries: prev });
      toast(t('saveError'), 'error');
    }
  };

  const valueFor = (what: TypeWhat | 'test'): string => {
    if (!entry) return '';
    if (what === 'username') return entry.username;
    if (what === 'totp') return totpRef.current?.code ?? '';
    return entry.password;
  };

  const phase = action.phase;
  const what = action.what ?? 'both';
  let area;
  if (phase.kind === 'ready' || phase.kind === 'typing' || phase.kind === 'typed') {
    area = (
      <Ready
        state={phase.kind}
        deadline={phase.kind === 'ready' ? phase.deadline : 0}
        total={phase.kind === 'ready' ? phase.total : 60000}
        title={t('readyTitle')}
        body={t('readyBody')}
        chip={
          <>
            {t(CHIP[what])} · <bdi>{e.title}</bdi>
          </>
        }
        notice={app.device && !app.device.host.usb ? <UsbNotice /> : undefined}
        onCancel={() => void action.cancel()}
      />
    );
  } else if (phase.kind === 'error') {
    area = <ActionError code={phase.code} retry={() => void action.start(what as TypeWhat)} copy={() => copyValue(valueFor(what))} edit={() => go(`/a/${id}/edit`)} close={action.dismiss} />;
  } else {
    area = (
      <div class="actions">
        <button type="button" class="act-both" disabled={!e.hasPassword} onClick={() => void action.start('both')}>
          <span class="both-icons" aria-hidden="true">
            <Icon name="user" size={24} />
            <Icon name="key-round" size={24} />
          </span>
          <span class="act-text">
            <span class="act-label">{t('both')}</span>
            <span class="act-sub">{t('bothSub')}</span>
          </span>
        </button>
        <div class="act-pair">
          <ActionTile icon="user" label={t('username')} disabled={!e.username} onType={() => void action.start('username')} copy={() => entry?.username ?? ''} />
          <ActionTile icon="key-round" label={t('password')} disabled={!e.hasPassword} onType={() => void action.start('password')} copy={() => entry?.password ?? ''} />
        </div>
        {e.hasTotp && <CodeCard id={id} onType={() => void action.start('totp')} codeRef={totpRef} timeValid={app.device?.timeValid ?? true} />}
        <p class="helper-line">{e.hasPassword ? t('helper') : t('noPassword')}</p>
      </div>
    );
  }

  return (
    <div class={`account account-${mode}`}>
      <header class="acc-head">
        <Monogram title={e.title} size={64} />
        <div class="acc-head-text">
          <h2 class="t2 acc-title" dir="auto">
            {e.title}
          </h2>
          {host && (
            <p class="callout acc-host" dir="ltr">
              {host}
            </p>
          )}
        </div>
        <div class="acc-tools">
          <IconButton icon="star" label={t('favorite')} pressed={e.favorite} onClick={() => void toggleFav()} class="star-btn" />
          <IconButton icon="pencil" label={t('edit')} onClick={() => go(`/a/${id}/edit`)} />
        </div>
      </header>
      <div class="action-area">{area}</div>
      <Details entry={entry} />
    </div>
  );
}

function copyValue(v: string): void {
  if (v && copyText(v)) toast(t('copied'), 'ok');
  else toast(t('genericError'), 'error');
}

function ActionTile({ icon, label, disabled, onType, copy }: { icon: 'user' | 'key-round'; label: string; disabled: boolean; onType: () => void; copy: () => string }) {
  return (
    <div class="act-tile-wrap">
      <button type="button" class="act-tile" disabled={disabled} onClick={onType}>
        <Icon name={icon} size={28} />
        <span class="act-label">{label}</span>
      </button>
      {!disabled && <CopyButton value={copy} label={`${t('copy')} · ${label}`} class="tile-copy" />}
    </div>
  );
}

function ActionError({ code, retry, copy, edit, close }: { code: ErrorCode; retry: () => void; copy: () => void; edit: () => void; close: () => void }) {
  if (code === 'no_usb')
    return <ErrorCard icon="usb" tone="warn" title={t('errNoUsbTitle')} body={t('errNoUsbBody')} primary={{ label: t('tryAgain'), run: retry }} ghost={{ label: t('copyInstead'), run: copy }} />;
  if (code === 'expired')
    return <ErrorCard icon="clock" tone="warn" title={t('errExpiredTitle')} body={t('errExpiredBody')} primary={{ label: t('tryAgain'), run: retry }} ghost={{ label: t('close'), run: close }} />;
  if (code === 'unsupported_char')
    return <ErrorCard icon="triangle-alert" tone="err" title={t('errUnsupportedTitle')} body={t('errUnsupportedBody')} primary={{ label: t('copyPassword'), run: copy }} ghost={{ label: t('editAccount'), run: edit }} />;
  return <ErrorCard icon="triangle-alert" tone="err" title={t('errFailedTitle')} body={t('errFailedBody')} primary={{ label: t('tryAgain'), run: retry }} ghost={{ label: t('close'), run: close }} />;
}

function CodeCard({ id, onType, codeRef, timeValid }: { id: number; onType: () => void; codeRef: { current: Totp | null }; timeValid: boolean }) {
  const [totp, setTotp] = useState<Totp | null>(null);
  const [noTime, setNoTime] = useState(false);
  const [left, setLeft] = useState(0);

  useEffect(() => {
    let live = true;
    let h: ReturnType<typeof setTimeout>;
    const load = async () => {
      try {
        const r = await api.totp(id);
        if (!live) return;
        setTotp(r);
        codeRef.current = r;
        setNoTime(false);
        setLeft(r.remaining);
        h = setTimeout(load, r.remaining * 1000 + 200);
      } catch (e) {
        if (!live) return;
        if (e instanceof ApiError && e.code === 'no_time') setNoTime(true);
        else h = setTimeout(load, 3000);
      }
    };
    void load();
    const tick = setInterval(() => setLeft((n) => Math.max(0, n - 1)), 1000);
    return () => {
      live = false;
      clearTimeout(h);
      clearInterval(tick);
      codeRef.current = null;
    };
  }, [id, timeValid]);

  if (noTime) return <div class="code-card"><p class="caption">{t('noClock')}</p></div>;
  const code = totp?.code ?? '';
  const pretty = code.length === 6 ? `${code.slice(0, 3)} ${code.slice(3)}` : code.length === 8 ? `${code.slice(0, 4)} ${code.slice(4)}` : code;
  return (
    <div class={`code-card${left <= 5 && totp ? ' warn' : ''}`}>
      <div class="code-main">
        <button type="button" class="code mono" dir="ltr" aria-label={`${t('copy')} ${code}`} onClick={() => copyValue(code)}>
          {pretty || '––– –––'}
        </button>
        <span class="code-meta">
          {totp && <MiniRing remaining={left} period={totp.period} />}
          <span class="caption">{t('codeCard', { n: left })}</span>
        </span>
      </div>
      <div class="code-tools">
        <Button variant="tinted" size="sm" onClick={onType}>
          {t('type')}
        </Button>
        <CopyButton value={() => code} />
      </div>
    </div>
  );
}

function Details({ entry }: { entry: Entry | null }) {
  const [shown, setShown] = useState(false);
  const [notesOpen, setNotesOpen] = useState(false);
  useEffect(() => {
    if (!shown) return;
    const h = setTimeout(() => setShown(false), 30000);
    return () => clearTimeout(h);
  }, [shown]);
  if (!entry) return null;
  return (
    <div class="card details">
      {entry.username && (
        <div class="kv-row">
          <span class="kv-label">{t('username')}</span>
          <span class="kv-value mono" dir="ltr">
            {entry.username}
          </span>
          <CopyButton value={() => entry.username} />
        </div>
      )}
      {entry.password && (
        <div class="kv-row">
          <span class="kv-label">{t('password')}</span>
          <span class="kv-value" dir="ltr">
            {shown ? <ColoredSecret value={entry.password} /> : <span class="masked">••••••••••</span>}
          </span>
          <IconButton icon={shown ? 'eye-off' : 'eye'} label={shown ? t('hidePassword') : t('showPassword')} pressed={shown} onClick={() => setShown(!shown)} />
          <CopyButton value={() => entry.password} />
        </div>
      )}
      {entry.url && (
        <div class="kv-row">
          <span class="kv-label">{t('website')}</span>
          <span class="kv-value" dir="ltr">
            {entry.url}
          </span>
          <CopyButton value={() => entry.url} />
        </div>
      )}
      {entry.notes && (
        <div class="kv-row kv-notes">
          <span class="kv-label">{t('notes')}</span>
          <p class={`notes${notesOpen ? ' open' : ''}`} dir="auto">
            {entry.notes}
          </p>
          {!notesOpen && entry.notes.split('\n').length + entry.notes.length / 60 > 3 && (
            <button type="button" class="link-btn" onClick={() => setNotesOpen(true)}>
              {t('showMore')}
            </button>
          )}
        </div>
      )}
    </div>
  );
}

