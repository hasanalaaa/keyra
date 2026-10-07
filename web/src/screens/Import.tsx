// CSV import (DESIGN §5.7): source → instructions → preview → batches (≤ 50 entries, ≤ 48 KiB) → result.
import { batches } from '../lib/batch';
import { useRef, useState } from 'preact/hooks';
import { Icon } from '../components/Icon';
import { QR_ERRORS, QrPhoto } from '../components/QrPhoto';
import { Button, Notice, Segmented } from '../components/ui';
import { Sheet, type SheetCtl } from '../components/Sheet';
import { api } from '../lib/api';
import { dupKey, parseExport, type ImportEntry, type Source } from '../lib/csv';
import { errorText, isLockedError } from '../lib/errors';
import { t, type Key } from '../lib/i18n';
import { accountKey, parseQrText, titleOf, toOtpauth, type OtpAccount, type OtpError } from '../lib/qrImport';
import { back } from '../lib/router';
import { loadEntries, toast, useApp } from '../lib/store';
import type { EntrySummary } from '../lib/types';

const SOURCES: { id: Source; name: string; steps: Key }[] = [
  { id: 'apple', name: 'Apple Passwords', steps: 'stepsApple' },
  { id: 'chrome', name: 'Chrome', steps: 'stepsChrome' },
  { id: 'bitwarden', name: 'Bitwarden', steps: 'stepsBitwarden' },
  { id: '1password', name: '1Password', steps: 'steps1Password' },
];

type Mode = 'new' | 'attach';
interface QrState {
  accounts: OtpAccount[];
  skipped: number;
  /** Google splits big exports over several QRs; `seen` holds the batchIndex values read so far. */
  batch: { id: number; size: number; seen: number[] } | null;
  mode: Mode | null; // null until the first preview decides
  err: OtpError | 'none' | null;
}

interface QrItem {
  a: OtpAccount;
  kind: 'new' | 'attach' | 'has';
  target?: EntrySummary;
}

/** An existing account with the same name (and the same user name, when the QR has one) can take the code. */
function planQr(accounts: OtpAccount[], entries: EntrySummary[], mode: Mode): QrItem[] {
  const lc = (x: string) => x.trim().toLowerCase();
  return accounts.map((a) => {
    if (mode === 'new') return { a, kind: 'new' };
    const hits = entries.filter((e) => lc(e.title) === lc(titleOf(a)) && (!a.account || lc(e.username) === lc(a.account)));
    const target = hits.find((e) => !e.hasTotp) ?? hits[0];
    return target ? { a, kind: target.hasTotp ? 'has' : 'attach', target } : { a, kind: 'new' };
  });
}

const qrEntry = (a: OtpAccount): ImportEntry => ({ title: titleOf(a), url: '', username: a.account, password: '', totp: toOtpauth(a), notes: '', favorite: false });

type Step =
  | { s: 'source' }
  | { s: 'qr'; q: QrState }
  | { s: 'qrPreview'; q: QrState }
  | { s: 'file'; src: (typeof SOURCES)[number]; bad?: boolean }
  | { s: 'preview'; entries: ImportEntry[]; dups: number; noPw: number }
  | { s: 'progress'; done: number; total: number }
  | { s: 'result'; added: number; attached?: number; qr?: boolean };

