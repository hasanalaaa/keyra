// The signature "Ready — press Keyra's button" card and its result/error states (DESIGN §4.11, §4.14).
// The press indicator is the Contour (BRAND §5): its line in signal blue with the gap breathing at the LED's
// rhythm while Keyra waits; the gap closes and the line settles to ink on success; it retracts in err on
// expiry or refusal. CSS only (stroke-dasharray/offset on pathLength 100).
import type { ComponentChildren, CSSProperties } from 'preact';
import { useEffect, useMemo, useRef, useState } from 'preact/hooks';
import { CONTOUR, CONTOUR_GAP, Icon, type IconName } from './Icon';
import { Button, Notice } from './ui';
import { clock, t } from '../lib/i18n';
import { useNow } from '../lib/actions';
import { deviceLabel, targetName } from '../lib/ble';
import { useApp } from '../lib/store';
import type { DeviceState } from '../lib/types';

const C = 578; // 2πr for r = 92

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
  /** The monospace line under "Typed"; by default "Typed · <where> · <time>" for typing actions. */
  receipt?: string;
}

/** The Contour as the press indicator; `mode` picks the state's motion. */
function PressMark({ mode }: { mode: 'wait' | 'typing' | 'done' | 'off' }) {
  return (
    <svg class={`press press-${mode}`} viewBox="14 14 72 72" width="120" height="120" aria-hidden="true" focusable="false">
      {mode === 'wait' && <path class="press-glow" d={CONTOUR_GAP} pathLength={100} />}
      {(mode === 'typing' || mode === 'done') && <path class="press-gap" d={CONTOUR_GAP} pathLength={100} />}
      <path class="press-line" d={CONTOUR} pathLength={100} />
    </svg>
  );
}

const hhmm = (): string => new Date().toLocaleTimeString('en-GB', { hour: '2-digit', minute: '2-digit' });

export function Ready({ state, deadline = 0, total = 60000, title, body, chip, notice, onCancel, footer = true, doneTitle, doneBody, receipt }: ReadyProps) {
  const app = useApp();
  const now = useNow(state === 'ready', 250);
  const remaining = Math.max(0, deadline - now);
  const secs = Math.ceil(remaining / 1000);
  const warn = state === 'ready' && remaining <= 10000;
  const [announce, setAnnounce] = useState('');
  const announced = useRef(new Set<number>());
  // Where the armed action types, kept for the receipt once the device has cleared it.
  const via = useRef('');
  const pending = app.device?.pending;
  if (state === 'ready' && pending) via.current = targetName(pending.target ?? null, app.ble, t('bleDevice'));
  const [doneAt, setDoneAt] = useState('');
  useEffect(() => setDoneAt(state === 'typed' ? hhmm() : ''), [state]);

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
  // "Typed · USB · 14:02": the word in the UI font, where and when in monospace (Western digits, LTR).
  const receiptText = receipt ?? (via.current ? t('typedTitle') : '');
  const receiptMono = receipt === undefined && via.current ? [via.current, doneAt].filter(Boolean).join(' · ') : '';

  return (
    <div class={`ready ready-${state}${warn ? ' warn' : ''}`} role="group" aria-labelledby="ready-title">
      <div class="ring-wrap">
        <svg class="ring" viewBox="0 0 200 200" width="176" height="176" aria-hidden="true">
          <circle class="ring-track" cx="100" cy="100" r="92" />
          {state === 'ready' && (
            <circle
              key={`${state}-${deadline}`}
              class="ring-prog"
              cx="100"
              cy="100"
              r="92"
              stroke-dasharray={C}
              transform="rotate(-90 100 100)"
              style={ringStyle}
            />
          )}
        </svg>
        <PressMark mode={state === 'ready' ? 'wait' : state === 'typing' ? 'typing' : 'done'} />
      </div>
      <h2 class="ready-title" id="ready-title">
        {titleText}
      </h2>
      {bodyText && <p class="ready-body">{bodyText}</p>}
      {state === 'typed' && receiptText && (
        <p class="receipt">
          {receiptText}
          {receiptMono && (
            <>
              {' · '}
              <bdi class="mono" dir="ltr">
                {receiptMono}
              </bdi>
            </>
          )}
        </p>
      )}
      {state === 'ready' && chip && <span class="chip chip-signal">{chip}</span>}
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

/** iPhone, iPad (which reports itself as a Mac with touch) or Android. */
const onPhone = (): boolean =>
  /iPhone|iPad|iPod|Android/.test(navigator.userAgent) || (/Macintosh/.test(navigator.userAgent) && navigator.maxTouchPoints > 1);

/** Under the Ready ring: where the keystrokes will go (SPEC §8.1). USB, the default, needs no note. */
export function HostNotice({ device }: { device: DeviceState }) {
  // A device that stays "connecting" usually still holds an old pairing Keyra no longer has
  // (after an erase or "Forget"): it then refuses to reconnect until it is paired again.
  const [slow, setSlow] = useState(false);
  const connecting = device.host.connecting;
  useEffect(() => {
    setSlow(false);
    if (!connecting) return;
    const id = setTimeout(() => setSlow(true), 12000);
    return () => clearTimeout(id);
  }, [connecting]);
  if (connecting)
    return slow ? (
      <Notice tone="warn" icon="bluetooth">
        {t('connectingSlow')}
      </Notice>
    ) : null; // the title already says it
  const target = device.host.bleTarget;
  if (target)
    return (
      <Notice tone="info" icon="bluetooth">
        {t('readyViaBle', { name: deviceLabel(target, t('bleDevice')) })}
        {/* Typing into the phone this page is open on: the keys land where its cursor is, and here there is none. */}
        {onPhone() && <span class="notice-more">{t('readySamePhone')}</span>}
      </Notice>
    );
  // Until the device reports the armed action, say nothing rather than guess
  // (a guess named the Bluetooth device for a moment when USB was chosen).
  const p = device.pending;
  if (!p || p.target === 'usb') return null;
  if (!p.target)
    return (
      <Notice tone="warn" icon="usb">
        {t('readyNoHost')}
      </Notice>
    );
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
        <PressMark mode="off" />
        <span class="press-icon">
          <Icon name={icon} size={26} />
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
