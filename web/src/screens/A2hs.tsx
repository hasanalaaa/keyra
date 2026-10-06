// Add-to-Home-Screen hint (DESIGN §5.10).
import { useRef } from 'preact/hooks';
import { Icon } from '../components/Icon';
import { Button } from '../components/ui';
import { Sheet, type SheetCtl } from '../components/Sheet';
import { t } from '../lib/i18n';

const KEY = 'keyra.a2hs';
const WEEK = 7 * 24 * 3600 * 1000;

const isIos = () => /iPhone|iPad|iPod/.test(navigator.userAgent);
const standalone = () =>
  (navigator as Navigator & { standalone?: boolean }).standalone === true || matchMedia('(display-mode: standalone)').matches;

export function shouldOfferA2hs(): boolean {
  if (standalone() || matchMedia('(min-width: 900px)').matches) return false;
  try {
    const v = localStorage.getItem(KEY);
    if (v === 'never') return false;
    return !v || Date.now() - Number(v) > WEEK;
  } catch {
    return false; // can't remember the answer, so don't nag every visit
  }
}

function remember(v: string): void {
  try {
    localStorage.setItem(KEY, v);
  } catch {
    // Not persisted; the hint may show again next time.
  }
}

export function A2hsSheet({ onClose }: { onClose: () => void }) {
  const ctl = useRef<SheetCtl | null>(null);
  const steps = isIos()
    ? [
        <>
          {t('a2hsIos1')} <Icon name="share" size={20} class="inline-icon" />
        </>,
        t('a2hsIos2'),
        t('a2hsIos3'),
      ]
    : [t('a2hsAnd1'), t('a2hsAnd2')];
  const close = (v: string) => {
    remember(v);
    ctl.current?.close();
  };
  return (
    <Sheet title={t('a2hsTitle')} size="sm" modal={false} ctl={ctl} onClose={onClose}>
      <div class="a2hs">
        <p class="callout">{t('a2hsBody')}</p>
        <ol class="howto">
          {steps.map((s, i) => (
            <li key={i}>{s}</li>
          ))}
        </ol>
        <div class="sheet-foot">
          <Button variant="secondary" onClick={() => close(String(Date.now()))}>
            {t('notNow')}
          </Button>
          <Button onClick={() => close('never')}>{t('gotIt')}</Button>
        </div>
      </div>
    </Sheet>
  );
}