export function ImportSheet() {
  const app = useApp();
  const [step, setStep] = useState<Step>({ s: 'source' });
  const ctl = useRef<SheetCtl | null>(null);
  const fileInput = useRef<HTMLInputElement>(null);

  const onFile = async (src: (typeof SOURCES)[number], f: File | undefined) => {
    if (!f) return;
    const parsed = parseExport(await f.text());
    if (fileInput.current) fileInput.current.value = '';
    if (!parsed || parsed.entries.length === 0) return setStep({ s: 'file', src, bad: true });
    const existing = new Set((app.entries ?? []).map(dupKey));
    const seen = new Set<string>();
    let dups = 0;
    for (const e of parsed.entries) {
      const k = dupKey(e);
      if (existing.has(k) || seen.has(k)) dups++;
      seen.add(k);
    }
    setStep({ s: 'preview', entries: parsed.entries, dups, noPw: parsed.entries.filter((e) => !e.password).length });
  };

  const onQr = (text: string, prev: QrState | null) => {
    const q: QrState = prev ?? { accounts: [], skipped: 0, batch: null, mode: null, err: null };
    const r = parseQrText(text);
    if (!r.ok) return setStep({ s: prev ? 'qrPreview' : 'qr', q: { ...q, err: r.error } });
    const incoming = r.value.kind === 'totp' ? { accounts: [r.value.account], skipped: 0, batch: null } : r.value.migration;
    const have = new Set(q.accounts.map(accountKey));
    const accounts = [...q.accounts, ...incoming.accounts.filter((a) => !have.has(accountKey(a)))];
    let batch = q.batch;
    if ('batchSize' in incoming && incoming.batchSize > 1) {
      const seen = batch?.id === incoming.batchId ? batch.seen : [];
      batch = { id: incoming.batchId, size: incoming.batchSize, seen: seen.includes(incoming.batchIndex) ? seen : [...seen, incoming.batchIndex] };
    }
    const mode = q.mode ?? (planQr(accounts, app.entries ?? [], 'attach').some((i) => i.kind === 'attach') ? 'attach' : 'new');
    const next: QrState = { accounts, skipped: q.skipped + incoming.skipped, batch, err: null, mode };
    if (accounts.length === 0) return setStep({ s: prev ? 'qrPreview' : 'qr', q: { ...next, err: 'secret' } });
    setStep({ s: 'qrPreview', q: next });
  };

  const run = async (entries: ImportEntry[], attach: { id: number; totp: string }[] = [], qr = false) => {
    let added = 0;
    let attached = 0;
    const total = entries.length + attach.length;
    setStep({ s: 'progress', done: 0, total });
    try {
      for (const x of attach) {
        await api.update(x.id, { totp: x.totp });
        setStep({ s: 'progress', done: ++attached, total });
      }
      let sent = 0;
      for (const part of batches(entries)) {
        const r = await api.importBatch(part);
        added += r.added;
        sent += part.length;
        setStep({ s: 'progress', done: attached + sent, total });
      }
    } catch (e) {
      if (!isLockedError(e)) toast(errorText(e), 'error');
    }
    await loadEntries();
    setStep({ s: 'result', added, attached, qr });
  };

  const busy = step.s === 'progress';
  return (
    <Sheet title={t('importTitle')} size="lg" tall ctl={ctl} onClose={() => back('/')} dismissible={!busy}>
      <div class="import">
        {step.s === 'source' && (
          <>
            <p class="callout">{t('importBody')}</p>
            <div class="source-grid">
              {SOURCES.map((src) => (
                <button key={src.id} type="button" class="source-card" onClick={() => setStep({ s: 'file', src })}>
                  <span class="source-name" dir="ltr">
                    {src.name}
                  </span>
                  <span class="caption">CSV</span>
                </button>
              ))}
              <button type="button" class="source-card source-wide" onClick={() => setStep({ s: 'qr', q: { accounts: [], skipped: 0, batch: null, mode: null, err: null } })}>
                <Icon name="qr-code" size={24} />
                <span class="source-name" dir="ltr">
                  {t('gaCard')}
                </span>
                <span class="caption">{t('gaCaption')}</span>
              </button>
            </div>
          </>
        )}
        {step.s === 'qr' && (
          <>
            <h3 class="headline" dir="ltr">
              {t('gaCard')}
            </h3>
            <ol class="howto">
              {t('stepsGoogle')
                .split('\n')
                .map((line) => (
                  <li key={line}>{line.replace(/^\d+\.\s*/, '')}</li>
                ))}
            </ol>
            {step.q.err && <Notice tone="err">{t(step.q.err === 'none' ? 'qrNone' : QR_ERRORS[step.q.err])}</Notice>}
            <QrPhoto label={t('scanQr')} variant="primary" onText={(x) => onQr(x, null)} onNone={() => setStep({ s: 'qr', q: { ...step.q, err: 'none' } })} />
            <Button variant="ghost" icon="chevron-left" onClick={() => setStep({ s: 'source' })}>
              {t('back')}
            </Button>
          </>
        )}
        {step.s === 'qrPreview' && (
          <QrPreview
            q={step.q}
            entries={app.entries ?? []}
            onQ={(q) => setStep({ s: 'qrPreview', q })}
            onText={(x) => onQr(x, step.q)}
            onRun={run}
          />
        )}
        {step.s === 'file' && (
          <>
            <h3 class="headline" dir="ltr">
              {step.src.name}
            </h3>
            <ol class="howto">
              {t(step.src.steps)
                .split('\n')
                .map((line) => (
                  <li key={line}>{line.replace(/^\d+\.\s*/, '')}</li>
                ))}
            </ol>
            {step.bad && <Notice tone="err">{t('badFile')}</Notice>}
            <label class="btn btn-primary btn-lg btn-full file-btn">
              {t('chooseFile')}
              <input
                ref={fileInput}
                type="file"
                accept=".csv,text/csv"
                class="sr-only"
                onChange={(e) => void onFile(step.src, e.currentTarget.files?.[0])}
              />
            </label>
            <Button variant="ghost" icon="chevron-left" onClick={() => setStep({ s: 'source' })}>
              {t('back')}
            </Button>
          </>
        )}
        {step.s === 'preview' && (
          <>
            <div class="card summary">
              <p class="t2">{t('found', { n: step.entries.length })}</p>
              {step.dups > 0 && <p class="callout">{t('duplicates', { d: step.dups })}</p>}
              {step.noPw > 0 && <p class="callout">{t('noPw', { m: step.noPw })}</p>}
              <ul class="preview-list">
                {step.entries.slice(0, 5).map((e, i) => (
                  <li key={i} dir="auto">
                    {e.title}
                  </li>
                ))}
              </ul>
            </div>
            <Button size="lg" full onClick={() => void run(step.entries)}>
              {t('importN', { n: step.entries.length })}
            </Button>
          </>
        )}
        {step.s === 'progress' && (
          <div class="progress-block">
            <p class="callout">{t('importing', { a: step.done, n: step.total })}</p>
            <div class="progress" role="progressbar" aria-valuemin={0} aria-valuemax={step.total} aria-valuenow={step.done}>
              <span style={{ inlineSize: `${(step.done / step.total) * 100}%` }} />
            </div>
          </div>
        )}
        {step.s === 'result' && (
          <>
            {step.qr ? (
              <>
                {step.added === 0 && !step.attached && <p class="t2">{t('qrNothing')}</p>}
                {step.added > 0 && <p class="t2">{t('imported', { a: step.added })}</p>}
                {!!step.attached && <p class="t2">{t('qrAttached', { u: step.attached })}</p>}
              </>
            ) : (
              <p class="t2">{t('imported', { a: step.added })}</p>
            )}
            <Notice tone="warn">{t(step.qr ? 'deleteQr' : 'deleteCsv')}</Notice>
            <Button size="lg" full onClick={() => ctl.current?.close()}>
              {t('done')}
            </Button>
          </>
        )}
      </div>
    </Sheet>
  );
}

