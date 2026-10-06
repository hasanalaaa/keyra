// CSV import (DESIGN §5.7): source → instructions → preview → batches of 50 → result.
import { useRef, useState } from 'preact/hooks';
import { Button, Notice } from '../components/ui';
import { Sheet, type SheetCtl } from '../components/Sheet';
import { api } from '../lib/api';
import { dupKey, parseExport, type ImportEntry, type Source } from '../lib/csv';
import { errorText, isLockedError } from '../lib/errors';
import { t, type Key } from '../lib/i18n';
import { back } from '../lib/router';
import { loadEntries, toast, useApp } from '../lib/store';

const SOURCES: { id: Source; name: string; steps: Key }[] = [
  { id: 'apple', name: 'Apple Passwords', steps: 'stepsApple' },
  { id: 'chrome', name: 'Chrome', steps: 'stepsChrome' },
  { id: 'bitwarden', name: 'Bitwarden', steps: 'stepsBitwarden' },
  { id: '1password', name: '1Password', steps: 'steps1Password' },
];

const BATCH = 50;

type Step =
  | { s: 'source' }
  | { s: 'file'; src: (typeof SOURCES)[number]; bad?: boolean }
  | { s: 'preview'; entries: ImportEntry[]; dups: number; noPw: number }
  | { s: 'progress'; done: number; total: number }
  | { s: 'result'; added: number };

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

  const run = async (entries: ImportEntry[]) => {
    let added = 0;
    setStep({ s: 'progress', done: 0, total: entries.length });
    for (let i = 0; i < entries.length; i += BATCH) {
      try {
        const r = await api.importBatch(entries.slice(i, i + BATCH));
        added += r.added;
      } catch (e) {
        if (!isLockedError(e)) toast(errorText(e), 'error');
        break;
      }
      setStep({ s: 'progress', done: Math.min(entries.length, i + BATCH), total: entries.length });
    }
    await loadEntries();
    setStep({ s: 'result', added });
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
            </div>
          </>
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
            <p class="t2">{t('imported', { a: step.added })}</p>
            <Notice tone="warn">{t('deleteCsv')}</Notice>
            <Button size="lg" full onClick={() => ctl.current?.close()}>
              {t('done')}
            </Button>
          </>
        )}
      </div>
    </Sheet>
  );
}
