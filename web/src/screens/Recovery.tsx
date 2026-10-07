// Recovery kit (SPEC §12.2): create / replace / remove the recovery key, the printable
// Emergency Kit, and Shamir shares. The key exists in this page only until the sheet closes.
import { createPortal } from 'preact';
import { useEffect, useMemo, useState } from 'preact/hooks';
// Only the encoder core (no canvas/PNG renderers): keeps the single-file app small.
import * as QRCode from 'qrcode/lib/core/qrcode';
import { Button, Notice, Segmented } from '../components/ui';
import { Alert, Sheet } from '../components/Sheet';
import { Ready } from '../components/Ready';
import { api } from '../lib/api';
import { usePressGate } from '../lib/actions';
import { errorText, isLockedError } from '../lib/errors';
import { t } from '../lib/i18n';
import { formatKey, fromHex, parseKey, splitKey } from '../lib/recovery';
import { toast, useApp } from '../lib/store';
import { shortDate } from '../lib/wifi';
import type { RecoveryInfo } from '../lib/types';

/** A QR code as inline SVG (no canvas, no network). */
export function Qr({ text, size = 168 }: { text: string; size?: number }) {
  const path = useMemo(() => {
    const m = QRCode.create(text, { errorCorrectionLevel: 'M' }).modules;
    let d = '';
    for (let r = 0; r < m.size; r++) for (let c = 0; c < m.size; c++) if (m.get(r, c)) d += `M${c + 4} ${r + 4}h1v1h-1z`;
    return { d, n: m.size + 8 };
  }, [text]);
  return (
    <svg class="qr" viewBox={`0 0 ${path.n} ${path.n}`} width={size} height={size} role="img" aria-label="QR" shape-rendering="crispEdges">
      <rect width={path.n} height={path.n} fill="#fff" />
      <path d={path.d} fill="#000" />
    </svg>
  );
}

type Confirm = 'replace' | 'remove' | null;

export function RecoverySheet({ onClose }: { onClose: () => void }) {
  const app = useApp();
  const [info, setInfo] = useState<RecoveryInfo | null>(null);
  const [key, setKey] = useState<{ text: string; created: number } | null>(null);
  const [confirm, setConfirm] = useState<Confirm>(null);
  const [busy, setBusy] = useState(false);
  const gate = usePressGate('recovery');

  useEffect(() => {
    api
      .recovery()
      .then(setInfo)
      .catch((e) => !isLockedError(e) && toast(errorText(e), 'error'));
  }, []);

  const create = async () => {
    setConfirm(null);
    setBusy(true);
    try {
      const r = await gate.run(() => api.createRecovery());
      if (!r) return;
      setKey({ text: formatKey(fromHex(r.recoveryKey)), created: r.created });
      setInfo({ enabled: true, created: r.created });
    } catch (e) {
      if (!isLockedError(e)) toast(errorText(e), 'error');
    } finally {
      setBusy(false);
    }
  };

  const remove = async () => {
    setConfirm(null);
    setBusy(true);
    try {
      if ((await gate.run(() => api.removeRecovery())) === null) return;
      setInfo({ enabled: false, created: 0 });
      toast(t('recoveryRemoved'), 'ok');
    } catch (e) {
      if (!isLockedError(e)) toast(errorText(e), 'error');
    } finally {
      setBusy(false);
    }
  };

  let body;
  if (gate.phase.kind === 'ready') {
    body = <Ready state="ready" deadline={gate.phase.deadline} total={gate.phase.total} title={t('recoveryPressTitle')} body={t('recoveryPressBody')} onCancel={gate.cancel} />;
  } else if (key) {
    body = <KitView keyText={key.text} created={key.created} device={app.device?.device.name ?? 'Keyra'} lang={app.lang} onDone={onClose} />;
  } else {
    body = (
      <div class="form">
        <p class="callout">{t('recoveryIntro')}</p>
        {info?.enabled && (
          <p class="caption">{info.created ? t('recoveryOn', { date: shortDate(info.created, app.lang) }) : t('recoveryOnUnknown')}</p>
        )}
        <Button full icon="key-round" loading={busy} disabled={!info} onClick={() => (info?.enabled ? setConfirm('replace') : void create())}>
          {info?.enabled ? t('recoveryRegenerate') : t('recoveryCreate')}
        </Button>
        {info?.enabled && (
          <Button variant="danger" full onClick={() => setConfirm('remove')}>
            {t('recoveryRemove')}
          </Button>
        )}
      </div>
    );
  }

  return (
    <Sheet title={t('recoveryTitle')} size="md" onClose={onClose}>
      <div class="recovery">{body}</div>
      {confirm === 'replace' && (
        <Alert
          title={t('recoveryReplaceTitle')}
          body={t('recoveryReplaceBody')}
          actions={[{ label: t('recoveryRegenerate'), variant: 'danger-confirm', run: () => void create() }]}
          onCancel={() => setConfirm(null)}
        />
      )}
      {confirm === 'remove' && (
        <Alert
          title={t('recoveryRemoveTitle')}
          body={t('recoveryRemoveBody')}
          actions={[{ label: t('recoveryRemove'), variant: 'danger-confirm', run: () => void remove() }]}
          onCancel={() => setConfirm(null)}
        />
      )}
    </Sheet>
  );
}

