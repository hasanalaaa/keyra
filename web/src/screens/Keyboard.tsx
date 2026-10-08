// Settings → Typing → Keyboard layout (SPEC §10.1) and the Layout Doctor (SPEC §10.3): pick the
// layout of the computer on each output, or let Keyra type its probe and match what appears.
import { useEffect, useRef, useState } from 'preact/hooks';
import { Icon } from '../components/Icon';
import { Button, Notice, TextField } from '../components/ui';
import { Sheet } from '../components/Sheet';
import { ErrorCard, HostNotice, Ready, readyText } from '../components/Ready';
import { useTypeAction } from '../lib/actions';
import { defaultTarget } from '../lib/ble';
import { byFit, layoutName, matchProbe } from '../lib/keyboard';
import { t, type Key } from '../lib/i18n';
import { loadBle, useApp } from '../lib/store';
import type { HostOs, KeyboardLayout } from '../lib/types';

export type LayoutOutput = 'usb' | 'ble';

const PLATFORM: Record<KeyboardLayout['platform'], Key> = { any: 'layoutAny', windows: 'osWindows', mac: 'osMac' };

/** "German · Mac": the firmware's English name, plus the system when the name alone is ambiguous. */
export const layoutTitle = (l: KeyboardLayout): string => (l.platform === 'any' ? layoutName(l) : `${layoutName(l)} · ${t(PLATFORM[l.platform])}`);

interface Props {
  output: LayoutOutput;
  layouts: KeyboardLayout[];
  value: string;
  usbOs: HostOs;
  onPick: (id: string) => void;
  onClose: () => void;
}

export function LayoutSheet({ output, layouts, value, usbOs, onPick, onClose }: Props) {
  const [doctor, setDoctor] = useState(false);
  // Waiting for the press: only Cancel leaves (DESIGN §4.9), so the probe is never left armed.
  const [armed, setArmed] = useState(false);
  return (
    <Sheet title={doctor ? t('doctorTitle') : output === 'usb' ? t('layoutSheetUsb') : t('layoutSheetBle')} size="md" onClose={onClose} dismissible={!armed}>
      {doctor ? (
        <LayoutDoctor output={output} layouts={layouts} value={value} usbOs={usbOs} onPick={onPick} onBack={() => setDoctor(false)} onArmed={setArmed} />
      ) : (
        <div class="layouts">
          <Button variant="secondary" full icon="keyboard" class="doctor-btn" onClick={() => setDoctor(true)}>
            {t('doctorOpen')}
          </Button>
          <div class="card" role="radiogroup" aria-label={output === 'usb' ? t('layoutUsb') : t('layoutBle')}>
            {layouts.map((l) => (
              <button key={l.id} type="button" role="radio" aria-checked={l.id === value} class="row nav-row layout-row" onClick={() => onPick(l.id)}>
                <span class="row-text">
                  <bdi class="row-title" dir="ltr">
                    {layoutName(l)}
                  </bdi>
                  <span class="row-sub">
                    {t(PLATFORM[l.platform])}
                    {l.experimental && <span class="layout-exp"> · {t('layoutExperimental')}</span>}
                  </span>
                </span>
                {l.id === value && <Icon name="check" size={20} class="accent" />}
              </button>
            ))}
          </div>
          {layouts.some((l) => l.experimental) && <p class="group-foot">{t('layoutExperimentalFoot')}</p>}
        </div>
      )}
    </Sheet>
  );
}

