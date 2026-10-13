// The Type tab: free text, the generator and the type test up front; where Keyra types and how.
import { useEffect, useRef, useState } from 'preact/hooks';
import { Icon, type IconName } from '../components/Icon';
import { Section, Segmented, SwitchRow } from '../components/ui';
import { Sheet } from '../components/Sheet';
import { ErrorCard, HostNotice, Ready, readyText } from '../components/Ready';
import { TargetList } from '../components/HostOs';
import { lazy } from '../components/Lazy';
import { storedTarget, validTarget } from '../lib/ble';
import { useTypeAction } from '../lib/actions';
import { t } from '../lib/i18n';
import { back, go } from '../lib/router';
import { useApp } from '../lib/store';
import { NavRow, TabPage, useSettings } from './prefs';

const BothSequenceSheet = lazy(() => import('./BothSequence').then((m) => m.BothSequenceSheet));

const SPEEDS = [
  { value: 30, key: 'slow' },
  { value: 12, key: 'normal' },
  { value: 5, key: 'fast' },
] as const;

export function Typing({ sub }: { sub: string | null }) {
  const { s, setS, save } = useSettings();
  const [, rerender] = useState(0);
  const close = () => back('/typing');
  const speed = s ? SPEEDS.reduce((a, b) => (Math.abs(b.value - s.keyDelayMs) < Math.abs(a.value - s.keyDelayMs) ? b : a)).value : null;

  return (
    <TabPage title={t('tabType')} name="typing" loaded={!!s}>
      <div class="hero-actions">
        <HeroAction icon="type" title={t('typeTextTitle')} sub={t('typeTextSub')} onClick={() => go('/type')} />
        <HeroAction icon="wand-sparkles" title={t('generate')} sub={t('generateSub')} onClick={() => go('/generate')} />
        <HeroAction icon="zap" title={t('typeTest')} sub={t('typeTestSub')} onClick={() => go('/typing/test')} wide />
      </div>
      <section class="group">
        <h2 class="section-head">{t('groupTypingTo')}</h2>
        <TargetList onPick={() => rerender((n) => n + 1)} />
        <button type="button" class="link-line group-link" onClick={() => go('/device/pair')}>
          {t('blePairNew')}
        </button>
      </section>
      {s && (
        <Section title={t('groupHowTyped')} footer={`${t('footSpeed')} ${t('footSubmit')}`}>
          <div class="row stack-row">
            <span class="row-label">{t('typingSpeed')}</span>
            <Segmented label={t('typingSpeed')} options={SPEEDS.map((o) => ({ value: o.value, label: t(o.key) }))} value={speed} onChange={(v) => void save({ keyDelayMs: v })} />
          </div>
          <div class="row stack-row">
            <span class="row-label">{t('betweenFields')}</span>
            <Segmented
              label={t('betweenFields')}
              options={[
                { value: 'tab', label: 'Tab' },
                { value: 'enter', label: 'Enter' },
              ]}
              value={s.bothSeparator}
              onChange={(v) => void save({ bothSeparator: v })}
            />
          </div>
          <SwitchRow label={t('submitAfterBoth')} checked={s.submitAfterBoth} onChange={(v) => void save({ submitAfterBoth: v })} />
          <NavRow icon="list-ordered" label={t('bothSeqRow')} value={s.bothSequence ? t('bothSeqCustom') : t('bothSeqBuiltIn')} onClick={() => go('/typing/order')} />
        </Section>
      )}
      {sub === 'test' && <TypeTestSheet onClose={close} />}
      {sub === 'order' && s && <BothSequenceSheet settings={s} onSaved={setS} onClose={close} />}
    </TabPage>
  );
}

function HeroAction({ icon, title, sub, onClick, wide }: { icon: IconName; title: string; sub: string; onClick: () => void; wide?: boolean }) {
  return (
    <button type="button" class={`hero-action${wide ? ' wide' : ''}`} onClick={onClick}>
      <span class="hero-action-icon">
        <Icon name={icon} size={22} />
      </span>
      <span class="hero-action-text">
        <span class="hero-action-title">{title}</span>
        <span class="hero-action-sub">{sub}</span>
      </span>
    </button>
  );
}

function TypeTestSheet({ onClose }: { onClose: () => void }) {
  const app = useApp();
  const action = useTypeAction(0);
  const active = useRef(false);
  useEffect(() => {
    if (action.phase.kind === 'idle') void action.start('test', validTarget(storedTarget(), app.ble) ?? undefined).then((ok) => !ok && onClose());
  }, []);
  // Close once the test has run its course (typed dwell finished, or cancelled on the device).
  useEffect(() => {
    if (action.phase.kind !== 'idle') active.current = true;
    else if (active.current) onClose();
  }, [action.phase.kind]);
  const p = action.phase;
  return (
    <Sheet title={t('typeTest')} size="md" onClose={onClose} dismissible={p.kind !== 'ready'} hideTitle>
      {p.kind === 'error' ? (
        <ErrorCard
          icon={p.code === 'no_usb' ? 'usb' : p.code === 'no_host' ? 'bluetooth' : p.code === 'expired' ? 'clock' : 'triangle-alert'}
          tone={p.code === 'no_usb' || p.code === 'no_host' || p.code === 'expired' ? 'warn' : 'err'}
          title={p.code === 'no_usb' ? t('errNoUsbTitle') : p.code === 'no_host' ? t('errNoHostTitle') : p.code === 'expired' ? t('errExpiredTitle') : t('errFailedTitle')}
          body={p.code === 'no_usb' ? t('errNoUsbBody') : p.code === 'no_host' ? t('errNoHostBody') : p.code === 'expired' ? t('errExpiredBody') : t('errFailedBody')}
          primary={{ label: t('tryAgain'), run: () => void action.start('test', validTarget(storedTarget(), app.ble) ?? undefined) }}
          ghost={{ label: t('close'), run: onClose }}
        />
      ) : (
        <Ready
          state={p.kind === 'idle' ? 'ready' : p.kind}
          deadline={p.kind === 'ready' ? p.deadline : Date.now() + 60000}
          total={p.kind === 'ready' ? p.total : 60000}
          {...readyText(app.device, t('typeTestBody'))}
          chip={t('typeTest')}
          notice={app.device ? <HostNotice device={app.device} /> : undefined}
          onCancel={() => {
            void action.cancel();
            onClose();
          }}
        />
      )}
    </Sheet>
  );
}
