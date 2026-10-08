// The password generator (SPEC §9.1, DESIGN §4.8): settings, live entropy, and the password
// Keyra made with its hardware RNG. Shared by the vault's Generate sheet and the Edit form.
import type { ComponentChildren } from 'preact';
import { useEffect, useMemo, useRef, useState } from 'preact/hooks';
import { Icon } from './Icon';
import { Button, ColoredSecret, IconButton, Slider, StrengthMeter, SwitchRow } from './ui';
import { ErrorCard, HostNotice, Ready, readyText } from './Ready';
import { ApiError, api } from '../lib/api';
import { copyText } from '../lib/clipboard';
import { errorText, isLockedError } from '../lib/errors';
import {
  MAX_LENGTH,
  MIN_LENGTH,
  SYMBOLS,
  cleanSymbolSet,
  entropyBits,
  loadGenSettings,
  maxMinimum,
  normalize,
  saveGenSettings,
  type GenSettings,
} from '../lib/generator';
import { t } from '../lib/i18n';
import { layoutName } from '../lib/keyboard';
import { toast, useApp } from '../lib/store';
import type { Phase } from '../lib/actions';
import type { KeyboardLayout } from '../lib/types';

export interface GeneratorState {
  settings: GenSettings;
  set: (patch: Partial<GenSettings>) => void;
  password: string;
  /** The device's exact entropy for `password` (0 until one arrives). */
  bits: number;
  busy: boolean;
  regenerate: () => void;
  /** The layouts set for the USB and Bluetooth outputs (SPEC §10.2); null = this firmware has none. */
  outputs: KeyboardLayout[] | null;
}

/** Settings remembered per browser; a new password from the device on every change (debounced). */
export function useGenerator(): GeneratorState {
  const [settings, setSettings] = useState<GenSettings>(loadGenSettings);
  const [password, setPassword] = useState('');
  const [bits, setBits] = useState(0);
  const [busy, setBusy] = useState(false);
  // undefined while loading: a layout-safe password waits for the computers' layouts.
  const [outputs, setOutputs] = useState<KeyboardLayout[] | null | undefined>(undefined);
  const seq = useRef(0);

  useEffect(() => {
    api
      .keyboard()
      .then((k) => {
        const pick = (id: string) => k.layouts.find((l) => l.id === id);
        setOutputs([pick(k.usb), pick(k.ble)].filter((l, i, all): l is KeyboardLayout => !!l && all.indexOf(l) === i));
      })
      .catch((e) => {
        // Firmware before SPEC §10 has no GET /api/keyboard: the layout-safe switch stays hidden,
        // so a remembered "on" must not stay in force where it cannot be turned off.
        setOutputs(null);
        setSettings((s) => (s.layoutSafe ? { ...s, layoutSafe: false } : s));
        if (!(e instanceof ApiError && e.status === 404) && !isLockedError(e)) toast(errorText(e), 'error');
      });
  }, []);

  const regenerate = (s = settings) => {
    const my = ++seq.current;
    setBusy(true);
    api
      .generate(s, s.layoutSafe ? (outputs ?? []).map((l) => l.id) : [])
      .then((r) => {
        if (my !== seq.current) return;
        setPassword(r.password);
        setBits(r.entropyBits);
      })
      .catch((e) => {
        if (my !== seq.current || isLockedError(e)) return;
        // The layouts share too few characters for these settings (SPEC §10.2).
        toast(s.layoutSafe && e instanceof ApiError && e.status === 400 ? t('layoutSafeTooFew') : errorText(e), 'error');
      })
      .finally(() => my === seq.current && setBusy(false));
  };

  const waiting = settings.layoutSafe && outputs === undefined;
  useEffect(() => {
    saveGenSettings(settings);
    if (waiting) return;
    const h = setTimeout(() => regenerate(settings), 150);
    return () => clearTimeout(h);
  }, [settings, waiting]);
  // A late answer must never overwrite a newer one, nor land after unmount.
  useEffect(() => () => void ++seq.current, []);

  return {
    settings,
    set: (patch) => setSettings((s) => normalize({ ...s, ...patch })),
    password,
    bits,
    busy,
    regenerate: () => regenerate(),
    outputs: outputs ?? null,
  };
}

