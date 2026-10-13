// Shared by the Type, Security and Device tabs: the settings object (every change saves at once,
// DESIGN §5.9), the tab page shell and the settings row with its icon.
import type { ComponentChildren } from 'preact';
import { useEffect, useState } from 'preact/hooks';
import { Icon, type IconName } from '../components/Icon';
import { api } from '../lib/api';
import { errorText, isLockedError } from '../lib/errors';
import { t } from '../lib/i18n';
import { pollNow, toast } from '../lib/store';
import type { Settings } from '../lib/types';

export interface SettingsCtl {
  s: Settings | null;
  setS: (s: Settings) => void;
  load: () => Promise<void>;
  /** Optimistic; on failure the old value comes back with an error toast. */
  save: (patch: Partial<Settings>) => Promise<void>;
}

export function useSettings(): SettingsCtl {
  const [s, setS] = useState<Settings | null>(null);
  const load = () =>
    api
      .settings()
      .then(setS)
      .catch((e) => {
        if (!isLockedError(e)) toast(errorText(e), 'error');
      });
  useEffect(() => void load(), []);
  const save = async (patch: Partial<Settings>) => {
    if (!s) return;
    const prev = s;
    setS({ ...s, ...patch });
    try {
      setS(await (api.putSettings(patch) as Promise<Settings>));
      if ('osUsb' in patch) void pollNow(); // state.host.usbOs feeds the account sheet
    } catch (e) {
      setS(prev);
      if (!isLockedError(e)) toast(t('saveError'), 'error');
    }
  };
  return { s, setS, load, save };
}

/** A tab's page: large title, then its groups. `loaded` marks when the settings have arrived. */
export function TabPage({ title, name, loaded, children }: { title: string; name: string; loaded: boolean; children: ComponentChildren }) {
  return (
    <div class={`tab-page settings ${name}`} data-loaded={loaded ? '' : undefined}>
      <header class="page-head">
        <h1 class="t1" tabIndex={-1}>
          {title}
        </h1>
      </header>
      {children}
    </div>
  );
}

export function NavRow({
  icon,
  label,
  value,
  onClick,
  badge,
  danger,
  disabled,
}: {
  icon: IconName;
  label: string;
  value?: ComponentChildren;
  onClick: () => void;
  badge?: string;
  danger?: boolean;
  disabled?: boolean;
}) {
  return (
    <button type="button" class="row nav-row has-icon" disabled={disabled} onClick={onClick}>
      <span class={`row-glyph${danger ? ' danger' : ''}`}>
        <Icon name={icon} size={18} />
      </span>
      <span class={`row-label${danger ? ' danger-text' : ''}`}>{label}</span>
      {badge && <span class="row-badge">{badge}</span>}
      {value !== undefined && <span class="row-value">{value}</span>}
      {!danger && <Icon name="chevron-right" size={16} class="row-chev" />}
    </button>
  );
}

/** A non-navigating row with an icon (a control sits at its end). */
export function IconRow({ icon, label, children, stack }: { icon: IconName; label: string; children: ComponentChildren; stack?: boolean }) {
  return (
    <div class={`row has-icon${stack ? ' stack-row icon-stack' : ''}`}>
      {stack ? (
        <span class="row-head">
          <span class="row-glyph">
            <Icon name={icon} size={18} />
          </span>
          <span class="row-label">{label}</span>
        </span>
      ) : (
        <>
          <span class="row-glyph">
            <Icon name={icon} size={18} />
          </span>
          <span class="row-label">{label}</span>
        </>
      )}
      {children}
    </div>
  );
}
