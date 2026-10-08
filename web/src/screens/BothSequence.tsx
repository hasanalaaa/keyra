// Settings → Typing → Order for "Both" (SPEC §10.4): settings.bothSequence, '' = the built-in order.
import { useRef, useState } from 'preact/hooks';
import { Button } from '../components/ui';
import { Sheet, type SheetCtl } from '../components/Sheet';
import { SequenceEditor, SequencePreview, sequenceError } from '../components/SequenceEditor';
import { api } from '../lib/api';
import { errorText, isLockedError } from '../lib/errors';
import { t } from '../lib/i18n';
import { builtInBoth } from '../lib/sequence';
import { setState, toast } from '../lib/store';
import type { Settings } from '../lib/types';

export function BothSequenceSheet({ settings, onSaved, onClose }: { settings: Settings; onSaved: (s: Settings) => void; onClose: () => void }) {
  const saved = settings.bothSequence ?? '';
  const [draft, setDraft] = useState(saved);
  const [busy, setBusy] = useState(false);
  const ctl = useRef<SheetCtl | null>(null);
  const err = sequenceError(draft);

  const put = async (bothSequence: string) => {
    setBusy(true);
    try {
      const s = (await api.putSettings({ bothSequence })) as Settings;
      onSaved(s);
      setState({ bothSequence: s.bothSequence ?? '' });
      toast(t('saved'), 'ok');
      ctl.current?.close();
    } catch (e) {
      setBusy(false);
      if (!isLockedError(e)) toast(errorText(e, 'saveError'), 'error');
    }
  };

  return (
    <Sheet title={t('bothSeqRow')} size="md" ctl={ctl} onClose={onClose}>
      <form
        class="form both-seq"
        onSubmit={(e) => {
          e.preventDefault();
          if (!err && draft !== saved) void put(draft);
        }}
      >
        <p class="callout">{t('bothSeqHelp')}</p>
        <SequenceEditor label={t('seqLabel')} value={draft} onValue={setDraft} />
        {draft === '' && (
          <div class="seq-preview">
            <span class="caption">{t('bothSeqBuiltIn')}</span>
            <SequencePreview preview={builtInBoth(settings.bothSeparator === 'enter', settings.submitAfterBoth)} />
          </div>
        )}
        <Button type="submit" size="lg" full loading={busy} disabled={!!err || draft === saved}>
          {t('save')}
        </Button>
        {saved !== '' && (
          <Button variant="ghost" full disabled={busy} onClick={() => void put('')}>
            {t('bothSeqReset')}
          </Button>
        )}
      </form>
    </Sheet>
  );
}
