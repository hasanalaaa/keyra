// Primitives from DESIGN §4: buttons, fields, switch, segmented control, notices, meters, monogram.
import type { ComponentChildren, CSSProperties, InputHTMLAttributes, Ref } from 'preact';
import { useEffect, useId, useRef, useState } from 'preact/hooks';
import { Icon, KeyGlyph, type IconName } from './Icon';
import { t, type Key } from '../lib/i18n';
import { copyText } from '../lib/clipboard';
import { toast } from '../lib/store';
import { strength } from '../lib/strength';
import { monogramColor, monogramLetter } from '../lib/monogram';

type Variant = 'primary' | 'tinted' | 'secondary' | 'ghost' | 'danger' | 'danger-confirm';

export interface ButtonProps {
  variant?: Variant;
  size?: 'lg' | 'md' | 'sm';
  icon?: IconName;
  loading?: boolean;
  disabled?: boolean;
  full?: boolean;
  type?: 'button' | 'submit';
  class?: string;
  onClick?: (e: MouseEvent) => void;
  children?: ComponentChildren;
  label?: string;
}

export function Button(p: ButtonProps) {
  const cls = ['btn', `btn-${p.variant ?? 'primary'}`, `btn-${p.size ?? 'md'}`, p.full ? 'btn-full' : '', p.class ?? '']
    .filter(Boolean)
    .join(' ');
  return (
    <button
      type={p.type ?? 'button'}
      class={cls}
      disabled={p.disabled || p.loading}
      aria-busy={p.loading || undefined}
      aria-label={p.label}
      onClick={p.onClick}
    >
      {p.loading ? (
        <Spinner />
      ) : (
        <>
          {p.icon && <Icon name={p.icon} size={p.size === 'sm' ? 20 : 22} />}
          {p.children}
        </>
      )}
    </button>
  );
}

export function IconButton(p: {
  icon: IconName;
  label: string;
  onClick: (e: MouseEvent) => void;
  pressed?: boolean;
  class?: string;
  size?: number;
}) {
  return (
    <button
      type="button"
      class={`icon-btn ${p.class ?? ''}`}
      aria-label={p.label}
      title={p.label}
      aria-pressed={p.pressed}
      onClick={p.onClick}
    >
      <Icon name={p.icon} size={p.size ?? 24} class={p.icon === 'star' && p.pressed ? 'i-fill' : undefined} />
    </button>
  );
}

export function Spinner({ size = 20 }: { size?: number }) {
  return (
    <svg class="spinner" width={size} height={size} viewBox="0 0 20 20" aria-hidden="true">
      <circle cx="10" cy="10" r="8" />
    </svg>
  );
}

let clipboardNoteShown = false;

/** Copies inside the click (needed for the execCommand fallback), then flips to a check for 1.2 s. */
export function CopyButton({ value, label, class: cls }: { value: () => string; label?: string; class?: string }) {
  const [done, setDone] = useState(false);
  const timer = useRef<ReturnType<typeof setTimeout> | undefined>(undefined);
  useEffect(() => () => clearTimeout(timer.current), []);
  const onClick = (e: MouseEvent) => {
    e.stopPropagation();
    if (!copyText(value())) {
      toast(t('genericError'), 'error');
      return;
    }
    toast(clipboardNoteShown ? t('copied') : `${t('copied')} · ${t('clipboardNote')}`, 'ok');
    clipboardNoteShown = true;
    setDone(true);
    clearTimeout(timer.current);
    timer.current = setTimeout(() => setDone(false), 1200);
  };
  return (
    <button type="button" class={`icon-btn copy-btn ${cls ?? ''}`} aria-label={label ?? t('copy')} title={label ?? t('copy')} onClick={onClick}>
      <Icon name={done ? 'check' : 'copy'} size={20} />
    </button>
  );
}

// ---------- fields ----------

interface FieldShell {
  label: string;
  helper?: ComponentChildren;
  error?: string | null;
  id: string;
  children: ComponentChildren;
  end?: ComponentChildren;
}

function Shell({ label, helper, error, id, children, end }: FieldShell) {
  return (
    <div class={`field${error ? ' has-error' : ''}`}>
      <label class="field-label" for={id}>
        {label}
      </label>
      <div class="field-box">
        {children}
        {end && <div class="field-end">{end}</div>}
      </div>
      {error ? (
        <p class="field-help field-error" id={`${id}-h`} role="alert">
          <Icon name="triangle-alert" size={16} />
          {error}
        </p>
      ) : (
        helper && (
          <p class="field-help" id={`${id}-h`}>
            {helper}
          </p>
        )
      )}
    </div>
  );
}

