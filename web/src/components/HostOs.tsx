// Where Keyra types and how it gets text past the host's input language (SPEC §8.1, §10.5):
// the "Type into" picker, the system picker for a computer, and the "its language now" switch.
import { useRef, useState } from 'preact/hooks';
import { Icon } from './Icon';
import { Segmented } from './ui';
import { Sheet, type SheetCtl } from './Sheet';
import { defaultTarget, deviceLabel, sortBonds, storeTarget, storedTarget, targetName, validTarget } from '../lib/ble';
import { OS_LIST, otherLang, setOtherLang, switchesLang } from '../lib/hostos';
import { go } from '../lib/router';
import { t, type Key } from '../lib/i18n';
import { useApp, type AppState } from '../lib/store';
import type { HostOs } from '../lib/types';

const OS_KEY: Record<Exclude<HostOs, ''>, Key> = { mac: 'osMac', ios: 'osIos', windows: 'osWindows', android: 'osAndroid', linux: 'osLinux' };

export const osLabel = (os: HostOs): string => (os ? t(OS_KEY[os]) : t('osUnset'));

export function OsSelect({ value, onChange, label }: { value: HostOs; onChange: (os: HostOs) => void; label: string }) {
  return (
    <select class="os-select" aria-label={label} value={value} onChange={(e) => onChange((e.currentTarget as HTMLSelectElement).value as HostOs)}>
      <option value="">{t('osUnset')}</option>
      {OS_LIST.map((os) => (
        <option key={os} value={os}>
          {osLabel(os)}
        </option>
      ))}
    </select>
  );
}

/**
 * "Type into": USB or one paired Bluetooth device, remembered per browser (shown once a device
 * is paired). `onPick` lets the screen re-read storedTarget() for what depends on it.
 */
export function TargetPicker({ onPick }: { onPick: () => void }) {
  const app = useApp();
  if (!app.ble?.enabled || app.ble.bonds.length === 0) return null;
  return (
    <div class="target-picker">
      <span class="caption">{t('typeInto')}</span>
      <Segmented<string>
        label={t('typeInto')}
        options={[{ value: 'usb', label: 'USB' }, ...app.ble.bonds.map((b) => ({ value: b.addr, label: deviceLabel(b, t('bleDevice')) }))]}
        value={validTarget(storedTarget(), app.ble) ?? defaultTarget(app.device?.host.output ?? null, app.ble)}
        onChange={(v) => {
          storeTarget(v);
          onPick();
        }}
      />
    </div>
  );
}

/** Under "Type into": Apple hosts get the language switch; an unset system gets a pointer to Settings. */
export function HostLangRow({ target, os }: { target: string | null; os: HostOs }) {
  const [, rerender] = useState(0);
  if (!target) return null;
  if (os === '')
    return (
      <button type="button" class="link-line" onClick={() => go('/device')}>
        {t('osUnknownHint')}
      </button>
    );
  if (os === 'android') return <p class="caption host-lang-help">{t('androidHint')}</p>;
  if (!switchesLang(os)) return null;
  // Read per render: the remembered answer belongs to whichever target is picked now.
  const value = otherLang(target) ? 'other' : 'latin';
  return (
    <div class="host-lang">
      <span class="caption">{t('hostLangLabel')}</span>
      <Segmented<'latin' | 'other'>
        label={t('hostLangLabel')}
        options={[
          { value: 'latin', label: t('hostLangLatin') },
          { value: 'other', label: t('hostLangOther') },
        ]}
        value={value}
        onChange={(v) => {
          setOtherLang(target, v === 'other');
          rerender((n) => n + 1);
        }}
      />
      {value === 'other' && <p class="caption host-lang-help">{t('hostLangHelp')}</p>}
    </div>
  );
}

/** Where the next action types: this browser's pick, else what the device would choose by itself. */
export const currentTarget = (app: AppState): string | null =>
  validTarget(storedTarget(), app.ble) ?? defaultTarget(app.device?.host.output ?? null, app.ble);

/** USB and each paired Bluetooth device as a radio list; picking one is remembered per browser. */
export function TargetList({ onPick }: { onPick: (v: string) => void }) {
  const app = useApp();
  const d = app.device;
  const ble = app.ble;
  const connected = ble?.connected?.addr ?? null;
  const current = currentTarget(app);
  const rows = [
    { value: 'usb', icon: 'usb' as const, label: 'USB', sub: d?.host.usb ? t('usbOn') : t('usbOff') },
    ...(ble?.enabled ? sortBonds(ble.bonds, connected) : []).map((b) => ({
      value: b.addr,
      icon: 'bluetooth' as const,
      label: deviceLabel(b, t('bleDevice')),
      sub: b.addr === connected ? t('bleConnected') : t('blePaired'),
    })),
  ];
  return (
    <div class="card" role="radiogroup" aria-label={t('typeInto')}>
      {rows.map((r) => (
        <button
          key={r.value}
          type="button"
          role="radio"
          aria-checked={r.value === current}
          class="row nav-row has-icon target-row"
          onClick={() => {
            storeTarget(r.value);
            onPick(r.value);
          }}
        >
          <span class="row-glyph">
            <Icon name={r.icon} size={18} />
          </span>
          <span class="row-label target-label">
            <bdi>{r.label}</bdi>
            <span class="caption">{r.sub}</span>
          </span>
          {r.value === current && <Icon name="check" size={20} class="strong-ink" />}
        </button>
      ))}
    </div>
  );
}

/** Accounts screen: "Typing to: USB / <device>", tap to choose (SPEC §8.1). */
export function TargetChip() {
  const app = useApp();
  const [open, setOpen] = useState(false);
  const [, rerender] = useState(0);
  const d = app.device;
  const target = currentTarget(app);
  const name = targetName(target, app.ble, t('bleDevice'));
  const live = target === 'usb' ? !!d?.host.usb : !!target && app.ble?.connected?.addr === target;
  return (
    <>
      <button type="button" class="target-chip" aria-haspopup="dialog" onClick={() => setOpen(true)}>
        <span class={`target-dot${live ? ' on' : ''}`} aria-hidden="true" />
        <span class="target-chip-text">
          <span class="target-chip-k">{t('typingTo')}</span>
          <bdi class="target-chip-v">{name || t('targetNone')}</bdi>
        </span>
        <Icon name="chevron-down" size={16} class="row-chev" />
      </button>
      {open && <TargetSheet onClose={() => setOpen(false)} onPick={() => rerender((n) => n + 1)} />}
    </>
  );
}

function TargetSheet({ onClose, onPick }: { onClose: () => void; onPick: () => void }) {
  const app = useApp();
  const ctl = useRef<SheetCtl | null>(null);
  const next = useRef<string | null>(null);
  return (
    <Sheet
      title={t('typeInto')}
      size="sm"
      ctl={ctl}
      onClose={() => {
        onClose();
        if (next.current) go(next.current);
      }}
    >
      <div class="target-sheet">
        <p class="callout">{t('targetBody')}</p>
        <TargetList
          onPick={() => {
            onPick();
            ctl.current?.close();
          }}
        />
        <div class="card">
          <button
            type="button"
            class="row nav-row has-icon"
            onClick={() => {
              next.current = app.ble?.enabled ? '/device/pair' : '/device'; // pairing needs Bluetooth on first
              ctl.current?.close();
            }}
          >
            <span class="row-glyph">
              <Icon name="plus" size={18} />
            </span>
            <span class="row-label">{t('blePairNew')}</span>
            <Icon name="chevron-right" size={16} class="row-chev" />
          </button>
        </div>
      </div>
    </Sheet>
  );
}