function LayoutDoctor({ output, layouts, value, usbOs, onPick, onBack, onArmed }: Omit<Props, 'onClose'> & { onBack: () => void; onArmed: (armed: boolean) => void }) {
  const app = useApp();
  const action = useTypeAction(0, 'probe');
  const [seen, setSeen] = useState('');
  const [typed, setTyped] = useState(false);
  const bleTarget = defaultTarget('ble', app.ble);
  const target = output === 'usb' ? 'usb' : app.ble?.enabled ? bleTarget : null;
  const os: HostOs = output === 'usb' ? usbOs : (app.ble?.bonds.find((b) => b.addr === target)?.os ?? '');
  const current = layouts.find((l) => l.id === value);

  useEffect(() => {
    if (output === 'ble' && !app.ble) void loadBle();
  }, []);
  // After the ✓ the comparing starts; "Type it again" keeps what was entered.
  useEffect(() => {
    if (action.phase.kind === 'typed') setTyped(true);
  }, [action.phase.kind]);

  const run = () => target && void action.start('probe', target);
  const p = action.phase;
  useEffect(() => onArmed(p.kind === 'ready'), [p.kind]);
  const field = useRef<HTMLDivElement>(null);
  useEffect(() => {
    if (typed && p.kind === 'idle') field.current?.querySelector('input')?.focus();
  }, [typed, p.kind === 'idle']);

  if (p.kind === 'error') {
    const c = p.code;
    return (
      <ErrorCard
        icon={c === 'no_usb' ? 'usb' : c === 'no_host' ? 'bluetooth' : c === 'expired' ? 'clock' : 'triangle-alert'}
        tone={c === 'no_usb' || c === 'no_host' || c === 'expired' ? 'warn' : 'err'}
        title={c === 'no_usb' ? t('errNoUsbTitle') : c === 'no_host' ? t('errNoHostTitle') : c === 'expired' ? t('errExpiredTitle') : t('errFailedTitle')}
        body={c === 'no_usb' ? t('errNoUsbBody') : c === 'no_host' ? t('errNoHostBody') : c === 'expired' ? t('errExpiredBody') : t('errFailedBody')}
        primary={{ label: t('tryAgain'), run }}
        ghost={{ label: t('back'), run: () => (action.dismiss(), onBack()) }}
      />
    );
  }
  if (p.kind !== 'idle') {
    return (
      <Ready
        state={p.kind}
        deadline={p.kind === 'ready' ? p.deadline : 0}
        total={p.kind === 'ready' ? p.total : 60000}
        {...readyText(app.device, t('doctorReadyBody'))}
        chip={t('doctorChip')}
        notice={app.device ? <HostNotice device={app.device} /> : undefined}
        onCancel={() => void action.cancel()}
      />
    );
  }

  const match = matchProbe(seen, layouts, os);
  return (
    <div class="doctor">
      {!typed ? (
        <>
          <p class="callout">{t(output === 'usb' ? 'doctorIntroUsb' : 'doctorIntroBle')}</p>
          {!target && <Notice tone="warn" icon="bluetooth">{t('doctorNoBle')}</Notice>}
          <Button full size="lg" icon="keyboard" class="doctor-type" disabled={!target} onClick={run}>
            {t('doctorType')}
          </Button>
        </>
      ) : (
        <>
          <div ref={field}>
            <TextField
              label={t('doctorSeenLabel')}
              value={seen}
              onValue={setSeen}
              ltr
              class="mono doctor-input"
              autocapitalize="off"
              autocomplete="off"
              spellcheck={false}
              helper={t('doctorSeenHelp')}
            />
          </div>
          {match && match.layouts.length === 0 && <Notice tone="warn">{t('doctorNoMatch')}</Notice>}
          {match && match.layouts.length > 0 && (
            <div class="doctor-result" role="status">
              <p class="callout">{t(match.exact ? 'doctorMatch' : 'doctorClose')}</p>
              {match.layouts.map((l) =>
                l.id === value ? (
                  <Notice key={l.id} tone="accent" icon="check">
                    {t('doctorAlready')} <bdi>{layoutTitle(l)}</bdi>
                  </Notice>
                ) : (
                  <Button key={l.id} full variant={match.exact ? 'primary' : 'secondary'} class="doctor-use" onClick={() => onPick(l.id)}>
                    {t('doctorUse', { name: layoutTitle(l) })}
                  </Button>
                ),
              )}
            </div>
          )}
          <Button variant="secondary" full icon="refresh-cw" onClick={run}>
            {t('doctorAgain')}
          </Button>
        </>
      )}
      {current && (
        <p class="caption">
          {t('doctorNow')} <bdi>{layoutTitle(current)}</bdi>
        </p>
      )}
      {typed && (
        <section class="group doctor-table">
          <h2 class="section-head">{t('doctorCompare')}</h2>
          <div class="card">
            {byFit(layouts, os).map((l) => (
              <button key={l.id} type="button" class="row nav-row probe-row" onClick={() => onPick(l.id)} aria-label={t('doctorUse', { name: layoutTitle(l) })}>
                <span class="row-text">
                  <bdi class="row-title mono probe" dir="ltr">
                    {l.probe}
                  </bdi>
                  <span class="row-sub">{layoutTitle(l)}</span>
                </span>
                {l.id === value && <Icon name="check" size={20} class="accent" />}
              </button>
            ))}
          </div>
          <p class="group-foot">{t('doctorCompareFoot')}</p>
        </section>
      )}
    </div>
  );
}