function QrPreview({ q, entries, onQ, onText, onRun }: { q: QrState; entries: EntrySummary[]; onQ: (q: QrState) => void; onText: (text: string) => void; onRun: (entries: ImportEntry[], attach: { id: number; totp: string }[], qr: boolean) => void }) {
  const mode = q.mode ?? 'new';
  const items = planQr(q.accounts, entries, mode);
  const canAttach = planQr(q.accounts, entries, 'attach').some((i) => i.kind !== 'new');
  const existing = new Set(entries.map(dupKey));
  const fresh = items.filter((i) => i.kind === 'new');
  const dups = fresh.filter((i) => existing.has(dupKey(qrEntry(i.a)))).length;
  const todo = items.length - items.filter((i) => i.kind === 'has').length;
  const more = q.batch && q.batch.seen.length < q.batch.size;
  const go = () =>
    onRun(
      fresh.map((i) => qrEntry(i.a)),
      items.filter((i) => i.kind === 'attach').map((i) => ({ id: i.target!.id, totp: toOtpauth(i.a) })),
      true,
    );
  return (
    <>
      {q.err && <Notice tone="err">{t(q.err === 'none' ? 'qrNone' : QR_ERRORS[q.err])}</Notice>}
      <div class="card summary">
        <p class="t2">{t('found', { n: items.length })}</p>
        {q.batch && <p class="callout">{t('qrScanned', { a: q.batch.seen.length, n: q.batch.size })}</p>}
        {q.skipped > 0 && <p class="callout">{t('qrSkipped', { s: q.skipped })}</p>}
        {mode === 'new' && dups > 0 && <p class="callout">{t('duplicates', { d: dups })}</p>}
        <ul class="preview-list qr-list">
          {items.map((i) => (
            <li key={accountKey(i.a)} dir="auto">
              <span>{titleOf(i.a)}</span>
              {i.a.account && i.a.account !== titleOf(i.a) && <span class="caption">{i.a.account}</span>}
              {i.kind === 'attach' && <span class="caption qr-tag">{t('qrRowAttach', { t: i.target!.title })}</span>}
              {i.kind === 'has' && <span class="caption">{t('qrRowHas', { t: i.target!.title })}</span>}
            </li>
          ))}
        </ul>
      </div>
      {canAttach && (
        <Segmented
          label={t('qrMode')}
          value={mode}
          onChange={(m) => onQ({ ...q, mode: m, err: null })}
          options={[
            { value: 'new', label: t('qrModeNew') },
            { value: 'attach', label: t('qrModeAttach') },
          ]}
        />
      )}
      {more && <QrPhoto label={t('qrScanNext')} onText={onText} onNone={() => onQ({ ...q, err: 'none' })} />}
      <Button size="lg" full disabled={todo === 0} onClick={go}>
        {t('importN', { n: todo })}
      </Button>
    </>
  );
}
