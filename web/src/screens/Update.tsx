// Settings → Firmware update (SPEC §14). Keyra fetches the latest release from
// GitHub itself (on the home network) or takes a file from this phone; either
// way it checks the signature and the version, then waits for its button.
import { useEffect, useRef, useState } from 'preact/hooks';
import { Icon } from '../components/Icon';
import { Button, Spinner } from '../components/ui';
import { Sheet } from '../components/Sheet';
import { ErrorCard, Ready } from '../components/Ready';
import { ApiError, api } from '../lib/api';
import { usePresence } from '../lib/actions';
import { isLockedError } from '../lib/errors';
import { t, type Key } from '../lib/i18n';
import { holdFastPolling, useApp } from '../lib/store';
import type { UpdateCheck } from '../lib/types';

const MAX_IMAGE = 3 * 1024 * 1024; // one app partition

const ERRORS: Record<string, Key> = {
  bad_signature: 'updErrSignature',
  bad_image: 'updErrImage',
  downgrade: 'updErrDowngrade',
  offline: 'updErrOffline',
  network: 'updErrNetwork',
  no_release: 'updErrNoRelease',
  rate_limited: 'updErrRateLimited',
  too_large: 'updErrImage',
  busy: 'updErrBusy',
};

/** Message for an update error code (exported for tests). */
export function updateError(code: string): string {
  return t(ERRORS[code] ?? 'updErrFailed');
}

/** Quick look before sending 3 MB: an ESP-IDF app image starts with 0xE9 and fits a partition. */
export async function looksLikeFirmware(file: Blob): Promise<boolean> {
  if (file.size < 4096 || file.size > MAX_IMAGE) return false;
  const head = new Uint8Array(await file.slice(0, 1).arrayBuffer());
  return head[0] === 0xe9;
}

type Step =
  | { kind: 'idle' }
  | { kind: 'checking' }
  | { kind: 'checked'; info: UpdateCheck }
  | { kind: 'receiving'; source: 'github' | 'upload'; done: number; total: number }
  | { kind: 'failed'; code: string };

const codeOf = (e: unknown) => (e instanceof ApiError ? e.code : 'failed');