type InputAttrs = Omit<InputHTMLAttributes<HTMLInputElement>, 'label' | 'value' | 'onInput' | 'type' | 'role'>;

export interface TextFieldProps extends InputAttrs {
  label: string;
  value: string;
  onValue: (v: string) => void;
  helper?: ComponentChildren;
  error?: string | null;
  ltr?: boolean;
  end?: ComponentChildren;
}

export function TextField({ label, value, onValue, helper, error, ltr, end, class: cls, ...rest }: TextFieldProps) {
  const id = useId();
  return (
    <Shell label={label} helper={helper} error={error} id={id} end={end}>
      <input
        id={id}
        type="text"
        class={`input ${ltr ? 'ltr' : ''} ${cls ?? ''}`}
        dir={ltr ? 'ltr' : 'auto'}
        value={value}
        onInput={(e) => onValue(e.currentTarget.value)}
        aria-invalid={error ? true : undefined}
        aria-describedby={error || helper ? `${id}-h` : undefined}
        autocomplete="off"
        {...rest}
      />
    </Shell>
  );
}

export interface SecretFieldProps extends InputAttrs {
  label: string;
  value: string;
  onValue: (v: string) => void;
  helper?: ComponentChildren;
  error?: string | null;
  reveal?: boolean;
  extraEnd?: ComponentChildren;
  inputRef?: Ref<HTMLInputElement>;
}

/** Password/passphrase input with show/hide; auto-hides after 30 s (DESIGN §4.6). */
export function SecretField({ label, value, onValue, helper, error, reveal = false, extraEnd, inputRef, class: cls, ...rest }: SecretFieldProps) {
  const id = useId();
  const [shown, setShown] = useState(reveal);
  useEffect(() => {
    if (!shown || reveal) return;
    const h = setTimeout(() => setShown(false), 30000);
    return () => clearTimeout(h);
  }, [shown, reveal]);
  return (
    <Shell
      label={label}
      helper={helper}
      error={error}
      id={id}
      end={
        <>
          {extraEnd}
          <IconButton
            icon={shown ? 'eye-off' : 'eye'}
            label={shown ? t('hidePassword') : t('showPassword')}
            pressed={shown}
            onClick={() => setShown(!shown)}
          />
        </>
      }
    >
      <input
        id={id}
        ref={inputRef}
        class={`input ltr ${shown ? 'mono' : ''} ${cls ?? ''}`}
        dir="ltr"
        // Preact's input typings discriminate on a literal `type`; the toggle is either value.
        type={(shown ? 'text' : 'password') as 'password'}
        value={value}
        onInput={(e) => onValue(e.currentTarget.value)}
        spellcheck={false}
        autocapitalize="off"
        autocorrect="off"
        aria-invalid={error ? true : undefined}
        aria-describedby={error || helper ? `${id}-h` : undefined}
        {...rest}
      />
    </Shell>
  );
}

/** A revealed secret with digits and symbols coloured (DESIGN §4.6). */
export function ColoredSecret({ value, class: cls }: { value: string; class?: string }) {
  return (
    <span class={`secret mono ${cls ?? ''}`} dir="ltr">
      {Array.from(value).map((ch, i) => (
        <span key={i} class={/[0-9]/.test(ch) ? 'pw-d' : /[A-Za-z]/.test(ch) ? undefined : 'pw-s'}>
          {ch}
        </span>
      ))}
    </span>
  );
}

// ---------- switch / segmented / slider ----------

export function SwitchRow({ label, checked, onChange, disabled }: { label: string; checked: boolean; onChange: (v: boolean) => void; disabled?: boolean }) {
  return (
    <button type="button" role="switch" aria-checked={checked} class="row switch-row" disabled={disabled} onClick={() => onChange(!checked)}>
      <span class="row-label">{label}</span>
      <span class="switch" aria-hidden="true">
        <span class="knob" />
      </span>
    </button>
  );
}

