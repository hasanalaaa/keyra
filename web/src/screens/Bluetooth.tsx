// Settings → Bluetooth (SPEC §8.1, DESIGN §5.9): on/off, where to type, paired devices, pairing.
import { Fragment } from 'preact';
import { useEffect, useRef, useState } from 'preact/hooks';
import { Icon } from '../components/Icon';
import { IconButton, Section, Segmented, SwitchRow } from '../components/ui';
import { Alert, Sheet } from '../components/Sheet';
import { ErrorCard, Ready } from '../components/Ready';
import { api } from '../lib/api';
import { usePresence } from '../lib/actions';
import { deviceLabel, newBond, sortBonds } from '../lib/ble';
import { errorText, isLockedError } from '../lib/errors';
import { t } from '../lib/i18n';
import { loadBle, toast, useApp } from '../lib/store';
import type { BleBond, HostOs, Output, Settings } from '../lib/types';
import { OsSelect } from '../components/HostOs';
import { guessOs } from '../lib/hostos';

const MAX_BONDS = 4;
const POLL_MS = 1500;

export function BluetoothSection({ s, save }: { s: Settings; save: (patch: Partial<Settings>) => Promise<void> }) {
  const app = useApp();
  const [pairing, setPairing] = useState(false);
  const [forget, setForget] = useState<BleBond | null>(null);
  useEffect(() => void loadBle(), [s.bleEnabled]);

  const info = app.ble;
  const connected = info?.connected?.addr ?? null;
  const bonds = info ? sortBonds(info.bonds, connected) : [];
  const full = bonds.length >= MAX_BONDS;
  const lastUsed = (b: BleBond) => {
    if (b.addr === connected) return t('bleConnected');
    if (!b.lastSeen) return t('blePaired');
    const day = new Date(b.lastSeen * 1000).toLocaleDateString(app.lang === 'ar' ? 'ar-u-nu-latn' : 'en-US', { day: 'numeric', month: 'short' });
    return t('bleLastUsed', { date: day });
  };

  const setOs = async (b: BleBond, os: HostOs) => {
    try {
      await api.bleSetOs(b.addr, os);
    } catch (e) {
      if (!isLockedError(e)) toast(errorText(e), 'error');
    }
    void loadBle();
  };

  const doForget = async (b: BleBond) => {
    setForget(null);
    try {
      await api.bleForget(b.addr);
      toast(t('bleForgotten', { name: deviceLabel(b, t('bleDevice')) }));
    } catch (e) {
      if (!isLockedError(e)) toast(errorText(e), 'error');
    }
    void loadBle();
  };

  return (
    <Section
      id="bluetooth"
      title={t('groupBluetooth')}
      footer={
        s.bleEnabled
          ? `${t('footOutput')} ${t(s.bleConnect === 'always' ? 'footAlways' : 'footOnDemand')}${full ? ` ${t('footBondsFull')}` : ''} ${t('footOs')}${bonds.some((b) => b.os === 'android') ? ` ${t('androidHint')}` : ''}`
          : t('footBleOff')
      }
    >
      <SwitchRow label={t('bleKeyboard')} checked={s.bleEnabled} onChange={(v) => void save({ bleEnabled: v })} />
      {s.bleEnabled && (
        <>
          <div class="row stack-row">
            <span class="row-label">{t('typeInto')}</span>
            <Segmented<Output>
              label={t('typeInto')}
              options={[
                { value: 'auto', label: t('auto') },
                { value: 'usb', label: 'USB' },
                { value: 'ble', label: t('bluetooth') },
              ]}
              value={s.output}
              onChange={(v) => void save({ output: v })}
            />
          </div>
          <div class="row stack-row">
            <span class="row-label">{t('bleConnectLabel')}</span>
            <Segmented<Settings['bleConnect']>
              label={t('bleConnectLabel')}
              options={[
                { value: 'on_demand', label: t('bleOnDemand') },
                { value: 'always', label: t('bleAlways') },
              ]}
              value={s.bleConnect}
              onChange={(v) => void save({ bleConnect: v })}
            />
          </div>
          {bonds.map((b) => (
            <Fragment key={b.addr}>
              <div class="row bond-row">
                <Icon name="bluetooth" size={20} class="row-icon" />
                <span class="row-label" dir="auto">
                  {deviceLabel(b, t('bleDevice'))}
                </span>
                <span class="row-value">{lastUsed(b)}</span>
                <IconButton icon="trash-2" label={`${t('bleForget')} · ${deviceLabel(b, t('bleDevice'))}`} onClick={() => setForget(b)} />
              </div>
              <div class="row bond-os-row">
                <span class="row-label caption">{t('hostOs')}</span>
                <OsSelect label={`${t('hostOs')} · ${deviceLabel(b, t('bleDevice'))}`} value={b.os} onChange={(os) => void setOs(b, os)} />
              </div>
            </Fragment>
          ))}
          <button type="button" class="row nav-row pair-row" disabled={full} onClick={() => setPairing(true)}>
            <span class="row-label accent">{t('blePairNew')}</span>
            <Icon name="chevron-right" size={16} class="row-chev" />
          </button>
        </>
      )}
      {pairing && <PairSheet name={app.device?.device.name ?? 'Keyra'} before={bonds.map((b) => b.addr)} onClose={() => setPairing(false)} />}
      {forget && (
        <Alert
          title={t('bleForgetTitle', { name: deviceLabel(forget, t('bleDevice')) })}
          body={t('bleForgetBody')}
          actions={[{ label: t('bleForget'), variant: 'danger-confirm', run: () => void doForget(forget) }]}
          onCancel={() => setForget(null)}
        />
      )}
    </Section>
  );
}

