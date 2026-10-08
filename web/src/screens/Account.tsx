// Account detail (DESIGN §5.5): type actions → Ready, copy, reveal, TOTP, favorite, edit.
import { useEffect, useRef, useState } from 'preact/hooks';
import { Icon } from '../components/Icon';
import { Button, ColoredSecret, CopyButton, IconButton, MiniRing, Monogram, Segmented } from '../components/ui';
import { ErrorCard, HostNotice, Ready, readyText } from '../components/Ready';
import { HostLangRow } from '../components/HostOs';
import { SequencePreview } from '../components/SequenceEditor';
import { parseSequence } from '../lib/sequence';
import { osOf, resolveTarget } from '../lib/hostos';
import { defaultTarget, deviceLabel, storeTarget, storedTarget, validTarget } from '../lib/ble';
import { ApiError, api } from '../lib/api';
import { usePressGate, useTypeAction, type ErrorCode } from '../lib/actions';
import { errorText, isLockedError } from '../lib/errors';
import { copyText } from '../lib/clipboard';
import { t, type Key } from '../lib/i18n';
import { go, replace } from '../lib/router';
import { knownSequences, setState, toast, useApp } from '../lib/store';
import { hostOf } from '../lib/csv';
import { shortDate } from '../lib/wifi';
import type { Entry, OldPassword, Totp, TypeWhat } from '../lib/types';

const CHIP: Record<TypeWhat | 'test' | 'text', Key> = {
  username: 'chipUsername',
  password: 'chipPassword',
  both: 'chipBoth',
  totp: 'chipCode',
  sequence: 'chipSequence',
  test: 'typeTest',
  text: 'chipText',
};

