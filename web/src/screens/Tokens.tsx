// Settings → Apps and agents (SPEC §17): access tokens that let an app or AI agent list
// account names and ask Keyra to type, never read a secret. Creating one needs a press;
// the token is shown once, here, and Keyra keeps only its hash. Revoking needs no press.
import { useEffect, useState } from 'preact/hooks';
import { Button, CopyButton, IconButton, Notice, Segmented, Spinner, TextField } from '../components/ui';
import { Alert, Sheet } from '../components/Sheet';
import { Ready } from '../components/Ready';
import { ApiError, api } from '../lib/api';
import { usePressGate } from '../lib/actions';
import { errorText, isLockedError } from '../lib/errors';
import { accountCount, t } from '../lib/i18n';
import { replace } from '../lib/router';
import { loadEntries, toast, useApp } from '../lib/store';
import type { AccessToken } from '../lib/types';
import { shortDate } from '../lib/wifi';
import { Qr } from './Recovery';

const MAX_SCOPE = 32;
const KIND = { agent: 'appsKindAgent', app: 'appsKindApp', extension: 'appsKindExt' } as const;
const KIND_HELP = { agent: 'appsKindAgentHelp', app: 'appsKindAppHelp', extension: 'appsKindExtHelp' } as const;
export const nameOk = (s: string) => s.trim().length > 0 && new TextEncoder().encode(s).length <= 48;

export function TokensSheet({ onClose }: { onClose: () => void }) {
  const app = useApp();
  const [list, setList] = useState<AccessToken[] | null>(null);
  const [max, setMax] = useState(8);
  const [view, setView] = useState<'list' | 'new'>('list');
  const [created, setCreated] = useState<(AccessToken & { token: string }) | null>(null);
  const [confirm, setConfirm] = useState<AccessToken | null>(null);

  const load = () =>
    api
      .tokens()
      .then((r) => {
        setList(r.tokens);
        setMax(r.max);
      })
      .catch((e) => {
        setList([]);
        if (!isLockedError(e)) toast(errorText(e), 'error');
      });
  useEffect(() => void load(), []);

  const revoke = async (tok: AccessToken) => {
    setConfirm(null);
    try {
      await api.revokeToken(tok.id);
      toast(t('appsRevoked'), 'ok');
      void load();
    } catch (e) {
      if (!isLockedError(e)) toast(errorText(e), 'error');
    }
  };

  let body;
  if (created) {
    body = (
      <ShownOnce
        tok={created}
        onDone={() => {
          setCreated(null);
          setView('list');
          void load();
        }}
      />
    );
  } else if (view === 'new') {
    body = <NewToken onCreated={setCreated} onCancel={() => setView('list')} />;
  } else {
    const full = (list?.length ?? 0) >= max;
    body = (
      <div class="form trusted">
        <p class="callout">{t('appsIntro')}</p>
        {list === null ? (
          <p class="waiting-row" role="status">
            <Spinner />
          </p>
        ) : list.length === 0 ? (
          <p class="callout center">{t('appsNone')}</p>
        ) : (
          <ul class="card rows" data-testid="token-list">
            {list.map((tok) => (
              <li key={tok.id}>
                <div class="row trusted-row">
                  <span class="row-label">
                    <bdi dir="auto">{tok.name}</bdi>
                    <span class="caption">
                      {t(KIND[tok.kind])} · {tok.scope === 'all' ? t('appsAll') : accountCount(tok.scope.length)} ·{' '}
                      {tok.lastUsed ? t('appsUsed', { date: shortDate(tok.lastUsed, app.lang) }) : t('appsNeverUsed')}
                    </span>
                  </span>
                  <IconButton icon="trash-2" label={`${t('appsRevoke')} ${tok.name}`} onClick={() => setConfirm(tok)} />
                </div>
              </li>
            ))}
          </ul>
        )}
        {full && <p class="caption">{t('appsFull')}</p>}
        <Button full icon="plus" disabled={list === null || full} onClick={() => setView('new')}>
          {t('appsNew')}
        </Button>
      </div>
    );
  }

  return (
    <Sheet title={t('appsRow')} size="md" onClose={onClose}>
      {body}
      {confirm && (
        <Alert
          title={t('appsRevokeTitle')}
          body={t('appsRevokeBody')}
          actions={[{ label: t('appsRevoke'), variant: 'danger-confirm', run: () => void revoke(confirm) }]}
          onCancel={() => setConfirm(null)}
        />
      )}
    </Sheet>
  );
}

type Fixed = { name: string; kind: AccessToken['kind'] };