type Win = { deadline: number; total: number } | 'expired' | null;

/** Button press (presence op `ble_pair`) → 120 s window → the new device shows up in GET /api/ble. */
function PairSheet({ name, before, onClose }: { name: string; before: string[]; onClose: () => void }) {
  const presence = usePresence('ble_pair');
  const [win, setWin] = useState<Win>(null);
  const [paired, setPaired] = useState<BleBond | null>(null);
  const known = useRef(before);

  useEffect(() => {
    void presence.start(api.blePair).then((ok) => !ok && onClose());
  }, []);

  // Approved on the device: the window is open now; watch for the new bond.
  useEffect(() => {
    if (presence.phase.kind !== 'done') return;
    let live = true;
    let h: ReturnType<typeof setTimeout>;
    const poll = async () => {
      try {
        const info = await api.ble();
        if (!live) return;
        const found = newBond(known.current, info.bonds);
        if (found) {
          const guess = guessOs(found.name);
          if (guess && !found.os) await api.bleSetOs(found.addr, guess).catch(() => undefined);
          setPaired(found);
          setWin(null);
          void loadBle();
          navigator.vibrate?.(12);
          return;
        }
        if (!info.pairing.active) {
          setWin('expired');
          return;
        }
        setWin((w) => (w && w !== 'expired' && Math.abs(w.deadline - (Date.now() + info.pairing.expiresIn)) < 1500 ? w : { deadline: Date.now() + info.pairing.expiresIn, total: 120000 }));
      } catch (e) {
        if (isLockedError(e)) return onClose();
      }
      h = setTimeout(poll, POLL_MS);
    };
    void poll();
    return () => {
      live = false;
      clearTimeout(h);
    };
  }, [presence.phase.kind]);

  useEffect(() => {
    if (!paired) return;
    const h = setTimeout(onClose, 2600);
    return () => clearTimeout(h);
  }, [paired]);

  const p = presence.phase;
  let body;
  if (paired) {
    body = <Ready state="typed" title="" doneTitle={t('pairedTitle')} doneBody={t('pairedBody', { name: deviceLabel(paired, t('bleDevice')) })} />;
  } else if (win === 'expired') {
    body = <ErrorCard icon="clock" tone="warn" title={t('pairExpiredTitle')} body={t('pairExpiredBody')} primary={{ label: t('tryAgain'), run: onClose }} />;
  } else if (win) {
    body = <Ready state="ready" deadline={win.deadline} total={win.total} title={t('pairPickTitle', { name })} body={t('pairPickBody')} footer={false} onCancel={onClose} />;
  } else if (p.kind === 'ready') {
    body = <Ready state="ready" deadline={p.deadline} total={p.total} title={t('pressToConfirm')} body={t('pairPressBody')} onCancel={onClose} />;
  } else if (p.kind === 'expired' || p.kind === 'cancelled' || p.kind === 'failed') {
    body = (
      <ErrorCard
        icon={p.kind === 'failed' ? 'triangle-alert' : 'clock'}
        tone={p.kind === 'failed' ? 'err' : 'warn'}
        title={p.kind === 'failed' ? t('genericError') : t('s3Expired')}
        body={t('pairPressBody')}
        primary={{ label: t('close'), run: onClose }}
      />
    );
  } else {
    body = <div class="detail-skel" />;
  }
  return (
    <Sheet title={t('blePairNew')} size="md" onClose={onClose} hideTitle>
      {body}
    </Sheet>
  );
}
