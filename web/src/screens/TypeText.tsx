// "Type text…" (SPEC §9.2): Keyra as a remote keyboard. The text goes to the device only when
// armed, is typed after a press of the button, and is wiped there right after.
import { useEffect, useState } from 'preact/hooks';
import { api } from '../lib/api';
import { FreeTextStatus } from '../components/Generator';
import { Button, Notice, Segmented, SwitchRow } from '../components/ui';
import { Sheet } from '../components/Sheet';
import { HostLangRow, TargetPicker } from '../components/HostOs';
import { osOf, resolveTarget } from '../lib/hostos';
import { useTypeAction } from '../lib/actions';
import { storedTarget, validTarget } from '../lib/ble';
import { toTypeable, untypeable } from '../lib/generator';
import { t } from '../lib/i18n';
import { layoutName } from '../lib/keyboard';
import { back } from '../lib/router';
import { useApp } from '../lib/store';
import type { Keyboard } from '../lib/types';

const MAX = 256;

/** How a character Keyra can't type is shown in the warning (control characters are invisible). */
function show(ch: string): string {
  if (ch === '\n' || ch === '\r') return '↵';
  if (ch === '\t') return '⇥';
  const c = ch.codePointAt(0)!;
  return c < 0x20 || c === 0x7f ? `U+${c.toString(16).toUpperCase().padStart(4, '0')}` : ch;
}

export function TypeTextSheet() {
  const app = useApp();
  const action = useTypeAction(0, 'text');
  const [text, setText] = useState('');
  const [twice, setTwice] = useState(false);
  const [sep, setSep] = useState<'tab' | 'enter'>('tab');
  const [kbd, setKbd] = useState<Keyboard | null>(null);
  const [, setPicked] = useState(0); // re-render after the picker stores a choice
  useEffect(() => {
    api.keyboard().then(setKbd, () => undefined); // not loaded: the US-ASCII check below still holds
  }, []);
  const hostTarget = resolveTarget(validTarget(storedTarget(), app.ble), app.device?.host.output ?? null, app.ble);
  // The device checks the text against the layout set for the output it types into.
  const layoutId = hostTarget === null ? null : hostTarget === 'usb' ? kbd?.usb : kbd?.ble;
  const layout = kbd?.layouts.find((l) => l.id === layoutId);
  const bad = untypeable(text, layout?.chars);
  const ok = text.length > 0 && text.length <= MAX && bad.length === 0;
  const phase = action.phase;

  // Typed: the device has already forgotten it; forget it here too.
  useEffect(() => {
    if (phase.kind === 'typed') setText('');
  }, [phase.kind]);

  const start = () => {
    if (ok) void action.start('text', validTarget(storedTarget(), app.ble) ?? undefined, { text, repeat: twice ? 2 : 1, separator: sep });
  };

  return (
    <Sheet title={t('typeTextTitle')} size="md" onClose={() => back('/')} dismissible={phase.kind !== 'ready'}>
      {phase.kind !== 'idle' ? (
        <FreeTextStatus
          phase={phase}
          chip={twice ? `${t('chipText')} · ${t('typeTwice')}` : t('chipText')}
          body={t('readyTextBody')}
          retry={start}
          close={action.dismiss}
          cancel={() => void action.cancel()}
        />
      ) : (
        <form
          class="form type-text"
          onSubmit={(e) => {
            e.preventDefault();
            start();
          }}
        >
          <p class="callout">{t('typeTextBody')}</p>
          <div class="field">
            <label class="field-label" for="free-text">
              {t('textLabel')}
            </label>
            <textarea
              id="free-text"
              class="input textarea mono"
              dir="ltr"
              rows={3}
              maxLength={MAX}
              autocomplete="off"
              autocapitalize="off"
              autocorrect="off"
              spellcheck={false}
              value={text}
              // Phones insert curly quotes and Arabic digits; kept only where the layout has them.
              onInput={(e) => setText(toTypeable(e.currentTarget.value, layout?.chars))}
              // Enter can't be typed as text here; it would only add a line break Keyra refuses.
              onKeyDown={(e) => e.key === 'Enter' && !e.shiftKey && (e.preventDefault(), start())}
            />
            <p class="field-help text-count">{t('textCount', { n: text.length })}</p>
          </div>
          {bad.length > 0 && (
            <Notice tone="warn">
              {layout && layout.id !== 'us'
                ? t('textBadLayout', { l: layoutName(layout), c: bad.map(show).join(' ') })
                : t('textBad', { c: bad.map(show).join(' ') })}
            </Notice>
          )}
          <div class="card">
            <SwitchRow label={t('textTwice')} checked={twice} onChange={setTwice} />
            {twice && (
              <div class="row stack-row">
                <span class="row-label">{t('textBetween')}</span>
                <Segmented<'tab' | 'enter'>
                  label={t('textBetween')}
                  options={[
                    { value: 'tab', label: 'Tab' },
                    { value: 'enter', label: 'Enter' },
                  ]}
                  value={sep}
                  onChange={setSep}
                />
              </div>
            )}
          </div>
          <TargetPicker onPick={() => setPicked((n) => n + 1)} />
          <HostLangRow key={hostTarget ?? ''} target={hostTarget} os={osOf(hostTarget, app.device?.host.usbOs, app.ble)} />
          <Button type="submit" full icon="keyboard" disabled={!ok}>
            {t('typeIt')}
          </Button>
          <p class="caption center">{t('textForget')}</p>
        </form>
      )}
    </Sheet>
  );
}