export function Segmented<T extends string | number>({
  options,
  value,
  onChange,
  label,
}: {
  options: { value: T; label: string }[];
  value: T | null;
  onChange: (v: T) => void;
  label: string;
}) {
  const idx = options.findIndex((o) => o.value === value);
  const refs = useRef<(HTMLButtonElement | null)[]>([]);
  const move = (i: number) => {
    const n = (i + options.length) % options.length;
    onChange(options[n].value);
    refs.current[n]?.focus();
  };
  const onKey = (e: KeyboardEvent) => {
    const rtl = document.documentElement.dir === 'rtl';
    const k = e.key;
    if (k === 'ArrowRight' || k === 'ArrowDown') move(idx + (k === 'ArrowRight' && rtl ? -1 : 1));
    else if (k === 'ArrowLeft' || k === 'ArrowUp') move(idx + (k === 'ArrowLeft' && rtl ? 1 : -1));
    else if (k === 'Home') move(0);
    else if (k === 'End') move(options.length - 1);
    else return;
    e.preventDefault();
  };
  return (
    <div
      class="seg"
      role="radiogroup"
      aria-label={label}
      onKeyDown={onKey}
      style={{ '--n': options.length, '--i': Math.max(0, idx) } as CSSProperties}
    >
      {idx >= 0 && <span class="seg-thumb" aria-hidden="true" />}
      {options.map((o, i) => (
        <button
          key={String(o.value)}
          ref={(el) => {
            refs.current[i] = el;
          }}
          type="button"
          role="radio"
          aria-checked={i === idx}
          tabIndex={i === idx || (idx < 0 && i === 0) ? 0 : -1}
          class="seg-item"
          onClick={() => onChange(o.value)}
        >
          {o.label}
        </button>
      ))}
    </div>
  );
}

export function Slider({ value, min, max, step = 1, onInput, onCommit, label }: { value: number; min: number; max: number; step?: number; onInput: (v: number) => void; onCommit?: (v: number) => void; label: string }) {
  const pct = ((value - min) / (max - min)) * 100;
  return (
    <input
      type="range"
      class="slider"
      min={min}
      max={max}
      step={step}
      value={value}
      aria-label={label}
      style={{ '--p': `${pct}%` } as CSSProperties}
      onInput={(e) => onInput(Number(e.currentTarget.value))}
      onChange={(e) => onCommit?.(Number(e.currentTarget.value))}
    />
  );
}

// ---------- notices, chips, meters ----------

export function Notice({ tone, icon, children, action }: { tone: 'warn' | 'err' | 'accent'; icon?: IconName; children: ComponentChildren; action?: ComponentChildren }) {
  return (
    <div class={`notice notice-${tone}`}>
      <Icon name={icon ?? (tone === 'accent' ? 'shield-check' : 'triangle-alert')} size={20} />
      <div class="notice-text">{children}</div>
      {action}
    </div>
  );
}

const LEVEL_KEYS: Key[] = ['weak', 'fair', 'good', 'strong'];

export function StrengthMeter({ value }: { value: string }) {
  const s = strength(value);
  return (
    <div class={`meter meter-${s}`}>
      <div class="meter-bars" aria-hidden="true">
        {[1, 2, 3, 4].map((i) => (
          <span key={i} class={i <= s ? 'on' : undefined} />
        ))}
      </div>
      <span class="meter-label" aria-live="polite">
        {s > 0 ? t(LEVEL_KEYS[s - 1]) : ''}
      </span>
    </div>
  );
}

export function Monogram({ title, size = 40 }: { title: string; size?: number }) {
  const letter = monogramLetter(title);
  return (
    <span
      class="mono-gram"
      aria-hidden="true"
      style={{ '--mg': monogramColor(title), inlineSize: `${size}px`, blockSize: `${size}px`, fontSize: `${Math.round(size * 0.45)}px` } as CSSProperties}
    >
      {letter || <KeyGlyph size={Math.round(size * 0.6)} />}
    </span>
  );
}

/** 28 px countdown ring for TOTP (DESIGN §4.12). */
export function MiniRing({ remaining, period }: { remaining: number; period: number }) {
  const c = 2 * Math.PI * 11;
  const warn = remaining <= 5;
  return (
    <svg class={`mini-ring${warn ? ' warn' : ''}`} width="28" height="28" viewBox="0 0 28 28" aria-hidden="true">
      <circle cx="14" cy="14" r="11" class="track" />
      <circle
        cx="14"
        cy="14"
        r="11"
        class="prog"
        stroke-dasharray={c}
        stroke-dashoffset={c * (1 - remaining / period)}
        transform="rotate(-90 14 14)"
      />
    </svg>
  );
}

export function Section({ title, children, footer, id }: { title?: ComponentChildren; children: ComponentChildren; footer?: ComponentChildren; id?: string }) {
  return (
    <section class="group" aria-labelledby={title && id ? id : undefined}>
      {title && (
        <h2 class="section-head" id={id}>
          {title}
        </h2>
      )}
      <div class="card">{children}</div>
      {footer && <p class="group-foot">{footer}</p>}
    </section>
  );
}

/** Renders `**bold**` spans from the copy deck. */
export function Rich({ text }: { text: string }) {
  return (
    <>
      {text.split(/\*\*(.+?)\*\*/).map((part, i) => (i % 2 ? <strong key={i}>{part}</strong> : part))}
    </>
  );
}
