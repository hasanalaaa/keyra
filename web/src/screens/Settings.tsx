// Settings (DESIGN §5.9): every change saves immediately; Wi-Fi, Bluetooth pairing and erase need the button.
import { clipBytes, passphraseOk } from '../lib/limits';
import type { ComponentChildren } from 'preact';
import { useEffect, useRef, useState } from 'preact/hooks';
import { Icon } from '../components/Icon';
import { Button, CopyButton, IconButton, Section, SecretField, Segmented, Slider, Spinner, StrengthMeter, SwitchRow, TextField } from '../components/ui';
import { Alert, Sheet, useMedia, type SheetCtl } from '../components/Sheet';
import { ErrorCard, HostNotice, Ready, readyText } from '../components/Ready';
import { storedTarget, validTarget } from '../lib/ble';
import { ApiError, api, isAwaiting } from '../lib/api';
import { usePresence, useTypeAction } from '../lib/actions';
import { errorText, isLockedError } from '../lib/errors';
import { clock, t } from '../lib/i18n';
import { back, go, replace } from '../lib/router';
import { holdFastPolling, lockNow, pollNow, setLangPref, setThemePref, toast, useApp } from '../lib/store';
import type { Settings as S } from '../lib/types';
import { validWifi } from './Setup';
import { BluetoothSection } from './Bluetooth';
import { OsSelect } from '../components/HostOs';
import { minHint } from './common';
import { HomeWifiSheet } from './HomeWifi';
import { PasskeysSheet } from './Passkeys';
import { HealthSheet } from './Health';
import { ActivitySheet } from './Activity';
import { UpdateSheet } from './Update';
import { TrustedSheet } from './Trusted';
import { RecoverySheet } from './Recovery';
import { BothSequenceSheet } from './BothSequence';
import { shortDate } from '../lib/wifi';
import type { Keyboard, RecoveryInfo } from '../lib/types';
import { LayoutSheet, layoutTitle, type LayoutOutput } from './Keyboard';

const SPEEDS = [
  { value: 30, key: 'slow' },
  { value: 12, key: 'normal' },
  { value: 5, key: 'fast' },
] as const;
const AUTOLOCK = [1, 5, 15, 30, 60, 120];

type Sub = LayoutOutput | 'wifi' | 'home' | 'trusted' | 'health' | 'activity' | 'update' | 'passkeys' | 'autolock' | 'passphrase' | 'test' | 'erase' | 'recovery' | 'unprotect' | 'bothseq' | null;

