// The Device tab: Keyra itself — its Wi‑Fi and the home network, Bluetooth, the computers' keyboards,
// backup and restore, apps and NFC tags, light, appearance, firmware, erase. Sub-screens are routed
// (#/device/<sub>) so Back closes them.
import { clipBytes } from '../lib/limits';
import { useEffect, useRef, useState } from 'preact/hooks';
import { ContourTile, Icon } from '../components/Icon';
import { Button, CopyButton, Section, SecretField, Segmented, Slider, Spinner, TextField } from '../components/ui';
import { Alert, Sheet, useMedia } from '../components/Sheet';
import { ErrorCard, Ready } from '../components/Ready';
import { OsSelect } from '../components/HostOs';
import { lazy } from '../components/Lazy';
import { ApiError, api, isAwaiting } from '../lib/api';
import { usePresence } from '../lib/actions';
import { errorText, isLockedError } from '../lib/errors';
import { t } from '../lib/i18n';
import { back, go } from '../lib/router';
import { holdFastPolling, setLangPref, setThemePref, toast, useApp } from '../lib/store';
import type { Keyboard, Settings as S } from '../lib/types';
import { layoutTitle } from '../lib/keyboard';
import { shortDate, validWifi } from '../lib/wifi';
import { BluetoothSection, PairSheet } from './Bluetooth';
import { IconRow, NavRow, TabPage, useSettings } from './prefs';

const HomeWifiSheet = lazy(() => import('./HomeWifi').then((m) => m.HomeWifiSheet));
const UpdateSheet = lazy(() => import('./Update').then((m) => m.UpdateSheet));
const TrustedSheet = lazy(() => import('./Trusted').then((m) => m.TrustedSheet));
const TokensSheet = lazy(() => import('./Tokens').then((m) => m.TokensSheet));
const TagsSheet = lazy(() => import('./Tags').then((m) => m.TagsSheet));
const LayoutSheet = lazy(() => import('./Keyboard').then((m) => m.LayoutSheet));

