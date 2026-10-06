// The signature "Ready — press Keyra's button" card and its result/error states (DESIGN §4.11, §4.14).
import type { ComponentChildren, CSSProperties } from 'preact';
import { useEffect, useMemo, useRef, useState } from 'preact/hooks';
import { Icon, KeyGlyph, type IconName } from './Icon';
import { Button, Notice } from './ui';
import { clock, t } from '../lib/i18n';
import { useNow } from '../lib/actions';
import { defaultTarget, deviceLabel } from '../lib/ble';
import type { BleInfo, DeviceState } from '../lib/types';

const C = 553; // 2πr for r = 88

export interface ReadyProps {
  state: 'ready' | 'typing' | 'typed';
  deadline?: number;
  total?: number;
  title: string;
  body?: string;
  chip?: ComponentChildren;
  notice?: ComponentChildren;
  onCancel?: () => void;
  footer?: boolean;
  doneTitle?: string;
  doneBody?: string;
}

export function Ready({ state, deadline = 0, total = 60000, title, body, chip, notice, onCancel, footer = true, doneTitle, doneBody }: ReadyProps) {
  const now = useNow(state === 'ready', 250);
  const remaining = Math.max(0, deadline - now);
  const secs = Math.ceil(remaining / 1000);
  const warn = state === 'ready' && remaining <= 10000;
  const [announce, setAnnounce] = useState('');
  const announced = useRef(new Set<number>());

  useEffect(() => {
    if (state !== 'ready') return;
    for (const mark of [30, 10, 5]) {
      if (secs === mark && !announced.current.has(mark)) {
        announced.current.add(mark);
        setAnnounce(t('secondsLeft', { n: mark }));
      }
    }
  }, [secs, state]);

  // The drain is one CSS animation; re-keyed when the deadline is re-synced from a poll.
  const ringStyle = useMemo(() => {
    const elapsed = Math.max(0, total - (deadline - Date.now()));
    return { animationDuration: `${total}ms`, animationDelay: `-${elapsed}ms` } as CSSProperties;
  }, [deadline, total]);

  const titleText = state === 'typing' ? t('typing') : state === 'typed' ? (doneTitle ?? t('typedTitle')) : title;
  const bodyText = state === 'typed' ? (doneBody ?? t('typedBody')) : body;

  return (
    <div class={`ready ready-${state}${warn ? ' warn' : ''}`} role="group" aria-labelledby="ready-title">
      <div class="ring-wrap">
        <svg class="ring" viewBox="0 0 200 200" width="176" height="176" aria-hidden="true">
          <circle class="ring-track" cx="100" cy="100" r="88" />
          {state === 'ready' && <circle class="ring-halo" cx="100" cy="100" r="88" />}
          {state === 'typed' && <circle class="ring-ripple" cx="100" cy="100" r="88" />}
          <circle
            key={`${state}-${deadline}`}
            class="ring-prog"
            cx="100"
            cy="100"
            r="88"
            stroke-dasharray={C}
            transform="rotate(-90 100 100)"
            style={state === 'ready' ? ringStyle : undefined}
          />
          {state === 'typed' && <path class="ring-check" d="M72 102 l20 20 l38 -44" />}
        </svg>
        {state === 'ready' && <KeyGlyph size={64} class="ready-glyph" />}
        {state === 'typing' && (
          <span class="dots" aria-hidden="true">
            <i />
            <i />
            <i />
          </span>
        )}
      </div>
      <h2 class="ready-title" id="ready-title">
        {titleText}
      </h2>
      {bodyText && <p class="ready-body">{bodyText}</p>}
      {state === 'ready' && chip && <span class="chip chip-accent">{chip}</span>}
      {state === 'ready' && (
        <span class="countdown mono" aria-hidden="true" key={warn ? secs : 'n'}>
          {clock(remaining)}
        </span>
      )}
      <span class="sr-only" aria-live="polite">
        {announce}
      </span>
      {state === 'typed' && (
        <span class="sr-only" role="status">
          {titleText}
        </span>
      )}
      {state === 'ready' && notice}
      {state === 'ready' && onCancel && (
        <Button variant="ghost" onClick={onCancel}>
          {t('cancel')}
        </Button>
      )}
      {state === 'ready' && footer && <p class="ready-foot">{t('readyFooter')}</p>}
    </div>
  );
}

/** Ready's title and body; while the Bluetooth device is still connecting, say so instead of asking for the press. */
export function readyText(device: DeviceState | null, body: string): { title: string; body: string } {
  if (device?.host.connecting)
    return { title: t('connectingTitle', { name: deviceLabel(device.host.bleTarget, t('bleDevice')) }), body: t('connectingBody') };
  return { title: t('readyTitle'), body };
}

/** Under the Ready ring: where the keystrokes will go (SPEC §8.1). USB, the default, needs no note. */
export function HostNotice({ device, ble }: { device: DeviceState; ble: BleInfo | null }) {
  if (device.host.connecting) return null; // the title already says it
  const target = device.host.bleTarget;
  if (target)
    return (
      <Notice tone="accent" icon="bluetooth">
        {t('readyViaBle', { name: deviceLabel(target, t('bleDevice')) })}
      </Notice>
    );
  if (device.pending?.target === 'usb') return null;
  if (device.host.output === null)
    return (
      <Notice tone="warn" icon="usb">
        {t('readyNoHost')}
      </Notice>
    );
  if (device.host.output === 'ble') {
    const addr = defaultTarget('ble', ble);
    return (
      <Notice tone="accent" icon="bluetooth">
        {t('readyViaBle', { name: deviceLabel(ble?.bonds.find((b) => b.addr === addr), t('bleDevice')) })}
      </Notice>
    );
  }
  return null;
}

export interface ErrorCardProps {
  icon: IconName;
  tone: 'warn' | 'err';
  title: string;
  body: string;
  primary: { label: string; run: () => void };
  ghost?: { label: string; run: () => void };
}

export function ErrorCard({ icon, tone, title, body, primary, ghost }: ErrorCardProps) {
  return (
    <div class={`ready ready-error tone-${tone}`} role="alert">
      <div class="ring-wrap">
        <span class="error-disc">
          <Icon name={icon} size={56} />
        </span>
      </div>
      <h2 class="ready-title">{title}</h2>
      <p class="ready-body">{body}</p>
      <div class="ready-actions">
        <Button variant="primary" full onClick={primary.run}>
          {primary.label}
        </Button>
        {ghost && (
          <Button variant="ghost" full onClick={ghost.run}>
            {ghost.label}
          </Button>
        )}
      </div>
    </div>
  );
}