export function Settings({ page, onA2hs }: { page?: boolean; onA2hs: () => void }) {
  const app = useApp();
  const desktop = useMedia('(min-width: 900px)');
  const [s, setS] = useState<S | null>(null);
  const [name, setName] = useState('');
  const [led, setLed] = useState(50);
  const [sub, setSub] = useState<Sub>(null);
  const [atTop, setAtTop] = useState(true);

  useEffect(() => {
    if (!page) return;
    const onScroll = () => setAtTop(window.scrollY < 4);
    window.addEventListener('scroll', onScroll, { passive: true });
    return () => window.removeEventListener('scroll', onScroll);
  }, [page]);

  const load = () =>
    api
      .settings()
      .then((v) => {
        setS(v);
        setName(v.deviceName);
        setLed(v.ledBrightness);
      })
      .catch((e) => !isLockedError(e) && toast(errorText(e), 'error'));
  useEffect(() => void load(), []);
  const [recovery, setRecovery] = useState<RecoveryInfo | null>(null);
  const loadRecovery = () =>
    api
      .recovery()
      .then(setRecovery)
      .catch(() => setRecovery(null));
  useEffect(() => void loadRecovery(), []);
  // SPEC §10.1; firmware before it answers 404 and the layout rows stay hidden.
  const [kb, setKb] = useState<Keyboard | null>(null);
  useEffect(() => {
    api
      .keyboard()
      .then(setKb)
      .catch((e) => !(e instanceof ApiError && e.status === 404) && !isLockedError(e) && toast(errorText(e), 'error'));
  }, []);
  const layoutName = (id: string) => {
    const l = kb?.layouts.find((x) => x.id === id);
    return l ? layoutTitle(l) : id;
  };
  // Turning reveal protection off is itself a press (SPEC §12.3).
  const unprotect = usePresence('unprotect');
  useEffect(() => {
    const k = unprotect.phase.kind;
    if (k === 'done') void load();
    if (k !== 'idle' && k !== 'ready') unprotect.abandon();
  }, [unprotect.phase.kind]);

  const save = async (patch: Partial<S>) => {
    if (!s) return;
    const prev = s;
    setS({ ...s, ...patch });
    try {
      setS(await (api.putSettings(patch) as Promise<S>));
      if ('osUsb' in patch) void pollNow(); // state.host.usbOs feeds the account sheet
    } catch (e) {
      setS(prev);
      setName(prev.deviceName);
      setLed(prev.ledBrightness);
      if (!isLockedError(e)) toast(t('saveError'), 'error');
    }
  };

  const speed = s ? SPEEDS.reduce((a, b) => (Math.abs(b.value - s.keyDelayMs) < Math.abs(a.value - s.keyDelayMs) ? b : a)).value : null;
  const d = app.device;

  const body = (
    <div class="settings">
      {s && (
        <>
          <Section title={t('groupKeyra')}>
            <div class="row field-row">
              <TextField
                label={t('deviceName')}
                value={name}
                onValue={(v) => setName(clipBytes(v))}
                onBlur={() => {
                  const v = name.trim();
                  if (!v) setName(s.deviceName); // empty is not a name: show the saved one again
                  else if (v !== s.deviceName) void save({ deviceName: v });
                }}
                onKeyDown={(e: KeyboardEvent) => e.key === 'Enter' && (e.currentTarget as HTMLInputElement).blur()}
                enterkeyhint="done"
              />
            </div>
            <NavRow label={t('wifiNetwork')} value={<bdi dir="ltr">{s.wifiSsid}</bdi>} onClick={() => setSub('wifi')} />
            {s.homeWifi && (
              <NavRow
                label={t('homeWifi')}
                value={s.homeWifi.enabled ? <bdi dir="ltr">{s.homeWifi.ssid}</bdi> : t('homeOff')}
                onClick={() => setSub('home')}
              />
            )}
          </Section>
          <Section title={t('groupSecurity')}>
            <NavRow label={t('healthRow')} onClick={() => setSub('health')} />
            <NavRow label={t('activityRow')} onClick={() => setSub('activity')} />
            <NavRow label={t('autoLock')} value={t('autoLockAfter', { n: s.autoLockMin })} onClick={() => setSub('autolock')} />
            <NavRow label={t('changePassphrase')} onClick={() => setSub('passphrase')} />
            <NavRow label={t('passkeysRow')} onClick={() => setSub('passkeys')} />
            <NavRow
              label={t('recoveryRow')}
              value={
                recovery?.enabled
                  ? recovery.created
                    ? t('recoveryOn', { date: shortDate(recovery.created, app.lang) })
                    : t('recoveryOnUnknown')
                  : t('recoveryOff')
              }
              onClick={() => setSub('recovery')}
            />
            {s.homeWifi && <NavRow label={t('trustedRow')} onClick={() => setSub('trusted')} />}
            <button type="button" class="row nav-row" onClick={() => void lockNow()}>
              <span class="row-label accent">{t('lockNow')}</span>
              <Icon name="lock" size={20} class="row-chev" />
            </button>
          </Section>
          <Section footer={t('protectRevealFoot')}>
            {unprotect.phase.kind === 'ready' ? (
              <div class="row">
                <Ready state="ready" deadline={unprotect.phase.deadline} total={unprotect.phase.total} title={t('unprotectPress')} onCancel={unprotect.abandon} />
              </div>
            ) : (
              <SwitchRow
                label={t('protectReveal')}
                checked={s.protectReveal}
                onChange={(v) => (v ? void save({ protectReveal: true }) : setSub('unprotect'))}
              />
            )}
          </Section>
          <Section footer={t('lockFoot')}>
            <SwitchRow label={t('lockOnUsb')} checked={s.lockOnUsb} onChange={(v) => void save({ lockOnUsb: v })} />
            <SwitchRow label={t('lockOnBle')} checked={s.lockOnBle} onChange={(v) => void save({ lockOnBle: v })} />
          </Section>
          <Section title={t('groupTyping')} footer={`${t('footSpeed')} ${t('footSubmit')} ${t('footOs')}${kb ? ` ${t('footLayout')}` : ''}`}>
            <div class="row">
              <span class="row-label">{t('usbComputer')}</span>
              <OsSelect label={`${t('hostOs')} · ${t('usbComputer')}`} value={s.osUsb ?? ''} onChange={(os) => void save({ osUsb: os })} />
            </div>
            {kb && (
              <>
                <NavRow label={t('layoutUsb')} value={<bdi dir="ltr">{layoutName(s.layoutUsb)}</bdi>} onClick={() => setSub('usb')} />
                <NavRow label={t('layoutBle')} value={<bdi dir="ltr">{layoutName(s.layoutBle)}</bdi>} onClick={() => setSub('ble')} />
              </>
            )}
            <div class="row stack-row">
              <span class="row-label">{t('typingSpeed')}</span>
              <Segmented label={t('typingSpeed')} options={SPEEDS.map((o) => ({ value: o.value, label: t(o.key) }))} value={speed} onChange={(v) => void save({ keyDelayMs: v })} />
            </div>
            <div class="row stack-row">
              <span class="row-label">{t('betweenFields')}</span>
              <Segmented
                label={t('betweenFields')}
                options={[
                  { value: 'tab', label: 'Tab' },
                  { value: 'enter', label: 'Enter' },
                ]}
                value={s.bothSeparator}
                onChange={(v) => void save({ bothSeparator: v })}
              />
            </div>
            <SwitchRow label={t('submitAfterBoth')} checked={s.submitAfterBoth} onChange={(v) => void save({ submitAfterBoth: v })} />
            <NavRow label={t('bothSeqRow')} value={s.bothSequence ? t('bothSeqCustom') : t('bothSeqBuiltIn')} onClick={() => setSub('bothseq')} />
            <NavRow label={t('typeTest')} onClick={() => setSub('test')} />
          </Section>
          <BluetoothSection s={s} save={save} />
          <Section title={t('groupLight')}>
            <div class="row slider-row">
              <span class="row-label">{t('ledBrightness')}</span>
              <Slider value={led} min={10} max={100} step={5} label={t('ledBrightness')} onInput={setLed} onCommit={(v) => void save({ ledBrightness: v })} />
              <span class="row-value mono">{led}%</span>
            </div>
          </Section>
        </>
      )}
      <Section title={t('groupAppearance')}>
        <div class="row stack-row">
          <span class="row-label">{t('language')}</span>
          <Segmented
            label={t('language')}
            options={[
              { value: 'auto', label: t('auto') },
              { value: 'ar', label: 'العربية' },
              { value: 'en', label: 'English' },
            ]}
            value={app.langPref}
            onChange={setLangPref}
          />
        </div>
        <div class="row stack-row">
          <span class="row-label">{t('theme')}</span>
          <Segmented
            label={t('theme')}
            options={[
              { value: 'auto', label: t('auto') },
              { value: 'light', label: t('light') },
              { value: 'dark', label: t('dark') },
            ]}
            value={app.themePref}
            onChange={setThemePref}
          />
        </div>
      </Section>
      <Section title={t('groupData')}>
        <NavRow label={t('import')} icon="upload" onClick={() => replace('/import')} />
        <NavRow label={t('backupRestore')} icon="download" onClick={() => replace('/backup')} />
      </Section>
      <Section title={t('groupAbout')}>
        {!desktop && <NavRow label={t('addToHome')} onClick={onA2hs} />}
        <NavRow label={t('updRow')} value={<bdi dir="ltr">{d?.device.version}</bdi>} onClick={() => setSub('update')} />
        <div class="row">
          <span class="row-label">{t('model')}</span>
          <span class="row-value" dir="ltr">
            {d?.device.model}
          </span>
        </div>
      </Section>
      <div class="card group danger-group">
        <button type="button" class="row nav-row" onClick={() => setSub('erase')}>
          <span class="row-label danger-text">{t('eraseRow')}</span>
        </button>
      </div>
      {sub === 'wifi' && s && (
        <WifiSheet
          ssid={s.wifiSsid}
          onClose={() => {
            setSub(null);
            void load();
          }}
        />
      )}
      {sub === 'home' && s && <HomeWifiSheet settings={s} onChange={() => void load()} onClose={() => setSub(null)} />}
      {sub === 'trusted' && <TrustedSheet onClose={() => setSub(null)} />}
      {sub === 'health' && <HealthSheet onClose={() => setSub(null)} />}
      {sub === 'activity' && <ActivitySheet onClose={() => setSub(null)} />}
      {sub === 'update' && <UpdateSheet onClose={() => setSub(null)} />}
      {sub === 'passkeys' && <PasskeysSheet onClose={() => setSub(null)} />}
      {sub === 'autolock' && s && (
        <AutoLockSheet
          value={s.autoLockMin}
          onPick={(n) => {
            void save({ autoLockMin: n });
            setSub(null);
          }}
          onClose={() => setSub(null)}
        />
      )}
      {sub === 'passphrase' && <PassphraseSheet onClose={() => setSub(null)} />}
      {sub === 'test' && <TypeTestSheet onClose={() => setSub(null)} />}
      {sub === 'bothseq' && s && <BothSequenceSheet settings={s} onSaved={setS} onClose={() => setSub(null)} />}
      {(sub === 'usb' || sub === 'ble') && s && kb && (
        <LayoutSheet
          output={sub}
          layouts={kb.layouts}
          value={sub === 'usb' ? s.layoutUsb : s.layoutBle}
          usbOs={s.osUsb ?? ''}
          onPick={(id) => {
            void save(sub === 'usb' ? { layoutUsb: id } : { layoutBle: id });
            setSub(null);
          }}
          onClose={() => setSub(null)}
        />
      )}
      {sub === 'erase' && <EraseFlow onClose={() => setSub(null)} />}
      {sub === 'recovery' && (
        <RecoverySheet
          onClose={() => {
            setSub(null);
            void loadRecovery();
          }}
        />
      )}
      {sub === 'unprotect' && (
        <Alert
          title={t('unprotectTitle')}
          body={t('unprotectBody')}
          actions={[
            {
              label: t('turnOff'),
              variant: 'danger-confirm',
              run: () => {
                setSub(null);
                const sent = Date.now();
                api
                  .putSettings({ protectReveal: false })
                  .then((r) => (isAwaiting(r) ? unprotect.watch(sent, r) : setS(r)))
                  .catch((e) => !isLockedError(e) && toast(errorText(e), 'error'));
              },
            },
          ]}
          onCancel={() => setSub(null)}
        />
      )}
    </div>
  );

  if (page) {
    return (
      <div class={`page settings-page${atTop ? ' at-top' : ''}`}>
        <header class="top-bar glass">
          <IconButton icon="chevron-left" label={t('back')} onClick={() => back('/')} />
          <span class="spacer" />
        </header>
        <h1 class="t1 page-title">{t('settings')}</h1>
        {body}
      </div>
    );
  }
  return (
    <Sheet title={t('settings')} size="lg" onClose={() => back('/')}>
      {body}
    </Sheet>
  );
}

