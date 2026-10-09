// Settings → NFC tags (SPEC §18): a tag's URL arms one account for typing, never reads it.
// Creating one needs a press; the URL (simple) or keys (secure) are shown once. Revoking needs none.
import type { ComponentChildren } from 'preact';
import { useEffect, useState } from 'preact/hooks';
import { Button, CopyButton, IconButton, Notice, Segmented, Spinner, TextField } from '../components/ui';
import { Alert, Sheet } from '../components/Sheet';
import { Ready } from '../components/Ready';
import { ApiError, api } from '../lib/api';
import { usePressGate } from '../lib/actions';
import { errorText, isLockedError } from '../lib/errors';
import { t, type Key } from '../lib/i18n';
import { loadEntries, toast, useApp } from '../lib/store';
import type { NewNfcTag, NfcTag } from '../lib/types';
import { shortDate } from '../lib/wifi';
import { hexDec, sdmOffsets } from '../lib/tags';
import { Qr } from './Recovery';
import { nameOk } from './Tokens';

const WHAT: [NfcTag['what'], Key][] = [
  ['both', 'chipBoth'],
  ['username', 'chipUsername'],
  ['password', 'chipPassword'],
  ['totp', 'chipCode'],
];

const onErr = (e: unknown) => {
  if (e instanceof ApiError && e.code === 'tags_full') toast(t('tagsFull'), 'error');
  else if (!isLockedError(e)) toast(errorText(e), 'error');
};

export function TagsSheet({ onClose }: { onClose: () => void }) {
  const app = useApp();
  const [list, setList] = useState<NfcTag[] | null>(null);
  const [max, setMax] = useState(16);
  const [view, setView] = useState<'list' | 'new'>('list');
  const [created, setCreated] = useState<NewNfcTag | null>(null);
  const [confirm, setConfirm] = useState<NfcTag | null>(null);

  const load = () =>
    api.tags().then(
      (r) => (setList(r.tags), setMax(r.max)),
      (e) => (setList([]), onErr(e)),
    );
  useEffect(() => {
    void load();
    if (!app.entries) void loadEntries();
  }, []);
  const title = (id: number) => app.entries?.find((e) => e.id === id)?.title ?? `#${id}`;

  const revoke = async (tag: NfcTag) => {
    setConfirm(null);
    try {
      await api.revokeTag(tag.id);
      toast(t('tagsRevoked'), 'ok');
      void load();
    } catch (e) {
      onErr(e);
    }
  };

  let body;
  if (created) {
    body = <ShownOnce tag={created} onDone={() => (setCreated(null), setView('list'), void load())} />;
  } else if (view === 'new') {
    body = <NewTag onCreated={setCreated} onCancel={() => setView('list')} />;
  } else {
    const full = (list?.length ?? 0) >= max;
    body = (
      <div class="form trusted">
        <p class="callout">{t('tagsIntro')}</p>
        {list === null ? (
          <p class="waiting-row" role="status">
            <Spinner />
          </p>
        ) : list.length === 0 ? (
          <p class="callout center">{t('tagsNone')}</p>
        ) : (
          <ul class="card rows" data-testid="tag-list">
            {list.map((tag) => (
              <li key={tag.id}>
                <div class="row trusted-row">
                  <span class="row-label">
                    <bdi dir="auto">{tag.name}</bdi>
                    <span class="caption">
                      <bdi dir="auto">{title(tag.entry)}</bdi> · {t(tag.kind === 'secure' ? 'tagsSecure' : 'tagsSimple')} ·{' '}
                      {tag.lastUsed ? t('appsUsed', { date: shortDate(tag.lastUsed, app.lang) }) : t('appsNeverUsed')}
                    </span>
                  </span>
                  <IconButton icon="trash-2" label={`${t('appsRevoke')} ${tag.name}`} onClick={() => setConfirm(tag)} />
                </div>
              </li>
            ))}
          </ul>
        )}
        {full && <p class="caption">{t('tagsFull')}</p>}
        <Button full icon="plus" disabled={list === null || full} onClick={() => setView('new')}>
          {t('tagsNew')}
        </Button>
      </div>
    );
  }

  return (
    <Sheet title={t('tagsRow')} size="md" onClose={onClose}>
      {body}
      {confirm && (
        <Alert
          title={t('tagsRevokeTitle')}
          body={t('tagsRevokeBody')}
          actions={[{ label: t('appsRevoke'), variant: 'danger-confirm', run: () => void revoke(confirm) }]}
          onCancel={() => setConfirm(null)}
        />
      )}
    </Sheet>
  );
}

