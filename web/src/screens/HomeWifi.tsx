// Settings → Home Wi‑Fi (SPEC §8.2): join the home network so keyra.local opens from any
// device on it. Joining, changing and turning it off each need a press of Keyra's button.
import { useEffect, useState } from 'preact/hooks';
import { Icon } from '../components/Icon';
import { Button, Rich, Section, SecretField, Spinner, SwitchRow } from '../components/ui';
import { Sheet } from '../components/Sheet';
import { ErrorCard, Ready } from '../components/Ready';
import { api, type Awaiting } from '../lib/api';
import { usePresence } from '../lib/actions';
import { errorText, isLockedError } from '../lib/errors';
import { t } from '../lib/i18n';
import { holdFastPolling, toast, useApp } from '../lib/store';
import type { Network, Settings as S } from '../lib/types';
import { signalBars, validHomePassword } from '../lib/wifi';

export function SignalBars({ rssi }: { rssi: number }) {
  const n = signalBars(rssi);
  return (
    <svg class="bars" viewBox="0 0 20 16" width="20" height="16" role="img" aria-label={t('signalOf', { n })}>
      {[0, 1, 2, 3].map((i) => (
        <rect key={i} x={i * 5 + 1} y={12 - i * 4} width="3" height={4 + i * 4} rx="1" class={i < n ? 'on' : ''} />
      ))}
    </svg>
  );
}

type View = { kind: 'main' } | { kind: 'pick' } | { kind: 'password'; net: Network };

export function HomeWifiSheet({ settings, onChange, onClose }: { settings: S; onChange: () => void; onClose: () => void }) {
  const app = useApp();
  const [view, setView] = useState<View>({ kind: 'main' });
  const [target, setTarget] = useState<{ enabled: boolean; ssid: string }>({ enabled: false, ssid: '' });
  const [apMode, setApMode] = useState(settings.apMode);
  const presence = usePresence('home_wifi');
  const home = app.device?.net?.home ?? null;
  const p = presence.phase;

  useEffect(() => holdFastPolling(), []); // live status while the sheet is open
  useEffect(() => {
    if (p.kind !== 'done') return;
    onChange();
    setView({ kind: 'main' });
    presence.abandon();
  }, [p.kind]);

  const send = async (body: { enabled: boolean; ssid?: string; password?: string }) => {
    setTarget({ enabled: body.enabled, ssid: body.ssid ?? settings.homeWifi.ssid });
    const sent = Date.now();
    try {
      const r: Awaiting = await api.putHomeWifi(body);
      presence.watch(sent, r);
    } catch (e) {
      if (!isLockedError(e)) toast(errorText(e, 'homeFailed'), 'error');
    }
  };

  const toggle = (on: boolean) => {
    if (!on) return void send({ enabled: false });
    // A stored network is rejoined with its saved password; otherwise pick one first.
    if (settings.homeWifi.ssid) void send({ enabled: true });
    else setView({ kind: 'pick' });
  };

  const saveApMode = async (always: boolean) => {
    const next = always ? 'always' : 'fallback';
    setApMode(next);
    try {
      await api.putSettings({ apMode: next });
      onChange();
    } catch (e) {
      setApMode(settings.apMode);
      if (!isLockedError(e)) toast(t('saveError'), 'error');
    }
  };

  let body;
  if (p.kind === 'ready') {
    body = (
      <Ready
        state="ready"
        deadline={p.deadline}
        total={p.total}
        title={target.enabled ? t('homePressJoin', { ssid: target.ssid }) : t('homePressOff')}
        body={t(target.enabled ? 'homePressBody' : 'homePressOffBody')}
        onCancel={presence.abandon}
      />
    );
  } else if (p.kind === 'failed' || p.kind === 'expired' || p.kind === 'cancelled') {
    body = (
      <ErrorCard
        icon={p.kind === 'failed' ? 'triangle-alert' : 'clock'}
        tone={p.kind === 'failed' ? 'err' : 'warn'}
        title={p.kind === 'failed' ? t('homeFailed') : t('s3Expired')}
        body={t('homePressBody')}
        primary={{ label: t('tryAgain'), run: presence.abandon }}
        ghost={{ label: t('close'), run: onClose }}
      />
    );
  } else if (view.kind === 'pick') {
    body = <Picker onPick={(net) => setView({ kind: 'password', net })} />;
  } else if (view.kind === 'password') {
    body = <JoinForm net={view.net} onJoin={(password) => send({ enabled: true, ssid: view.net.ssid, password })} />;
  } else {
    const enabled = settings.homeWifi.enabled;
    body = (
      <div class="form home-wifi">
        <p class="callout">{t('homeIntro')}</p>
        <div class="card">
          <SwitchRow label={t('homeUse')} checked={enabled} onChange={toggle} />
        </div>
        {enabled && (
          <>
            <div class="card kv home-status" role="status">
              <div class="kv-row">
                <span class="kv-label">{t('network')}</span>
                <span class="kv-value" dir="ltr">
                  {settings.homeWifi.ssid}
                </span>
                <span class={`chip ${home?.connected ? 'chip-ok' : home?.error ? 'chip-err' : 'chip-neutral'}`}>
                  {home?.connected ? t('homeConnected') : home?.error ? t('homeOffline') : t('homeConnecting')}
                </span>
              </div>
              {!home?.connected && home?.error && (
                <p class="kv-row caption home-error">
                  {t(home.error === 'wrong_password' ? 'homeWrongPassword' : home.error === 'not_found' ? 'homeNotFound' : 'homeJoinFailed')}
                </p>
              )}
              {home?.connected && home.ip && (
                <div class="kv-row">
                  <span class="kv-label">{t('homeAddress')}</span>
                  <span class="kv-value mono" dir="ltr">
                    {home.ip}
                  </span>
                </div>
              )}
              {home?.connected && home.rssi !== null && (
                <div class="kv-row">
                  <span class="kv-label">{t('homeSignal')}</span>
                  <span class="kv-value">
                    <SignalBars rssi={home.rssi} />
                  </span>
                </div>
              )}
            </div>
            {home?.connected && (
              <p class="callout home-hint">
                <Rich text={t('homeHint', { ssid: settings.homeWifi.ssid })} />
              </p>
            )}
            <div class="card">
              <button type="button" class="row nav-row" onClick={() => setView({ kind: 'pick' })}>
                <span class="row-label">{t('homeChange')}</span>
                <Icon name="chevron-right" size={16} class="row-chev" />
              </button>
            </div>
          </>
        )}
        <Section footer={t('keepApFoot')}>
          <SwitchRow label={t('keepAp')} checked={apMode === 'always'} onChange={(v) => void saveApMode(v)} />
        </Section>
      </div>
    );
  }

  const sub = view.kind !== 'main' && p.kind === 'idle';
  return (
    <Sheet
      title={view.kind === 'pick' ? t('homeChoose') : t('homeWifi')}
      size="md"
      onClose={sub ? () => setView({ kind: 'main' }) : onClose}
      dismissible={p.kind !== 'ready'}
    >
      {body}
    </Sheet>
  );
}