function NewToken({ onCreated, onCancel, fixed }: { onCreated: (tok: AccessToken & { token: string }) => void; onCancel: () => void; fixed?: Fixed }) {
  const app = useApp();
  const [name, setName] = useState(fixed?.name ?? '');
  const [kind, setKind] = useState<AccessToken['kind']>(fixed?.kind ?? 'agent');
  const [all, setAll] = useState(!!fixed);
  const [picked, setPicked] = useState<number[]>([]);
  const [busy, setBusy] = useState(false);
  const gate = usePressGate('token_create');

  useEffect(() => {
    if (!app.entries) void loadEntries();
  }, []);

  const toggle = (id: number) => setPicked((p) => (p.includes(id) ? p.filter((x) => x !== id) : p.length < MAX_SCOPE ? [...p, id] : p));
  const ok = nameOk(name) && (all || picked.length > 0);

  const create = async () => {
    setBusy(true);
    try {
      const r = await gate.run(() => api.createToken({ name: name.trim(), kind, scope: all ? 'all' : picked }));
      if (r) onCreated(r);
    } catch (e) {
      if (e instanceof ApiError && e.code === 'tokens_full') toast(t('appsFull'), 'error');
      else if (!isLockedError(e)) toast(errorText(e), 'error');
    } finally {
      setBusy(false);
    }
  };

  if (gate.phase.kind === 'ready') {
    return <Ready state="ready" deadline={gate.phase.deadline} total={gate.phase.total} title={t('appsPressTitle')} body={t('appsPressBody')} onCancel={gate.cancel} />;
  }
  const entries = [...(app.entries ?? [])].sort((a, b) => a.title.localeCompare(b.title));
  return (
    <div class="form tokens-new">
      {!fixed && (
        <>
          <TextField label={t('appsName')} value={name} onValue={setName} helper={t('appsNameHelp')} maxLength={48} autocomplete="off" />
          <div class="row stack-row">
            <span class="row-label">{t('appsKind')}</span>
            <Segmented label={t('appsKind')} options={(['agent', 'app', 'extension'] as const).map((value) => ({ value, label: t(value === 'extension' ? 'appsKindExtShort' : KIND[value]) }))} value={kind} onChange={setKind} />
          </div>
          <p class="field-help">{t(KIND_HELP[kind])}</p>
        </>
      )}
      <div class="row stack-row">
        <span class="row-label">{t('appsScope')}</span>
        <Segmented
          label={t('appsScope')}
          options={[
            { value: 'some', label: t('appsScopeSome') },
            { value: 'all', label: t('appsScopeAll') },
          ]}
          value={all ? 'all' : 'some'}
          onChange={(v) => setAll(v === 'all')}
        />
      </div>
      <p class="field-help">{all ? t('appsScopeHelp') : picked.length >= MAX_SCOPE ? t('appsScopeMax') : picked.length === 0 ? t('appsScopePick') : t('appsScopeHelp')}</p>
      {!all &&
        (app.entries === null ? (
          <p class="waiting-row" role="status">
            <Spinner />
          </p>
        ) : (
          <ul class="card rows token-pick" data-testid="token-scope">
            {entries.map((e) => (
              <li key={e.id}>
                <label class="row pick-row">
                  <input type="checkbox" checked={picked.includes(e.id)} onChange={() => toggle(e.id)} />
                  <bdi dir="auto" class="row-label">
                    {e.title}
                  </bdi>
                </label>
              </li>
            ))}
          </ul>
        ))}
      <Button full icon="key-round" loading={busy} disabled={!ok} onClick={() => void create()}>
        {t('appsCreate')}
      </Button>
      <Button variant="ghost" full onClick={onCancel}>
        {t('cancel')}
      </Button>
    </div>
  );
}

function ShownOnce({ tok, onDone }: { tok: AccessToken & { token: string }; onDone: () => void }) {
  return (
    <div class="form kit">
      <Notice tone="warn">{t('appsShownOnce')}</Notice>
      <div class="card pad kit-key">
        <span class="field-label">
          {t('appsTokenLabel')} · <bdi dir="auto">{tok.name}</bdi>
        </span>
        <p class="kit-code mono" dir="ltr" data-testid="access-token">
          {tok.token}
        </p>
        <CopyButton value={() => tok.token} />
        <Qr text={tok.token} />
      </div>
      {tok.kind === 'agent' && <p class="caption">{t('appsMcpHint')}</p>}
      <Button full onClick={onDone}>
        {t('done')}
      </Button>
    </div>
  );
}

const NONCE = /^[A-Za-z0-9_-]{22}$/;

/**
 * SPEC §9.4 pairing, step 3–4: #/connect?ext=<name>&n=<nonce>, opened by Keyra Companion. After the
 * press the token goes to the page by postMessage (the extension listens in this tab only) and is
 * not kept here.
 */
export function ConnectSheet({ ext, n }: { ext: string; n: string }) {
  const [done, setDone] = useState(false);
  const name = ext.trim();
  const close = () => replace('/');
  let body;
  if (!NONCE.test(n) || !nameOk(name) || /[\x00-\x1f\x7f]/.test(name)) body = <Notice tone="warn">{t('connectBad')}</Notice>;
  else if (done) body = <Notice tone="accent">{t('connectDone')}</Notice>;
  else
    body = (
      <>
        <p class="callout">{t('connectAsk', { name })}</p>
        <NewToken
          fixed={{ name, kind: 'extension' }}
          onCreated={(tok) => {
            window.postMessage({ type: 'keyra:token', n, token: tok.token }, location.origin);
            setDone(true);
          }}
          onCancel={close}
        />
      </>
    );
  return (
    <Sheet title={t('connectTitle')} size="md" onClose={close}>
      <div class="form">{body}</div>
    </Sheet>
  );
}