function NavRow({ label, value, onClick, icon }: { label: string; value?: ComponentChildren; onClick: () => void; icon?: 'upload' | 'download' }) {
  return (
    <button type="button" class="row nav-row" onClick={onClick}>
      {icon && <Icon name={icon} size={20} class="row-icon" />}
      <span class="row-label">{label}</span>
      {value !== undefined && <span class="row-value">{value}</span>}
      <Icon name="chevron-right" size={16} class="row-chev" />
    </button>
  );
}

function AutoLockSheet({ value, onPick, onClose }: { value: number; onPick: (n: number) => void; onClose: () => void }) {
  return (
    <Sheet title={t('autoLock')} size="sm" onClose={onClose}>
      <div class="card" role="radiogroup" aria-label={t('autoLock')}>
        {AUTOLOCK.map((n) => (
          <button key={n} type="button" role="radio" aria-checked={n === value} class="row nav-row" onClick={() => onPick(n)}>
            <span class="row-label">{t('minutes', { n })}</span>
            {n === value && <Icon name="check" size={20} class="accent" />}
          </button>
        ))}
      </div>
    </Sheet>
  );
}

function PassphraseSheet({ onClose }: { onClose: () => void }) {
  const [cur, setCur] = useState('');
  const [next, setNext] = useState('');
  const [again, setAgain] = useState('');
  const [wrong, setWrong] = useState(false);
  const [busy, setBusy] = useState(false);
  const ctl = useRef<SheetCtl | null>(null);
  const ok = cur && passphraseOk(next) && next === again;

  const submit = async (e: Event) => {
    e.preventDefault();
    if (!ok) return;
    setBusy(true);
    setWrong(false);
    try {
      await api.passphrase(cur, next);
      toast(t('passphraseChanged'), 'ok');
      ctl.current?.close();
    } catch (err) {
      if (err instanceof ApiError && (err.code === 'wrong' || err.code === 'rate_limited')) {
        if (err.code === 'wrong') setWrong(true);
        // Wrong guesses are throttled like unlock: say how long to wait.
        if (err.retryAfterMs > 0) toast(t('rateLimited', { t: clock(err.retryAfterMs) }), 'error');
      } else if (!isLockedError(err)) toast(errorText(err), 'error');
      setBusy(false);
    }
  };

  return (
    <Sheet title={t('changePassphrase')} size="md" ctl={ctl} onClose={onClose}>
      <form class="form" onSubmit={submit}>
        <SecretField label={t('currentPassphrase')} value={cur} onValue={setCur} autocomplete="current-password" error={wrong ? t('wrongPassphrase') : null} />
        <SecretField
          label={t('newPassphrase')}
          value={next}
          onValue={setNext}
          autocomplete="new-password"
          helper={
            <>
              <StrengthMeter value={next} />
              {minHint(next, 10)}
            </>
          }
        />
        <SecretField label={t('s1Label2')} value={again} onValue={setAgain} autocomplete="new-password" error={again && next !== again ? t('mismatch') : null} />
        <Button type="submit" size="lg" full loading={busy} disabled={!ok}>
          {t('save')}
        </Button>
      </form>
    </Sheet>
  );
}

