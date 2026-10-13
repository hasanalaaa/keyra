// The Security tab: a dashboard (password health, passkeys, recovery kit, activity) over the
// locking and reveal settings. Each tile opens its routed screen (#/security/<sub>).
import { passphraseOk } from '../lib/limits';
import { useEffect, useRef, useState } from 'preact/hooks';
import { Icon, type IconName } from '../components/Icon';
import { Button, SecretField, Section, StrengthMeter, SwitchRow } from '../components/ui';
import { Alert, Sheet, type SheetCtl } from '../components/Sheet';
import { Ready } from '../components/Ready';
import { lazy } from '../components/Lazy';
import { ApiError, api, isAwaiting } from '../lib/api';
import { usePresence } from '../lib/actions';
import { errorText, isLockedError } from '../lib/errors';
import { accountCount, clock, passkeyCount, t } from '../lib/i18n';
import { back, go } from '../lib/router';
import { lockNow, toast, useApp } from '../lib/store';
import type { ActivityEvent, Health, RecoveryInfo } from '../lib/types';
import { shortDate } from '../lib/wifi';
import { eventText, when } from './Activity';
import { minHint } from './common';
import { NavRow, TabPage, useSettings } from './prefs';

const HealthSheet = lazy(() => import('./Health').then((m) => m.HealthSheet));
const ActivitySheet = lazy(() => import('./Activity').then((m) => m.ActivitySheet));
const PasskeysSheet = lazy(() => import('./Passkeys').then((m) => m.PasskeysSheet));
const RecoverySheet = lazy(() => import('./Recovery').then((m) => m.RecoverySheet));

const AUTOLOCK = [1, 5, 15, 30, 60, 120];

interface Dash {
  health?: Health | null;
  passkeys?: number | null;
  recovery?: RecoveryInfo | null;
  latest?: ActivityEvent | null;
}

/** The dashboard's numbers, one request after another (the device serves one at a time anyway). */
function useDashboard(refresh: unknown): Dash {
  const [d, setD] = useState<Dash>({});
  useEffect(() => {
    let live = true;
    const step = async <T,>(get: () => Promise<T>): Promise<T | null> => {
      try {
        return await get();
      } catch {
        return null; // a tile without its number still opens its screen
      }
    };
    void (async () => {
      const health = await step(api.health);
      if (live) setD((x) => ({ ...x, health }));
      const passkeys = (await step(api.passkeys))?.passkeys.length ?? null;
      if (live) setD((x) => ({ ...x, passkeys }));
      const recovery = await step(api.recovery);
      if (live) setD((x) => ({ ...x, recovery }));
      const latest = (await step(api.activity))?.events[0] ?? null;
      if (live) setD((x) => ({ ...x, latest }));
    })();
    return () => {
      live = false;
    };
  }, [refresh]);
  return d;
}