export function AccountView({ id, mode }: { id: number; mode: 'sheet' | 'pane' }) {
  const app = useApp();
  const summary = app.entries?.find((e) => e.id === id) ?? null;
  const [entry, setEntry] = useState<Entry | null>(null);
  const [missing, setMissing] = useState(false);
  const action = useTypeAction(id);
  const [, setPicked] = useState(0); // re-render after the picker stores a choice
  const chosen = validTarget(storedTarget(), app.ble);
  const hostTarget = resolveTarget(chosen, app.device?.host.output ?? null, app.ble);
  const startAction = (what: TypeWhat) => void action.start(what, chosen ?? undefined);
  const totpRef = useRef<Totp | null>(null);
  // SPEC §12.3: secrets arrive only after a press of Keyra's button (then 60 s without one).
  const gate = usePressGate('reveal');
  const reveal = async (): Promise<Entry | null> => {
    if (entry?.revealed) return entry;
    try {
      const full = await gate.run(() => api.reveal(id));
      if (full) setEntry(full);
      return full;
    } catch (err) {
      if (!isLockedError(err)) toast(errorText(err), 'error');
      return null;
    }
  };
  /** Copy needs a fresh tap (no clipboard after an async wait), so a press only reveals. */
  const secretCopy = (get: (x: Entry) => string | undefined) =>
    entry?.revealed ? { copy: () => get(entry) ?? '' } : { reveal: () => void reveal().then((x) => x && toast(t('revealedTapCopy'), 'ok')) };

  // SPEC §16: an account set to delete itself is gone right after its last typing.
  const burnRef = useRef(0);
  burnRef.current = entry?.burnAfter ?? summary?.burnAfter ?? burnRef.current;
  useEffect(() => {
    // Already gone from the refreshed list: no need to ask the device.
    if (!summary && app.entries && burnRef.current > 0) {
      toast(t('burnDone'), 'ok');
      replace('/');
      return;
    }
    let live = true;
    api
      .entry(id)
      .then((e) => live && setEntry(e))
      .catch((e) => {
        if (!live || !(e instanceof ApiError && e.status === 404)) return;
        if (burnRef.current > 0) {
          toast(t('burnDone'), 'ok');
          replace('/');
        } else setMissing(true);
      });
    // Secrets live only in this component's state, so they are gone when the sheet closes.
    return () => {
      live = false;
    };
  }, [id, summary?.updated]);

  // SPEC §10.4: `sequence` comes only with the secrets; remember what a revealed copy said.
  useEffect(() => {
    if (entry?.sequence !== undefined) knownSequences.set(id, entry.sequence !== '');
  }, [id, entry?.sequence]);
  const ownSeq = entry?.sequence !== undefined ? entry.sequence !== '' : (knownSequences.get(id) ?? false);
  const parsedSeq = entry?.sequence ? parseSequence(entry.sequence) : null;
  const seqParts = parsedSeq?.ok ? parsedSeq.parts : 1;
  // A custom "Both" order is typed as a sequence (the entry's own sequence, if any, comes first).
  const bothWhat: TypeWhat = app.bothSequence ? 'sequence' : 'both';
  const pending = app.device?.pending;
  const seqPending = pending && pending.what === 'sequence' && pending.id === id ? pending : null;

  const e = summary
    ? summary
    : entry
      ? entry
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

  const valueFor = (what: TypeWhat | 'test' | 'text'): string => {
    if (!entry) return '';
    if (what === 'username') return entry.username;
    if (what === 'totp') return totpRef.current?.code ?? '';
    return entry.password ?? '';
  };
  const copyOrReveal = (what: TypeWhat | 'test' | 'text') => {
    if (what === 'username' || what === 'totp' || entry?.revealed) copyValue(valueFor(what));
    else void reveal().then((x) => x && toast(t('revealedTapCopy'), 'ok'));
  };

  const phase = action.phase;
  const what = action.what ?? 'both';
  let area;
  if (gate.phase.kind === 'ready') {
    area = (
      <Ready
        state="ready"
        deadline={gate.phase.deadline}
        total={gate.phase.total}
        title={t('revealTitle')}
        body={t('revealBody')}
        chip={<bdi>{e.title}</bdi>}
        onCancel={gate.cancel}
      />
    );
  } else if (phase.kind === 'ready' || phase.kind === 'typing' || phase.kind === 'typed') {
    // A sequence with {PRESS} types one part per press (SPEC §10.4): say which one is next.
    const parts = seqPending?.parts ?? 1;
    const part = seqPending?.part ?? 1;
    const body = parts > 1 ? (part > 1 ? t('seqReadyNext', { i: part, n: parts }) : t('seqReadyFirst', { n: parts })) : t('readyBody');
    area = (
      <Ready
        state={phase.kind}
        deadline={phase.kind === 'ready' ? phase.deadline : 0}
        total={phase.kind === 'ready' ? phase.total : 60000}
        {...readyText(app.device, body)}
        chip={
          <>
            {t(what === 'sequence' && !ownSeq ? 'chipBoth' : CHIP[what])} · <bdi>{e.title}</bdi>
          </>
        }
        notice={
          app.device ? (
            <>
              {seqPending?.preview && <SequencePreview preview={seqPending.preview} current={parts > 1 ? part : undefined} />}
              <HostNotice device={app.device} />
            </>
          ) : undefined
        }
        onCancel={() => void action.cancel()}
      />
    );
  } else if (phase.kind === 'error') {
    area = <ActionError code={phase.code} retry={() => startAction(what as TypeWhat)} copy={() => copyOrReveal(what)} edit={() => go(`/a/${id}/edit`)} close={action.dismiss} />;
  } else {
    area = (
      <div class="actions">
        {app.ble?.enabled && app.ble.bonds.length > 0 && (
          <div class="target-picker">
            <span class="caption">{t('typeInto')}</span>
            <Segmented<string>
              label={t('typeInto')}
              options={[{ value: 'usb', label: 'USB' }, ...app.ble.bonds.map((b) => ({ value: b.addr, label: deviceLabel(b, t('bleDevice')) }))]}
              value={chosen ?? defaultTarget(app.device?.host.output ?? null, app.ble)}
              onChange={(v) => {
                storeTarget(v);
                setPicked((n) => n + 1);
              }}
            />
          </div>
        )}
        <HostLangRow key={hostTarget ?? ''} target={hostTarget} os={osOf(hostTarget, app.device?.host.usbOs, app.ble)} />
        <button type="button" class="act-both" disabled={!e.hasPassword || !e.username} onClick={() => startAction(bothWhat)}>
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
          <ActionTile icon="user" label={t('username')} disabled={!e.username} onType={() => startAction('username')} copy={() => entry?.username ?? ''} />
          <ActionTile icon="key-round" label={t('password')} disabled={!e.hasPassword} onType={() => startAction('password')} {...secretCopy((x) => x.password)} />
        </div>
        {ownSeq && (
          <button type="button" class="act-seq" onClick={() => startAction('sequence')}>
            <Icon name="keyboard" size={24} />
            <span class="act-text">
              <span class="act-label">{t('typeSequence')}</span>
              <span class="act-sub">{seqParts > 1 ? t('typeSequenceParts', { n: seqParts }) : t('typeSequenceSub')}</span>
            </span>
          </button>
        )}
        {e.hasTotp && <CodeCard id={id} onType={() => startAction('totp')} codeRef={totpRef} timeValid={app.device?.timeValid ?? true} />}
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
      <Details entry={entry} reveal={reveal} />
      {entry && entry.history?.length > 0 && <History entry={entry} lang={app.lang} reveal={reveal} />}
    </div>
  );
}

function copyValue(v: string): void {
  if (v && copyText(v)) toast(t('copied'), 'ok');
  else toast(t('genericError'), 'error');
}

function ActionTile({ icon, label, disabled, onType, copy, reveal }: { icon: 'user' | 'key-round'; label: string; disabled: boolean; onType: () => void; copy?: () => string; reveal?: () => void }) {
  return (
    <div class="act-tile-wrap">
      <button type="button" class="act-tile" disabled={disabled} onClick={onType}>
        <Icon name={icon} size={28} />
        <span class="act-label">{label}</span>
      </button>
      {!disabled && <SecretCopy copy={copy} reveal={reveal} label={`${t('copy')} · ${label}`} class="tile-copy" />}
    </div>
  );
}

/** Copy when the value is here; otherwise the same-looking button asks Keyra's button to reveal it first. */
function SecretCopy({ copy, reveal, label, class: cls }: { copy?: () => string; reveal?: () => void; label?: string; class?: string }) {
  if (copy) return <CopyButton value={copy} label={label} class={cls} />;
  return (
    <button
      type="button"
      class={`icon-btn copy-btn ${cls ?? ''}`}
      aria-label={label ?? t('copy')}
      title={label ?? t('copy')}
      onClick={(ev) => {
        ev.stopPropagation();
        reveal?.();
      }}
    >
      <Icon name="copy" size={20} />
    </button>
  );
}

function ActionError({ code, retry, copy, edit, close }: { code: ErrorCode; retry: () => void; copy: () => void; edit: () => void; close: () => void }) {
  if (code === 'no_usb')
    return <ErrorCard icon="usb" tone="warn" title={t('errNoUsbTitle')} body={t('errNoUsbBody')} primary={{ label: t('tryAgain'), run: retry }} ghost={{ label: t('copyInstead'), run: copy }} />;
  if (code === 'no_host')
    return <ErrorCard icon="bluetooth" tone="warn" title={t('errNoHostTitle')} body={t('errNoHostBody')} primary={{ label: t('tryAgain'), run: retry }} ghost={{ label: t('copyInstead'), run: copy }} />;
  if (code === 'expired')
    return <ErrorCard icon="clock" tone="warn" title={t('errExpiredTitle')} body={t('errExpiredBody')} primary={{ label: t('tryAgain'), run: retry }} ghost={{ label: t('close'), run: close }} />;
  if (code === 'host_changed')
    return <ErrorCard icon="usb" tone="warn" title={t('errHostChangedTitle')} body={t('errHostChangedBody')} primary={{ label: t('tryAgain'), run: retry }} ghost={{ label: t('close'), run: close }} />;
  if (code === 'unsupported_char')
    return <ErrorCard icon="triangle-alert" tone="err" title={t('errUnsupportedTitle')} body={t('errUnsupportedBody')} primary={{ label: t('copyPassword'), run: copy }} ghost={{ label: t('editAccount'), run: edit }} />;
  return <ErrorCard icon="triangle-alert" tone="err" title={t('errFailedTitle')} body={t('errFailedBody')} primary={{ label: t('tryAgain'), run: retry }} ghost={{ label: t('close'), run: close }} />;
}

function CodeCard({ id, onType, codeRef, timeValid }: { id: number; onType: () => void; codeRef: { current: Totp | null }; timeValid: boolean }) {
  const [totp, setTotp] = useState<Totp | null>(null);
  const [noTime, setNoTime] = useState(false);
  const [bad, setBad] = useState(false); // the stored 2FA secret cannot make codes
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
        // A secret the device rejects stays rejected: say so instead of retrying forever.
        else if (e instanceof ApiError && e.code === 'invalid') setBad(true);
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
  if (bad) return <div class="code-card"><p class="caption">{t('totpBroken')}</p></div>;
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

function Details({ entry, reveal }: { entry: Entry | null; reveal: () => Promise<Entry | null> }) {
  const [shown, setShown] = useState(false);
  const [notesOpen, setNotesOpen] = useState(false);
  useEffect(() => {
    if (!shown) return;
    const h = setTimeout(() => setShown(false), 30000);
    return () => clearTimeout(h);
  }, [shown]);
  if (!entry) return null;
  return (
    <>
    {(entry.burnAfter ?? 0) > 0 && (
      <p class="callout burn-note" role="note">
        <Icon name="trash-2" size={16} />
        {entry.burnAfter === 1 ? t('burnLast') : t('burnLeft', { n: entry.burnAfter ?? 0 })}
      </p>
    )}
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
      {entry.hasPassword && (
        <div class="kv-row">
          <span class="kv-label">{t('password')}</span>
          <span class="kv-value" dir="ltr">
            {shown && entry.password ? <ColoredSecret value={entry.password} /> : <span class="masked">••••••••••</span>}
          </span>
          <IconButton
            icon={shown ? 'eye-off' : 'eye'}
            label={shown ? t('hidePassword') : t('showPassword')}
            pressed={shown}
            onClick={() => (shown || entry.revealed ? setShown(!shown) : void reveal().then((x) => x && setShown(true)))}
          />
          <SecretCopy
            copy={entry.revealed ? () => entry.password ?? '' : undefined}
            reveal={() => void reveal().then((x) => x && toast(t('revealedTapCopy'), 'ok'))}
          />
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
          <p class={`notes${notesOpen ? ' open' : ''}`}>
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
    </>
  );
}

/** Previous passwords (SPEC §9.3), newest first; each revealed and copied on its own. */
function History({ entry, lang, reveal }: { entry: Entry; lang: 'ar' | 'en'; reveal: () => Promise<Entry | null> }) {
  return (
    <section class="group history" aria-labelledby="history-h">
      <h3 class="section-head" id="history-h">
        {t('history')}
      </h3>
      <div class="card">
        {entry.history.map((h, i) => (
          <HistoryRow key={i} item={h} lang={lang} reveal={reveal} />
        ))}
      </div>
      <p class="group-foot">{t('historyFoot')}</p>
    </section>
  );
}

function HistoryRow({ item, lang, reveal }: { item: OldPassword; lang: 'ar' | 'en'; reveal: () => Promise<Entry | null> }) {
  const [shown, setShown] = useState(false);
  useEffect(() => {
    if (!shown) return;
    const h = setTimeout(() => setShown(false), 30000);
    return () => clearTimeout(h);
  }, [shown]);
  return (
    <div class="kv-row history-row">
      <span class="kv-label">{item.changedAt ? t('historyUntil', { date: shortDate(item.changedAt, lang) }) : t('historyUnknown')}</span>
      <span class="kv-value" dir="ltr">
        {shown && item.password !== undefined ? <ColoredSecret value={item.password} /> : <span class="masked">••••••••••</span>}
      </span>
      <IconButton
        icon={shown ? 'eye-off' : 'eye'}
        label={shown ? t('hidePassword') : t('showPassword')}
        pressed={shown}
        onClick={() => (shown || item.password !== undefined ? setShown(!shown) : void reveal().then((x) => x && setShown(true)))}
      />
      <SecretCopy
        copy={item.password !== undefined ? () => item.password ?? '' : undefined}
        reveal={() => void reveal().then((x) => x && toast(t('revealedTapCopy'), 'ok'))}
      />
    </div>
  );
}
