// Where Keyra types and how it gets text past the host's input language (SPEC §8.1, §10.5):
// the "Type into" picker, the system picker for a computer, and the "its language now" switch.
import { useState } from 'preact/hooks';
import { Segmented } from './ui';
import { defaultTarget, deviceLabel, storeTarget, storedTarget, validTarget } from '../lib/ble';
import { OS_LIST, otherLang, setOtherLang, switchesLang } from '../lib/hostos';
import { go } from '../lib/router';
import { t, type Key } from '../lib/i18n';
import { useApp } from '../lib/store';
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
      <button type="button" class="link-line" onClick={() => go('/settings')}>
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