function KitView({ keyText, created, device, lang, onDone }: { keyText: string; created: number; device: string; lang: 'ar' | 'en'; onDone: () => void }) {
  const [n, setN] = useState(3);
  const [k, setK] = useState(2);
  const [shares, setShares] = useState<string[] | null>(null);
  const [splitOpen, setSplitOpen] = useState(false);
  const [printing, setPrinting] = useState<'kit' | 'shares' | null>(null);
  const date = created ? shortDate(created, lang) : '';

  // Shares are recomputed for every new n/k: old ones would not combine with new ones anyway.
  useEffect(() => {
    if (!splitOpen) return;
    let live = true;
    setShares(null);
    const bytes = keyBytes(keyText);
    void splitKey(bytes, n, Math.min(k, n)).then((s) => live && setShares(s));
    return () => {
      live = false;
    };
  }, [splitOpen, n, k, keyText]);

  useEffect(() => {
    if (!printing) return;
    // Let the print-only markup render, then open the system print dialog (PDF or paper).
    const h = setTimeout(() => {
      window.print();
      setPrinting(null);
    }, 50);
    return () => clearTimeout(h);
  }, [printing]);

  return (
    <div class="form kit">
      <Notice tone="warn">{t('recoveryWarn')}</Notice>
      <div class="card pad kit-key">
        <span class="field-label">{t('recoveryKeyLabel')}</span>
        <p class="kit-code mono" dir="ltr" data-testid="recovery-key">
          {keyText}
        </p>
        <Qr text={keyText} />
        <p class="caption">{t('recoveryShownOnce')}</p>
      </div>
      <Button full icon="download" onClick={() => setPrinting('kit')}>
        {t('recoveryPrint')}
      </Button>
      {!splitOpen ? (
        <Button variant="secondary" full onClick={() => setSplitOpen(true)}>
          {t('recoverySplit')}
        </Button>
      ) : (
        <section class="group shares">
          <div class="row stack-row">
            <span class="row-label">{t('sharesN')}</span>
            <Segmented label={t('sharesN')} options={[2, 3, 4, 5].map((v) => ({ value: v, label: String(v) }))} value={n} onChange={(v) => setN(v)} />
          </div>
          <div class="row stack-row">
            <span class="row-label">{t('sharesK')}</span>
            <Segmented
              label={t('sharesK')}
              options={[2, 3, 4, 5].filter((v) => v <= n).map((v) => ({ value: v, label: String(v) }))}
              value={Math.min(k, n)}
              onChange={(v) => setK(v)}
            />
          </div>
          <p class="field-help">{t('recoveryShareHelp', { k: Math.min(k, n), n })}</p>
          {shares?.map((s, i) => (
            <div class="card pad share-card" key={s}>
              <span class="field-label">{t('shareLabel', { i: i + 1, n, k: Math.min(k, n) })}</span>
              <p class="kit-code mono" dir="ltr" data-testid="recovery-share">
                {s}
              </p>
              <Qr text={s} size={140} />
            </div>
          ))}
          {shares && (
            <Button variant="secondary" full icon="download" onClick={() => setPrinting('shares')}>
              {t('recoveryPrint')}
            </Button>
          )}
        </section>
      )}
      <Button variant="ghost" full onClick={onDone}>
        {t('recoveryDone')}
      </Button>
      {printing &&
        createPortal(
        <div class="print-kit" aria-hidden="true">
          {printing === 'kit' ? (
            <KitPage title={t('kitHeading')} device={device} date={date} code={keyText} steps={[t('kitStep1'), t('kitStep2'), t('kitStep3')]} />
          ) : (
            shares?.map((s, i) => (
              <KitPage
                key={s}
                title={`${t('kitHeading')} · ${t('shareLabel', { i: i + 1, n, k: Math.min(k, n) })}`}
                device={device}
                date={date}
                code={s}
                steps={[t('kitShareStep', { k: Math.min(k, n) }), t('kitStep3')]}
              />
            ))
          )}
        </div>,
          document.body,
        )}
    </div>
  );
}

function KitPage({ title, device, date, code, steps }: { title: string; device: string; date: string; code: string; steps: string[] }) {
  return (
    <article class="kit-page">
      <h1>{title}</h1>
      <dl>
        <dt>{t('kitDevice')}</dt>
        <dd>{device}</dd>
        {date && (
          <>
            <dt>{t('kitDate')}</dt>
            <dd>{date}</dd>
          </>
        )}
      </dl>
      <p class="kit-print-code" dir="ltr">
        {code}
      </p>
      <Qr text={code} size={200} />
      <ol>
        {steps.map((s) => (
          <li key={s}>{s}</li>
        ))}
      </ol>
    </article>
  );
}

function keyBytes(text: string): Uint8Array {
  const k = parseKey(text); // formatKey output always parses
  if (typeof k === 'string') throw new Error('recovery key did not parse');
  return k;
}