function WifiSheet({ ssid, onClose }: { ssid: string; onClose: () => void }) {
  const app = useApp();
  const [name, setName] = useState(ssid);
  const [pw, setPw] = useState('');
  const [busy, setBusy] = useState(false);
  const presence = usePresence('wifi', { doneOnDisconnect: true });
  const dropped = useRef(false);
  const ok = name.trim().length > 0 && (pw === '' || validWifi(pw)) && (name.trim() !== ssid || pw !== '');
  const done = presence.phase.kind === 'done';

  useEffect(() => {
    if (!done) return;
    return holdFastPolling();
  }, [done]);
  useEffect(() => {
    if (!done) return;
    if (!app.online) dropped.current = true;
    else if (dropped.current) onClose();
  }, [done, app.online]);

  const submit = async (e: Event) => {
    e.preventDefault();
    if (!ok) return;
    setBusy(true);
    const sent = Date.now();
    try {
      const patch: Partial<S> & { wifiPassword?: string } = {};
      if (name.trim() !== ssid) patch.wifiSsid = name.trim();
      if (pw) patch.wifiPassword = pw;
      const r = await api.putSettings(patch);
      if (isAwaiting(r)) presence.watch(sent, r);
      else onClose();
    } catch (err) {
      if (!isLockedError(err)) toast(errorText(err, 'saveError'), 'error');
    } finally {
      setBusy(false);
    }
  };

  const p = presence.phase;
  return (
    <Sheet title={t('wifiNetwork')} size="md" onClose={onClose} dismissible={p.kind !== 'ready'}>
      {p.kind === 'ready' ? (
        <Ready state="ready" deadline={p.deadline} total={p.total} title={t('pressToConfirm')} body={t('s2Notice')} onCancel={presence.abandon} />
      ) : done ? (
        <div class="form">
          <p class="callout">{t('doneBody')}</p>
          <div class="card kv">
            <div class="kv-row">
              <span class="kv-label">{t('network')}</span>
              <span class="kv-value mono" dir="ltr">
                {name.trim()}
              </span>
              <CopyButton value={() => name.trim()} />
            </div>
            {pw && (
              <div class="kv-row">
                <span class="kv-label">{t('password')}</span>
                <span class="kv-value mono" dir="ltr">
                  {pw}
                </span>
                <CopyButton value={() => pw} />
              </div>
            )}
          </div>
          <p class="waiting-row" role="status">
            <Spinner />
            {t('waiting')}
          </p>
        </div>
      ) : p.kind === 'expired' || p.kind === 'cancelled' || p.kind === 'failed' ? (
        <ErrorCard
          icon={p.kind === 'failed' ? 'triangle-alert' : 'clock'}
          tone={p.kind === 'failed' ? 'err' : 'warn'}
          title={p.kind === 'failed' ? t('genericError') : t('s3Expired')}
          body={t('s2Notice')}
          primary={{ label: t('tryAgain'), run: presence.abandon }}
          ghost={{ label: t('close'), run: onClose }}
        />
      ) : (
        <form class="form" onSubmit={submit}>
          <TextField label={t('network')} value={name} onValue={(v) => setName(clipBytes(v))} ltr autocapitalize="off" spellcheck={false} />
          <SecretField label={t('s2Label')} value={pw} onValue={setPw} autocomplete="new-password" helper={t('s2Hint')} error={pw && !validWifi(pw) ? t('s2Hint') : null} />
          <Button type="submit" size="lg" full loading={busy} disabled={!ok}>
            {t('save')}
          </Button>
        </form>
      )}
    </Sheet>
  );
}