export function Security({ sub }: { sub: string | null }) {
  const app = useApp();
  const { s, setS, load, save } = useSettings();
  const [ask, setAsk] = useState(false);
  const dash = useDashboard(sub === null);
  const close = () => back('/security');

  // Turning reveal protection off is itself a press (SPEC §12.3).
  const unprotect = usePresence('unprotect');
  useEffect(() => {
    const k = unprotect.phase.kind;
    if (k === 'done') void load();
    if (k !== 'idle' && k !== 'ready') unprotect.abandon();
  }, [unprotect.phase.kind]);
  // So is putting passkeys into backups (docs/research/PASSKEY-BACKUP.md); leaving them out is not.
  const passkeysOn = usePresence('passkeys_backup_on');
  useEffect(() => {
    const k = passkeysOn.phase.kind;
    if (k === 'done') void load();
    if (k !== 'idle' && k !== 'ready') passkeysOn.abandon();
  }, [passkeysOn.phase.kind]);
  const putPasskeysInBackup = () => {
    const sent = Date.now();
    api
      .putSettings({ passkeysInBackup: true })
      .then((r) => (isAwaiting(r) ? passkeysOn.watch(sent, r) : setS(r)))
      .catch((e) => !isLockedError(e) && toast(errorText(e), 'error'));
  };

  const h = dash.health;
  const hidden = h?.revealed === false; // weak and reused wait for a press (SPEC §13)
  const title = (id: number) => app.entries?.find((e) => e.id === id)?.title;

  return (
    <TabPage title={t('tabSecurity')} name="security" loaded={!!s}>
      <button type="button" class="dash-card dash-health" onClick={() => go('/security/health')}>
        <span class="dash-head">
          <span class="row-glyph">
            <Icon name="pulse" size={18} />
          </span>
          <span class="dash-title">{t('healthRow')}</span>
          <Icon name="chevron-right" size={16} class="row-chev" />
        </span>
        <span class="stats">
          <Stat n={h && !hidden ? h.reused.reduce((a, g) => a + g.length, 0) : null} label={t('statReused')} />
          <Stat n={h && !hidden ? h.weak.length : null} label={t('statWeak')} />
          <Stat n={h && h.clock ? h.old.length : null} label={t('statOld')} />
        </span>
        <span class="caption dash-foot">{h ? (hidden ? t('healthNeedsPress') : t('healthChecked', { accounts: accountCount(h.checked) })) : ' '}</span>
      </button>
      <div class="dash-grid">
        <Tile icon="key-round" label={t('passkeysRow')} value={dash.passkeys == null ? '' : passkeyCount(dash.passkeys)} onClick={() => go('/security/passkeys')} />
        <Tile
          icon="life-buoy"
          label={t('recoveryRow')}
          value={
            dash.recovery === undefined || dash.recovery === null
              ? ''
              : dash.recovery.enabled
                ? dash.recovery.created
                  ? t('recoveryOn', { date: shortDate(dash.recovery.created, app.lang) })
                  : t('recoveryOnUnknown')
                : t('recoveryOff')
          }
          off={dash.recovery?.enabled === false}
          onClick={() => go('/security/recovery')}
        />
      </div>
      <button type="button" class="dash-card dash-activity" onClick={() => go('/security/activity')}>
        <span class="dash-head">
          <span class="row-glyph">
            <Icon name="activity" size={18} />
          </span>
          <span class="dash-title">{t('activityRow')}</span>
          <Icon name="chevron-right" size={16} class="row-chev" />
        </span>
        <span class="dash-latest">
          {dash.latest ? (
            <>
              <span class="dash-latest-text">{eventText(dash.latest, title)}</span>
              <span class="caption mono-time">{when(dash.latest.at, app.lang)}</span>
            </>
          ) : (
            <span class="caption">{dash.latest === null ? t('activityNothing') : ' '}</span>
          )}
        </span>
      </button>

      {s && (
        <>
          <Section title={t('groupLocking')} footer={t('lockFoot')}>
            <NavRow icon="timer" label={t('autoLock')} value={t('autoLockAfter', { n: s.autoLockMin })} onClick={() => go('/security/autolock')} />
            <NavRow icon="lock" label={t('changePassphrase')} onClick={() => go('/security/passphrase')} />
            <SwitchRow icon="usb" label={t('lockOnUsb')} checked={s.lockOnUsb} onChange={(v) => void save({ lockOnUsb: v })} />
            <SwitchRow icon="bluetooth" label={t('lockOnBle')} checked={s.lockOnBle} onChange={(v) => void save({ lockOnBle: v })} />
            <button type="button" class="row nav-row has-icon" onClick={() => void lockNow()}>
              <span class="row-glyph">
                <Icon name="lock" size={18} />
              </span>
              <span class="row-label strong">{t('lockNow')}</span>
            </button>
          </Section>
          <Section title={t('groupSecrets')} footer={`${t('protectRevealFoot')} ${t('passkeysInBackupFoot')}`}>
            {unprotect.phase.kind === 'ready' ? (
              <div class="row">
                <Ready state="ready" deadline={unprotect.phase.deadline} total={unprotect.phase.total} title={t('unprotectPress')} onCancel={unprotect.abandon} />
              </div>
            ) : (
              <SwitchRow icon="eye-off" label={t('protectReveal')} checked={s.protectReveal} onChange={(v) => (v ? void save({ protectReveal: true }) : setAsk(true))} />
            )}
            {passkeysOn.phase.kind === 'ready' ? (
              <div class="row">
                <Ready state="ready" deadline={passkeysOn.phase.deadline} total={passkeysOn.phase.total} title={t('passkeysInBackupPress')} onCancel={passkeysOn.abandon} />
              </div>
            ) : (
              <SwitchRow
                icon="download"
                label={t('passkeysInBackup')}
                checked={s.passkeysInBackup}
                onChange={(v) => (v ? putPasskeysInBackup() : void save({ passkeysInBackup: false }))}
              />
            )}
          </Section>
        </>
      )}

      {sub === 'health' && <HealthSheet onClose={close} />}
      {sub === 'activity' && <ActivitySheet onClose={close} />}
      {sub === 'passkeys' && <PasskeysSheet onClose={close} />}
      {sub === 'recovery' && <RecoverySheet onClose={close} />}
      {sub === 'passphrase' && <PassphraseSheet onClose={close} />}
      {sub === 'autolock' && s && (
        <AutoLockSheet
          value={s.autoLockMin}
          onPick={(n) => {
            void save({ autoLockMin: n });
            close();
          }}
          onClose={close}
        />
      )}
      {ask && (
        <Alert
          title={t('unprotectTitle')}
          body={t('unprotectBody')}
          actions={[
            {
              label: t('turnOff'),
              variant: 'danger-confirm',
              run: () => {
                setAsk(false);
                const sent = Date.now();
                api
                  .putSettings({ protectReveal: false })
                  .then((r) => (isAwaiting(r) ? unprotect.watch(sent, r) : setS(r)))
                  .catch((e) => !isLockedError(e) && toast(errorText(e), 'error'));
              },
            },
          ]}
          onCancel={() => setAsk(false)}
        />
      )}
    </TabPage>
  );
}

function Stat({ n, label }: { n: number | null; label: string }) {
  return (
    <span class={`stat${n ? ' hot' : ''}`}>
      <span class="stat-n" dir="ltr">
        {n === null ? '–' : n}
      </span>
      <span class="stat-k">{label}</span>
    </span>
  );
}

function Tile({ icon, label, value, onClick, off }: { icon: IconName; label: string; value: string; onClick: () => void; off?: boolean }) {
  return (
    <button type="button" class="dash-card dash-tile" onClick={onClick}>
      <span class="row-glyph">
        <Icon name={icon} size={18} />
      </span>
      <span class="dash-title">{label}</span>
      <span class={`dash-value${off ? ' off' : ''}`}>{value || ' '}</span>
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
            {n === value && <Icon name="check" size={20} class="strong-ink" />}
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
        <div class="sticky-save">
          <Button type="submit" size="lg" full loading={busy} disabled={!ok}>
            {t('save')}
          </Button>
        </div>
      </form>
    </Sheet>
  );
}