function NewTag({ onCreated, onCancel }: { onCreated: (tag: NewNfcTag) => void; onCancel: () => void }) {
  const app = useApp();
  const [name, setName] = useState('');
  const [entry, setEntry] = useState(0);
  const [what, setWhat] = useState<NfcTag['what']>('both');
  const [target, setTarget] = useState('');
  const [kind, setKind] = useState<NfcTag['kind']>('simple');
  const [busy, setBusy] = useState(false);
  const gate = usePressGate('tag_create');

  const create = async () => {
    setBusy(true);
    try {
      const r = await gate.run(() => api.createTag({ name: name.trim(), kind, entry, what, ...(target ? { target } : {}) }));
      if (r) onCreated(r);
    } catch (e) {
      onErr(e);
    } finally {
      setBusy(false);
    }
  };

  if (gate.phase.kind === 'ready') {
    return <Ready state="ready" deadline={gate.phase.deadline} total={gate.phase.total} title={t('tagsPressTitle')} body={t('tagsPressBody')} onCancel={gate.cancel} />;
  }
  const entries = [...(app.entries ?? [])].sort((a, b) => a.title.localeCompare(b.title));
  const select = (label: string, value: string | number, on: (v: string) => void, options: [string | number, string][], testid: string) => (
    <label class="row stack-row">
      <span class="row-label">{label}</span>
      <select class="os-select" data-testid={testid} value={value} onChange={(e) => on((e.currentTarget as HTMLSelectElement).value)}>
        {options.map(([v, l]) => (
          <option key={v} value={v}>
            {l}
          </option>
        ))}
      </select>
    </label>
  );
  return (
    <div class="form tokens-new">
      <TextField label={t('appsName')} value={name} onValue={setName} helper={t('tagsNameHelp')} maxLength={48} autocomplete="off" />
      {select(t('tagsAccount'), entry, (v) => setEntry(Number(v)), [[0, '—'], ...entries.map((e): [number, string] => [e.id, e.title])], 'tag-entry')}
      {select(t('tagsWhat'), what, (v) => setWhat(v as NfcTag['what']), WHAT.map(([v, k]) => [v, t(k)]), 'tag-what')}
      {select(t('typeInto'), target, setTarget, [['', t('tagsDefault')], ['usb', 'USB'], ...(app.ble?.bonds ?? []).map((b): [string, string] => [b.addr, b.name || b.addr])], 'tag-target')}
      <div class="row stack-row">
        <span class="row-label">{t('tagsKind')}</span>
        <Segmented
          label={t('tagsKind')}
          options={[
            { value: 'simple', label: t('tagsSimple') },
            { value: 'secure', label: t('tagsSecure') },
          ]}
          value={kind}
          onChange={setKind}
        />
      </div>
      <p class="field-help">{t(kind === 'secure' ? 'tagsSecureHelp' : 'tagsSimpleHelp')}</p>
      <Button full icon="key-round" loading={busy} disabled={!nameOk(name) || !entry} onClick={() => void create()}>
        {t('tagsCreate')}
      </Button>
      <Button variant="ghost" full onClick={onCancel}>
        {t('cancel')}
      </Button>
    </div>
  );
}

function Code({ label, value, testid, children }: { label: string; value: string; testid: string; children?: ComponentChildren }) {
  return (
    <div class="card pad kit-key">
      <span class="field-label">{label}</span>
      <p class="kit-code mono" dir="ltr" data-testid={testid}>
        {value}
      </p>
      <CopyButton value={() => value} />
      {children}
    </div>
  );
}

function ShownOnce({ tag, onDone }: { tag: NewNfcTag; onDone: () => void }) {
  const o = sdmOffsets(tag.url);
  return (
    <div class="form kit">
      <Notice tone="warn">{t(tag.keys ? 'tagsKeysOnce' : 'tagsShownOnce')}</Notice>
      <Code label={t('tagsUrl')} value={tag.url} testid="tag-url">
        {!tag.keys && <Qr text={tag.url} />}
      </Code>
      <p class="caption">{t(tag.keys ? 'tagsHowSecure' : 'tagsHowSimple')}</p>
      {tag.keys && (
        <>
          <Code label={t('tagsKeyMeta')} value={tag.keys.meta} testid="tag-key-meta" />
          <Code label={t('tagsKeyFile')} value={tag.keys.file} testid="tag-key-file" />
          {o && (
            <ul class="card rows" data-testid="tag-sdm">
              <li class="row">
                <span class="row-label">{t('tagsSdmPicc')}</span>
                <span class="mono" dir="ltr">{hexDec(o.picc)}</span>
              </li>
              <li class="row">
                <span class="row-label">{t('tagsSdmMac')}</span>
                <span class="mono" dir="ltr">{hexDec(o.mac)}</span>
              </li>
            </ul>
          )}
        </>
      )}
      <Button full onClick={onDone}>
        {t('done')}
      </Button>
    </div>
  );
}