export function UpdateSheet({ onClose }: { onClose: () => void }) {
  const app = useApp();
  const d = app.device;
  const [step, setStep] = useState<Step>({ kind: 'idle' });
  const presence = usePresence('update', { doneOnDisconnect: true });
  const file = useRef<HTMLInputElement>(null);
  const online = !!d?.net?.home?.connected;

  const install = async () => {
    setStep({ kind: 'idle' });
    await presence.start(api.updateApply);
  };

  // While Keyra downloads, its state says how far it is: poll it briskly.
  const downloading = step.kind === 'receiving' && step.source === 'github';
  useEffect(() => (downloading ? holdFastPolling() : undefined), [downloading]);
  useEffect(() => {
    if (!downloading) return;
    const u = d?.update;
    if (!u || u.source !== 'github') return;
    if (u.phase === 'receiving') setStep({ kind: 'receiving', source: 'github', done: u.done, total: u.total });
    else if (u.phase === 'failed') setStep({ kind: 'failed', code: u.error });
    else if (u.phase === 'staged') void install();
  }, [d?.update?.phase, d?.update?.done]);

  const check = async () => {
    setStep({ kind: 'checking' });
    try {
      setStep({ kind: 'checked', info: await api.updateCheck() });
    } catch (e) {
      if (!isLockedError(e)) setStep({ kind: 'failed', code: codeOf(e) });
    }
  };

  const download = async () => {
    try {
      await api.updateDownload();
      setStep({ kind: 'receiving', source: 'github', done: 0, total: 0 });
    } catch (e) {
      if (!isLockedError(e)) setStep({ kind: 'failed', code: codeOf(e) });
    }
  };

  const upload = async (f: File) => {
    if (!(await looksLikeFirmware(f))) return setStep({ kind: 'failed', code: 'bad_image' });
    setStep({ kind: 'receiving', source: 'upload', done: 0, total: f.size });
    try {
      await api.updateUpload(f, (done, total) => setStep({ kind: 'receiving', source: 'upload', done, total }));
      await install();
    } catch (e) {
      if (!isLockedError(e)) setStep({ kind: 'failed', code: codeOf(e) });
    }
  };

  const p = presence.phase;
  let body;
  if (p.kind === 'ready') {
    body = (
      <Ready
        state="ready"
        deadline={p.deadline}
        total={p.total}
        title={t('updPressTitle')}
        body={t('updPressBody', { version: d?.update?.version ?? '' })}
        onCancel={() => {
          presence.abandon();
          onClose();
        }}
      />
    );
  } else if (p.kind === 'done') {
    body = <Ready state="typed" title="" doneTitle={t('updDoneTitle')} doneBody={t('updDoneBody')} />;
  } else if (p.kind === 'expired' || p.kind === 'cancelled' || p.kind === 'failed') {
    body = (
      <ErrorCard
        icon={p.kind === 'failed' ? 'triangle-alert' : 'clock'}
        tone={p.kind === 'failed' ? 'err' : 'warn'}
        title={p.kind === 'failed' ? t('updErrFailed') : t('s3Expired')}
        body={t('updRetryBody')}
        primary={{ label: t('tryAgain'), run: () => void install() }}
        ghost={{ label: t('close'), run: onClose }}
      />
    );
  } else if (step.kind === 'failed') {
    body = (
      <ErrorCard
        icon="triangle-alert"
        tone="err"
        title={t('updErrTitle')}
        body={updateError(step.code)}
        primary={{ label: t('tryAgain'), run: () => setStep({ kind: 'idle' }) }}
        ghost={{ label: t('close'), run: onClose }}
      />
    );
  } else if (step.kind === 'receiving') {
    const pct = step.total ? Math.min(100, Math.round((step.done / step.total) * 100)) : 0;
    body = (
      <div class="form update">
        <p class="callout center">{step.source === 'github' ? t('updDownloading') : t('updUploading')}</p>
        <div class="rotate-bar" role="progressbar" aria-valuemin={0} aria-valuemax={100} aria-valuenow={pct}>
          <span style={{ inlineSize: `${pct}%` }} />
        </div>
        <p class="caption center">{step.total ? `${pct}%` : t('updStarting')}</p>
        <p class="caption">{t('updKeepOpen')}</p>
      </div>
    );
  } else {
    const info = step.kind === 'checked' ? step.info : null;
    body = (
      <div class="form update">
        <div class="card update-summary">
          <Icon name="shield-check" size={28} />
          <span class="row-label">
            <strong>{t('updCurrent', { version: d?.device.version ?? '' })}</strong>
            <span class="caption">{t('updSigned')}</span>
          </span>
        </div>
        {info ? (
          info.newer ? (
            <section class="group">
              <h2 class="section-head">{t('updAvailable', { version: info.latest })}</h2>
              {info.notes && (
                <p class="update-notes" dir="auto">
                  {info.notes}
                </p>
              )}
              <Button full icon="download" onClick={() => void download()}>
                {t('updInstall', { version: info.latest })}
              </Button>
            </section>
          ) : (
            <p class="callout center">
              {t('updUpToDate')}{' '}
              <button type="button" class="link-btn" onClick={() => void download()}>
                {t('updReinstall')}
              </button>
            </p>
          )
        ) : (
          <Button full disabled={!online || step.kind === 'checking'} onClick={() => void check()}>
            {step.kind === 'checking' ? <Spinner size={18} /> : t('updCheck')}
          </Button>
        )}
        {!online && <p class="caption">{t('updNeedsHome')}</p>}
        <section class="group">
          <h2 class="section-head">{t('updFromFile')}</h2>
          <p class="group-foot">{t('updFromFileWhy')}</p>
          <Button variant="secondary" full icon="upload" onClick={() => file.current?.click()}>
            {t('updChooseFile')}
          </Button>
          <input
            ref={file}
            type="file"
            accept=".bin,application/octet-stream"
            hidden
            onChange={(e) => {
              const f = e.currentTarget.files?.[0];
              e.currentTarget.value = '';
              if (f) void upload(f);
            }}
          />
        </section>
      </div>
    );
  }

  return (
    <Sheet title={t('updRow')} size="md" onClose={onClose}>
      {body}
    </Sheet>
  );
}
