// Onboarding (DESIGN §5.2): passphrase → Wi-Fi password → press the button → reconnect → unlock.
import { useEffect, useRef, useState } from 'preact/hooks';
import { Button, CopyButton, IconButton, Notice, SecretField, Spinner, StrengthMeter } from '../components/ui';
import { ErrorCard, Ready } from '../components/Ready';
import { api } from '../lib/api';
import { usePresence } from '../lib/actions';
import { generateWifiPassword } from '../lib/generator';
import { t } from '../lib/i18n';
import { back, go, replace } from '../lib/router';
import { holdFastPolling, unlock, useApp } from '../lib/store';
import { LangButton, minHint } from './common';

// In memory only (never persisted): survives step changes, not reloads.
// `sent`: this tab asked the device to set up, so `initialized` turning true is our own commit and the
// app must stay here for the done/reconnect screen (the poll can report it before the result is read).
const draft = { passphrase: '', confirm: '', wifi: '', sent: false, committed: false };

/** True while the just-initialized device still needs the done/reconnect screen. */
export const setupInProgress = (): boolean => draft.sent || draft.committed;

export function validWifi(pw: string): boolean {
  return pw.length >= 8 && pw.length <= 63 && /^[\x20-\x7e]+$/.test(pw) && pw !== 'keyra1234';
}

/** "Keyra-XXXX" from the last two MAC bytes (the firmware's default SSID). */
function ssidFromMac(mac: string): string {
  const hex = mac.replace(/[^0-9a-f]/gi, '').toUpperCase();
  return `Keyra-${hex.slice(-4)}`;
}

function Dots({ step }: { step: number }) {
  return (
    <div class="step-dots" aria-hidden="true">
      {[1, 2, 3].map((i) => (
        <span key={i} class={i === step ? 'on' : undefined} />
      ))}
    </div>
  );
}

function Chrome({ step, onBack, children, cta }: { step: number; onBack?: () => void; children: preact.ComponentChildren; cta?: preact.ComponentChildren }) {
  return (
    <div class="page glow-page">
      <div class="top-bar-plain">
        {onBack ? <IconButton icon="chevron-left" label={t('back')} onClick={onBack} /> : <span class="icon-btn-space" />}
        <Dots step={step} />
        <LangButton />
      </div>
      <div class="hero-col">
        <div class="hero-card setup">
          <p class="step-label">{t('stepLabel', { n: step })}</p>
          {children}
          {cta && <div class="sticky-cta">{cta}</div>}
        </div>
      </div>
    </div>
  );
}

export function Setup({ step }: { step: number }) {
  if (step > 1 && !draft.committed && !draft.passphrase) return <Redirect to="/setup/1" />;
  if (step === 1) return <StepPassphrase />;
  if (step === 2) return <StepWifi />;
  return <StepButton />;
}

function Redirect({ to }: { to: string }) {
  useEffect(() => replace(to), [to]);
  return null;
}

function StepPassphrase() {
  const [pass, setPass] = useState(draft.passphrase);
  const [confirm, setConfirm] = useState(draft.confirm);
  const [touched, setTouched] = useState(false);
  const long = Array.from(pass).length >= 10;
  const ok = long && pass === confirm;
  const next = (e?: Event) => {
    e?.preventDefault();
    setTouched(true);
    if (!ok) return;
    draft.passphrase = pass;
    draft.confirm = confirm;
    go('/setup/2');
  };
  return (
    <Chrome
      step={1}
      onBack={() => back('/welcome')}
      cta={
        <Button size="lg" full disabled={!ok} onClick={next}>
          {t('continue')}
        </Button>
      }
    >
      <h1 class="t1">{t('s1Title')}</h1>
      <p class="callout">{t('s1Body')}</p>
      <form class="form" onSubmit={next}>
        <SecretField
          label={t('s1Label1')}
          value={pass}
          onValue={setPass}
          autocomplete="new-password"
          autofocus
          enterkeyhint="next"
          helper={
            <>
              <StrengthMeter value={pass} />
              {minHint(pass, 10)}
            </>
          }
        />
        <SecretField
          label={t('s1Label2')}
          value={confirm}
          onValue={setConfirm}
          onBlur={() => confirm && setTouched(true)}
          autocomplete="new-password"
          enterkeyhint="done"
          error={touched && confirm && pass !== confirm ? t('mismatch') : null}
        />
        <Notice tone="warn">{t('s1Notice')}</Notice>
        <button type="submit" hidden />
      </form>
    </Chrome>
  );
}