export function Device({ sub, onA2hs }: { sub: string | null; onA2hs: () => void }) {
  const app = useApp();
  const desktop = useMedia('(min-width: 900px)');
  const { s, load, save } = useSettings();
  const [name, setName] = useState('');
  const [led, setLed] = useState(50);
  // Shown again from the settings after a refused save, too.
  useEffect(() => {
    if (!s) return;
    setName(s.deviceName);
    setLed(s.ledBrightness);
  }, [s]);
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
  const close = () => back('/device');
  const d = app.device;
  const staged = d?.update?.phase === 'staged';
  const bonds = app.ble?.bonds ?? [];

  return (
    <TabPage title={t('tabDevice')} name="device" loaded={!!s}>
      <div class="device-id">
        <ContourTile size={56} />
        <div class="device-id-text">
          <span class="headline" dir="auto">
            {d?.device.name}
          </span>
          <span class="caption mono" dir="ltr">
            {[d?.device.model, d?.device.version].filter(Boolean).join(' · ')}
          </span>
        </div>
      </div>
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
          </Section>
          <Section title={t('groupWifi')}>
            <NavRow icon="wifi" label={t('keyraWifi')} value={<bdi dir="ltr">{s.wifiSsid}</bdi>} onClick={() => go('/device/wifi')} />
            {s.homeWifi && (
              <NavRow
                icon="house"
                label={t('homeWifi')}
                value={s.homeWifi.enabled ? <bdi dir="ltr">{s.homeWifi.ssid}</bdi> : t('homeOff')}
                onClick={() => go('/device/home')}
              />
            )}
            {s.homeWifi && <NavRow icon="monitor" label={t('trustedRow')} onClick={() => go('/device/trusted')} />}
          </Section>
          <BluetoothSection s={s} save={save} />
          <Section title={t('groupKeyboard')} footer={`${t('footOs')}${kb ? ` ${t('footLayout')}` : ''}`}>
            <IconRow icon="monitor" label={t('usbComputer')}>
              <OsSelect label={`${t('hostOs')} · ${t('usbComputer')}`} value={s.osUsb ?? ''} onChange={(os) => void save({ osUsb: os })} />
            </IconRow>
            {kb && (
              <>
                <NavRow icon="keyboard" label={t('layoutUsb')} value={<bdi dir="ltr">{layoutName(s.layoutUsb)}</bdi>} onClick={() => go('/device/layout-usb')} />
                <NavRow icon="keyboard" label={t('layoutBle')} value={<bdi dir="ltr">{layoutName(s.layoutBle)}</bdi>} onClick={() => go('/device/layout-ble')} />
              </>
            )}
          </Section>
          <Section title={t('groupBackup')}>
            <NavRow
              icon="download"
              label={t('backupNowRow')}
              value={app.backupAt ? shortDate(app.backupAt, app.lang) : app.backupAt === 0 ? t('backupNeverShort') : undefined}
              onClick={() => go('/backup')}
            />
            <NavRow icon="upload" label={t('restoreRow')} onClick={() => go('/restore')} />
            <NavRow icon="import" label={t('importRow')} onClick={() => go('/import')} />
          </Section>
          <Section title={t('groupConnected')}>
            <NavRow icon="bot" label={t('appsRow')} onClick={() => go('/device/apps')} />
            <NavRow icon="nfc" label={t('tagsRow')} onClick={() => go('/device/tags')} />
          </Section>
          <Section title={t('groupLight')}>
            <div class="row slider-row has-icon">
              <span class="row-glyph">
                <Icon name="sun" size={18} />
              </span>
              <span class="row-label">{t('ledBrightness')}</span>
              <Slider value={led} min={10} max={100} step={5} label={t('ledBrightness')} onInput={setLed} onCommit={(v) => void save({ ledBrightness: v })} />
              <span class="row-value mono">{led}%</span>
            </div>
          </Section>
        </>
      )}
      <Section title={t('groupAppearance')}>
        <IconRow icon="languages" label={t('language')} stack>
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
        </IconRow>
        <IconRow icon="moon" label={t('theme')} stack>
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
        </IconRow>
      </Section>
      <Section title={t('groupAbout')}>
        <NavRow icon="refresh-cw" label={t('updRow')} badge={staged ? t('updReadyBadge') : undefined} value={<bdi dir="ltr">{d?.device.version}</bdi>} onClick={() => go('/device/update')} />
        {!desktop && <NavRow icon="smartphone" label={t('addToHome')} onClick={onA2hs} />}
        <div class="row has-icon">
          <span class="row-glyph">
            <Icon name="info" size={18} />
          </span>
          <span class="row-label">{t('model')}</span>
          <span class="row-value" dir="ltr">
            {d?.device.model}
          </span>
        </div>
      </Section>
      <div class="card group danger-group">
        <NavRow icon="trash-2" label={t('eraseRow')} danger onClick={() => go('/device/erase')} />
      </div>

      {sub === 'wifi' && s && (
        <WifiSheet
          ssid={s.wifiSsid}
          onClose={() => {
            close();
            void load();
          }}
        />
      )}
      {sub === 'home' && s && <HomeWifiSheet settings={s} onChange={() => void load()} onClose={close} />}
      {sub === 'trusted' && <TrustedSheet onClose={close} />}
      {sub === 'apps' && <TokensSheet onClose={close} />}
      {sub === 'tags' && <TagsSheet onClose={close} />}
      {sub === 'update' && <UpdateSheet onClose={close} />}
      {sub === 'pair' && <PairSheet name={d?.device.name ?? 'Keyra'} before={bonds.map((b) => b.addr)} onClose={close} />}
      {(sub === 'layout-usb' || sub === 'layout-ble') && s && kb && (
        <LayoutSheet
          output={sub === 'layout-usb' ? 'usb' : 'ble'}
          layouts={kb.layouts}
          value={sub === 'layout-usb' ? s.layoutUsb : s.layoutBle}
          usbOs={s.osUsb ?? ''}
          onPick={(id) => {
            void save(sub === 'layout-usb' ? { layoutUsb: id } : { layoutBle: id });
            close();
          }}
          onClose={close}
        />
      )}
      {sub === 'erase' && <EraseFlow onClose={close} />}
    </TabPage>
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
    <Sheet title={t('keyraWifi')} size="md" onClose={onClose} dismissible={p.kind !== 'ready'}>
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
          <div class="sticky-save">
            <Button type="submit" size="lg" full loading={busy} disabled={!ok}>
              {t('save')}
            </Button>
          </div>
        </form>
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
      <Ready
        state="ready"
        deadline={p.deadline}
        total={p.total}
        title={t('eraseWaitTitle')}
        body={t('s3Body')}
        onCancel={() => {
          presence.abandon();
          onClose();
        }}
      />
    </Sheet>
  );
}