function TypeTestSheet({ onClose }: { onClose: () => void }) {
  const app = useApp();
  const action = useTypeAction(0);
  const active = useRef(false);
  useEffect(() => {
    if (action.phase.kind === 'idle') void action.start('test', validTarget(storedTarget(), app.ble) ?? undefined).then((ok) => !ok && onClose());
  }, []);
  // Close once the test has run its course (typed dwell finished, or cancelled on the device).
  useEffect(() => {
    if (action.phase.kind !== 'idle') active.current = true;
    else if (active.current) onClose();
  }, [action.phase.kind]);
  const p = action.phase;
  return (
    <Sheet title={t('typeTest')} size="md" onClose={onClose} dismissible={p.kind !== 'ready'} hideTitle>
      {p.kind === 'error' ? (
        <ErrorCard
          icon={p.code === 'no_usb' ? 'usb' : p.code === 'no_host' ? 'bluetooth' : p.code === 'expired' ? 'clock' : 'triangle-alert'}
          tone={p.code === 'no_usb' || p.code === 'no_host' || p.code === 'expired' ? 'warn' : 'err'}
          title={p.code === 'no_usb' ? t('errNoUsbTitle') : p.code === 'no_host' ? t('errNoHostTitle') : p.code === 'expired' ? t('errExpiredTitle') : t('errFailedTitle')}
          body={p.code === 'no_usb' ? t('errNoUsbBody') : p.code === 'no_host' ? t('errNoHostBody') : p.code === 'expired' ? t('errExpiredBody') : t('errFailedBody')}
          primary={{ label: t('tryAgain'), run: () => void action.start('test', validTarget(storedTarget(), app.ble) ?? undefined) }}
          ghost={{ label: t('close'), run: onClose }}
        />
      ) : (
        <Ready
          state={p.kind === 'idle' ? 'ready' : p.kind}
          deadline={p.kind === 'ready' ? p.deadline : Date.now() + 60000}
          total={p.kind === 'ready' ? p.total : 60000}
          {...readyText(app.device, t('typeTestBody'))}
          chip={t('typeTest')}
          notice={app.device ? <HostNotice device={app.device} /> : undefined}
          onCancel={() => {
            void action.cancel();
            onClose();
          }}
        />
      )}
    </Sheet>
  );
}