/** Big monospaced password (digits and symbols tinted), tap = copy. */
export function GenPreview({ password, busy }: { password: string; busy: boolean }) {
  return (
    <button
      type="button"
      class={`gen-preview${busy ? ' busy' : ''}`}
      onClick={() => password && copyText(password) && toast(t('copied'), 'ok')}
      aria-label={`${t('copy')} ${password}`}
    >
      <ColoredSecret value={password || ' '} />
    </button>
  );
}

export function Stepper({ label, value, min, max, onChange }: { label: string; value: number; min: number; max: number; onChange: (v: number) => void }) {
  return (
    <div class="row stepper-row">
      <span class="row-label">{label}</span>
      <span class="stepper">
        <IconButton icon="minus" label={`${t('fewer')} · ${label}`} onClick={() => value > min && onChange(value - 1)} size={20} />
        <span class="stepper-value mono" aria-live="polite">
          {value}
        </span>
        <IconButton icon="plus" label={`${t('more')} · ${label}`} onClick={() => value < max && onChange(value + 1)} size={20} />
      </span>
    </div>
  );
}

/** Settings card: length (slider + number), classes, minimums, look-alikes. */
export function GenOptions({ gen }: { gen: GeneratorState }) {
  const s = gen.settings;
  const [lengthText, setLengthText] = useState(String(s.length));
  useEffect(() => setLengthText(String(s.length)), [s.length]);
  const maxDigits = useMemo(() => maxMinimum(s, 'minDigits'), [s]);
  const maxSymbols = useMemo(() => maxMinimum(s, 'minSymbols'), [s]);
  const [symbolText, setSymbolText] = useState(s.symbolSet);
  useEffect(() => setSymbolText(s.symbolSet), [s.symbolSet]);
  const commitSymbols = () => {
    const next = normalize({ ...s, symbolSet: cleanSymbolSet(symbolText) }).symbolSet;
    gen.set({ symbolSet: next });
    setSymbolText(next); // when it equals the set in use, nothing else re-renders the box
  };
  const on = [s.lower, s.upper, s.digits, s.symbols].filter(Boolean).length;
  const toggle = (k: 'lower' | 'upper' | 'digits' | 'symbols', label: string) => (
    // The last enabled class cannot be turned off (the device needs one).
    <SwitchRow label={label} checked={s[k]} disabled={s[k] && on === 1} onChange={(v) => gen.set({ [k]: v })} />
  );
  const commitLength = () => {
    const n = Number(lengthText);
    if (Number.isFinite(n)) gen.set({ length: n });
    // gen.set clamps to 8-128; when the clamped length equals the current one
    // nothing re-renders the box, so show the length actually in use.
    setLengthText(String(Number.isFinite(n) ? Math.min(MAX_LENGTH, Math.max(MIN_LENGTH, Math.round(n))) : s.length));
  };
  return (
    <div class="card gen-options">
      <div class="row slider-row">
        <span class="row-label">{t('length')}</span>
        <Slider value={s.length} min={MIN_LENGTH} max={MAX_LENGTH} label={t('length')} onInput={(n) => gen.set({ length: n })} />
        <input
          class="input len-input mono"
          type="number"
          inputMode="numeric"
          min={MIN_LENGTH}
          max={MAX_LENGTH}
          aria-label={t('length')}
          value={lengthText}
          onInput={(e) => setLengthText(e.currentTarget.value)}
          onBlur={commitLength}
          onKeyDown={(e) => e.key === 'Enter' && (e.preventDefault(), commitLength())}
        />
      </div>
      {toggle('lower', t('lower'))}
      {toggle('upper', t('upper'))}
      {toggle('digits', t('digits'))}
      {s.digits && <Stepper label={t('minDigits')} value={s.minDigits} min={1} max={maxDigits} onChange={(v) => gen.set({ minDigits: v })} />}
      {toggle('symbols', t('symbols'))}
      {s.symbols && <Stepper label={t('minSymbols')} value={s.minSymbols} min={1} max={maxSymbols} onChange={(v) => gen.set({ minSymbols: v })} />}
      {s.symbols && (
        <label class="row symbol-row">
          <span class="row-label">{t('symbolSet')}</span>
          <input
            class="input symbol-input mono"
            dir="ltr"
            autocapitalize="off"
            autocomplete="off"
            spellcheck={false}
            maxLength={64}
            placeholder={SYMBOLS}
            value={symbolText}
            onInput={(e) => setSymbolText(e.currentTarget.value)}
            onBlur={commitSymbols}
            onKeyDown={(e) => e.key === 'Enter' && (e.preventDefault(), commitSymbols())}
          />
        </label>
      )}
      <SwitchRow label={t('lookAlikes')} checked={s.avoidAmbiguous} onChange={(v) => gen.set({ avoidAmbiguous: v })} />
      {gen.outputs && (
        <>
          <SwitchRow label={t('layoutSafe')} checked={s.layoutSafe} onChange={(v) => gen.set({ layoutSafe: v })} />
          <p class="caption row-note">
            {t('layoutSafeNote')} <bdi>{gen.outputs.map(layoutName).join(' · ')}</bdi>
          </p>
        </>
      )}
    </div>
  );
}