function Picker({ onPick }: { onPick: (n: Network) => void }) {
  const [nets, setNets] = useState<Network[] | null>(null);
  const [busy, setBusy] = useState(false);
  const scan = async () => {
    setBusy(true);
    try {
      setNets(await api.wifiScan());
    } catch (e) {
      if (!isLockedError(e)) toast(errorText(e), 'error');
      setNets((n) => n ?? []);
    } finally {
      setBusy(false);
    }
  };
  useEffect(() => void scan(), []);

  return (
    <div class="form picker">
      {nets === null ? (
        <p class="waiting-row" role="status">
          <Spinner />
          {t('homeScanning')}
        </p>
      ) : nets.length === 0 ? (
        <p class="callout center">{t('homeNone')}</p>
      ) : (
        <ul class="card rows net-list">
          {nets.map((n) => (
            <li key={n.ssid}>
              <button type="button" class="row nav-row net-row" disabled={!n.secure} onClick={() => onPick(n)}>
                <SignalBars rssi={n.rssi} />
                <span class="row-label">
                  <bdi dir="ltr">{n.ssid}</bdi>
                  {!n.secure && <span class="caption net-open">{t('homeOpenNet')}</span>}
                </span>
                {n.secure && <Icon name="lock" size={16} class="row-chev" />}
              </button>
            </li>
          ))}
        </ul>
      )}
      <Button variant="secondary" full loading={busy && nets !== null} disabled={busy} onClick={() => void scan()}>
        {t('homeRescan')}
      </Button>
    </div>
  );
}

function JoinForm({ net, onJoin }: { net: Network; onJoin: (password: string) => Promise<void> }) {
  const [pw, setPw] = useState('');
  const [busy, setBusy] = useState(false);
  const ok = validHomePassword(pw);
  const submit = async (e: Event) => {
    e.preventDefault();
    if (!ok || busy) return;
    setBusy(true);
    await onJoin(pw);
    setBusy(false);
  };
  return (
    <form class="form" onSubmit={submit}>
      <SecretField
        label={t('homePassLabel', { ssid: net.ssid })}
        value={pw}
        onValue={setPw}
        autocomplete="off"
        autofocus
        helper={t('homePassHint')}
        error={pw && !ok ? t('homePassHint') : null}
      />
      <Button type="submit" size="lg" full loading={busy} disabled={!ok}>
        {t('homeJoin')}
      </Button>
    </form>
  );
}