function StepWifi() {
  if (!draft.wifi) draft.wifi = generateWifiPassword();
  const [wifi, setWifi] = useState(draft.wifi);
  const [touched, setTouched] = useState(false);
  const ok = validWifi(wifi);
  const next = (e?: Event) => {
    e?.preventDefault();
    setTouched(true);
    if (!ok) return;
    draft.wifi = wifi;
    go('/setup/3');
  };
  return (
    <Chrome
      step={2}
      onBack={() => back('/setup/1')}
      cta={
        <Button size="lg" full disabled={!ok} onClick={next}>
          {t('continue')}
        </Button>
      }
    >
      <h1 class="t1">{t('s2Title')}</h1>
      <p class="callout">{t('s2Body')}</p>
      <form class="form" onSubmit={next}>
        <SecretField
          label={t('s2Label')}
          value={wifi}
          onValue={setWifi}
          onBlur={() => setTouched(true)}
          reveal
          autocomplete="off"
          enterkeyhint="done"
          error={touched && !ok ? t('s2Hint') : null}
          helper={t('s2Hint')}
          extraEnd={<CopyButton value={() => wifi} />}
        />
        <Button variant="secondary" icon="refresh-cw" onClick={() => setWifi(generateWifiPassword())}>
          {t('s2Suggest')}
        </Button>
        <Notice tone="accent" icon="wifi">
          {t('s2Notice')}
        </Notice>
        <button type="submit" hidden />
      </form>
    </Chrome>
  );
}

function StepButton() {
  const app = useApp();
  const presence = usePresence('setup');
  const started = useRef(false);
  const [committed, setCommitted] = useState(draft.committed);
  const begin = async () => {
    draft.sent = await presence.start(() => api.setup(draft.passphrase, draft.wifi));
  };

  useEffect(() => {
    if (started.current || draft.committed) return;
    started.current = true;
    void begin();
  }, []);

  useEffect(() => {
    if (presence.phase.kind === 'done') {
      draft.committed = true;
      setCommitted(true);
    }
  }, [presence.phase.kind]);

  if (committed) return <Reconnect ssid={ssidFromMac(app.device?.device.mac ?? '')} />;

  const p = presence.phase;
  return (
    <Chrome step={3}>
      {p.kind === 'expired' || p.kind === 'cancelled' || p.kind === 'failed' ? (
        <ErrorCard
          icon="clock"
          tone="warn"
          title={t('s3Title')}
          body={p.kind === 'failed' ? t('genericError') : t('s3Expired')}
          primary={{ label: t('tryAgain'), run: () => void begin() }}
          ghost={{ label: t('back'), run: () => back('/setup/2') }}
        />
      ) : (
        <Ready
          state="ready"
          deadline={p.kind === 'ready' ? p.deadline : Date.now() + 60000}
          total={p.kind === 'ready' ? p.total : 60000}
          title={t('s3Title')}
          body={t('s3Body')}
          onCancel={() => {
            presence.abandon();
            draft.sent = false;
            back('/setup/2');
          }}
        />
      )}
    </Chrome>
  );
}

function Reconnect({ ssid }: { ssid: string }) {
  const app = useApp();
  const dropped = useRef(false);
  const busy = useRef(false);
  const [since] = useState(Date.now());

  useEffect(() => holdFastPolling(), []);
  useEffect(() => {
    if (!app.online) dropped.current = true;
    // Rejoined after the AP restart (or the link never dropped): unlock with the in-memory passphrase.
    if (app.online && (dropped.current || Date.now() - since > 8000) && !busy.current) {
      busy.current = true;
      unlock(draft.passphrase)
        .catch(() => replace('/unlock'))
        .finally(() => {
          draft.passphrase = draft.confirm = draft.wifi = '';
          draft.sent = draft.committed = false;
        });
    }
  }, [app.online, app.device]);

  return (
    <div class="page glow-page">
      <div class="hero-col">
        <div class="hero-card setup">
          <Ready state="typed" title="" doneTitle={t('doneTitle')} doneBody={t('doneBody')} />
          <div class="card kv">
            <div class="kv-row">
              <span class="kv-label">{t('network')}</span>
              <span class="kv-value mono" dir="ltr">
                {ssid}
              </span>
              <CopyButton value={() => ssid} />
            </div>
            <div class="kv-row">
              <span class="kv-label">{t('password')}</span>
              <span class="kv-value mono" dir="ltr">
                {draft.wifi}
              </span>
              <CopyButton value={() => draft.wifi} />
            </div>
          </div>
          <p class="waiting-row" role="status">
            <Spinner />
            {t('waiting')}
          </p>
        </div>
      </div>
    </div>
  );
}