/**
 * Exact strength of what the settings produce (every password equally likely): known at once from
 * the settings, except layout-safe ones, where only the device knows which characters are left.
 */
export function GenStrength({ gen }: { gen: GeneratorState }) {
  const local = useMemo(() => entropyBits(gen.settings), [gen.settings]);
  return <StrengthMeter bits={gen.settings.layoutSafe ? gen.bits : local} />;
}

/**
 * Ready / typed / error for free text (SPEC §9.2), or null when idle so the caller shows its form.
 * `retry` re-sends the same request.
 */
export function FreeTextStatus({ phase, chip, body, retry, close, cancel }: { phase: Phase; chip: ComponentChildren; body: string; retry: () => void; close: () => void; cancel: () => void }) {
  const app = useApp();
  if (phase.kind === 'idle') return null;
  if (phase.kind === 'error') {
    const c = phase.code;
    const warn = c === 'no_usb' || c === 'no_host' || c === 'expired' || c === 'host_changed';
    return (
      <ErrorCard
        icon={c === 'no_usb' ? 'usb' : c === 'no_host' ? 'bluetooth' : c === 'expired' ? 'clock' : 'triangle-alert'}
        tone={warn ? 'warn' : 'err'}
        title={c === 'no_usb' ? t('errNoUsbTitle') : c === 'no_host' ? t('errNoHostTitle') : c === 'expired' ? t('errExpiredTitle') : c === 'host_changed' ? t('errHostChangedTitle') : t('errFailedTitle')}
        body={c === 'no_usb' ? t('errNoUsbBody') : c === 'no_host' ? t('errNoHostBody') : c === 'expired' ? t('errExpiredBody') : c === 'host_changed' ? t('errHostChangedBody') : t('errFailedBody')}
        primary={{ label: t('tryAgain'), run: retry }}
        ghost={{ label: t('close'), run: close }}
      />
    );
  }
  return (
    <Ready
      state={phase.kind}
      deadline={phase.kind === 'ready' ? phase.deadline : 0}
      total={phase.kind === 'ready' ? phase.total : 60000}
      {...readyText(app.device, body)}
      chip={chip}
      notice={app.device ? <HostNotice device={app.device} /> : undefined}
      onCancel={cancel}
    />
  );
}

/** Inline generator for Add/Edit: "New one" and "Use this password". */
export function InlineGenerator({ onUse }: { onUse: (pw: string) => void }) {
  const gen = useGenerator();
  return (
    <div class="gen gen-inline">
      <GenPreview password={gen.password} busy={gen.busy} />
      <GenStrength gen={gen} />
      <GenOptions gen={gen} />
      <div class="sheet-foot">
        <Button variant="secondary" icon="refresh-cw" onClick={gen.regenerate}>
          {t('newOne')}
        </Button>
        <Button disabled={!gen.password} onClick={() => onUse(gen.password)}>
          {t('usePassword')}
        </Button>
      </div>
      <p class="caption gen-from">
        <Icon name="shield-check" size={16} />
        {t('genFrom')}
      </p>
    </div>
  );
}