function EraseFlow({ onClose }: { onClose: () => void }) {
  const presence = usePresence('factory_reset', { doneOnDisconnect: true });
  const [asked, setAsked] = useState(true);
  useEffect(() => {
    const k = presence.phase.kind;
    if (k === 'done') {
      go('/welcome');
    } else if (k === 'failed') {
      toast(t('genericError'), 'error');
      onClose();
    } else if (k === 'expired' || k === 'cancelled') onClose();
  }, [presence.phase.kind]);

  if (asked) {
    return (
      <Alert
        title={t('eraseTitle')}
        body={t('eraseSettingsBody')}
        actions={[
          {
            label: t('eraseConfirm'),
            variant: 'danger-confirm',
            run: () => {
              setAsked(false);
              void presence.start(api.factoryReset).then((ok) => !ok && onClose());
            },
          },
        ]}
        onCancel={onClose}
      />
    );
  }
  const p = presence.phase;
  if (p.kind !== 'ready') return null;
  return (
    <Sheet title={t('eraseWaitTitle')} size="md" onClose={onClose} dismissible={false} hideTitle>
      <Ready state="ready" deadline={p.deadline} total={p.total} title={t('eraseWaitTitle')} body={t('s3Body')} onCancel={() => {
        presence.abandon();
        onClose();
      }} />
    </Sheet>
  );
}
