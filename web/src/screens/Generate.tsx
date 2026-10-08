// The vault's Generate sheet (SPEC §9.1): a new password from Keyra's hardware RNG, then
// Type it / Type twice (sign-up "confirm" fields), Copy, or Save as a new account or as the
// new password of an existing one (the old one moves to that account's history, §9.3).
import { useMemo, useRef, useState } from 'preact/hooks';
import { Icon } from '../components/Icon';
import { FreeTextStatus, GenOptions, GenPreview, GenStrength, useGenerator } from '../components/Generator';
import { Button, Monogram } from '../components/ui';
import { HostLangRow, TargetPicker } from '../components/HostOs';
import { Alert, Sheet, type SheetCtl } from '../components/Sheet';
import { ApiError, api } from '../lib/api';
import { useTypeAction } from '../lib/actions';
import { storedTarget, validTarget } from '../lib/ble';
import { osOf, resolveTarget } from '../lib/hostos';
import { copyText } from '../lib/clipboard';
import { hostOf } from '../lib/csv';
import { setDraftPassword } from '../lib/draft';
import { toTypeable } from '../lib/generator';
import { t } from '../lib/i18n';
import { back, replace } from '../lib/router';
import { search } from '../lib/search';
import { loadEntries, toast, useApp } from '../lib/store';
import type { EntrySummary } from '../lib/types';

type View = 'gen' | 'save' | 'pick';

export function GenerateSheet() {
  const app = useApp();
  const gen = useGenerator();
  const action = useTypeAction(0, 'text');
  const [twice, setTwice] = useState(false);
  const [view, setView] = useState<View>('gen');
  const [q, setQ] = useState('');
  const [target, setTarget] = useState<EntrySummary | null>(null);
  const [saving, setSaving] = useState(false);
  const [, setPicked] = useState(0); // re-render after the picker stores a choice
  const hostTarget = resolveTarget(validTarget(storedTarget(), app.ble), app.device?.host.output ?? null, app.ble);
  const ctl = useRef<SheetCtl | null>(null);
  const after = useRef<() => void>(() => back('/'));

  const typeIt = (repeat: 1 | 2) => {
    setTwice(repeat === 2);
    void action.start('text', validTarget(storedTarget(), app.ble) ?? undefined, { text: gen.password, repeat, separator: 'tab' });
  };
  const close = (then: () => void) => {
    after.current = then;
    ctl.current?.close();
  };

  const saveNew = () => {
    setDraftPassword(toTypeable(gen.password));
    close(() => replace('/new'));
  };
  const update = async () => {
    if (!target || saving) return;
    setSaving(true);
    try {
      await api.update(target.id, { password: toTypeable(gen.password) });
      await loadEntries();
      toast(t('replaced'), 'ok');
      const id = target.id;
      setTarget(null);
      close(() => replace(`/a/${id}`));
    } catch (e) {
      setSaving(false);
      setTarget(null);
      if (!(e instanceof ApiError && e.status === 401)) toast(e instanceof ApiError && e.code === 'full' ? t('full') : t('saveError'), 'error');
    }
  };

  const results = useMemo(() => {
    const all = app.entries ?? [];
    return q.trim() ? search(all, q).map((m) => m.entry) : [...all].sort((a, b) => a.title.localeCompare(b.title));
  }, [app.entries, q]);

  const phase = action.phase;
  const title = view === 'save' ? t('saveTitle') : view === 'pick' ? t('pickTitle') : t('genTitle');
  let body;
  if (phase.kind !== 'idle') {
    body = (
      <FreeTextStatus
        phase={phase}
        chip={twice ? t('chipNewPasswordTwice') : t('chipNewPassword')}
        body={twice ? t('readyTwiceBody') : t('readyTextBody')}
        retry={() => typeIt(twice ? 2 : 1)}
        close={action.dismiss}
        cancel={() => void action.cancel()}
      />
    );
  } else if (view === 'save') {
    body = (
      <div class="gen">
        <GenPreview password={gen.password} busy={false} />
        <div class="card">
          <button type="button" class="row nav-row save-row save-new" onClick={saveNew}>
            <Icon name="plus" size={24} class="row-icon" />
            <span class="row-text">
              <span class="row-title">{t('saveNew')}</span>
              <span class="row-sub">{t('saveNewSub')}</span>
            </span>
            <Icon name="chevron-right" size={16} class="row-chev" />
          </button>
          <button type="button" class="row nav-row save-row save-update" onClick={() => setView('pick')}>
            <Icon name="pencil" size={24} class="row-icon" />
            <span class="row-text">
              <span class="row-title">{t('saveUpdate')}</span>
              <span class="row-sub">{t('saveUpdateSub')}</span>
            </span>
            <Icon name="chevron-right" size={16} class="row-chev" />
          </button>
        </div>
        <Button variant="ghost" onClick={() => setView('gen')}>
          {t('back')}
        </Button>
      </div>
    );
  } else if (view === 'pick') {
    body = (
      <div class="gen pick">
        <label class="search">
          <Icon name="search" size={20} />
          <input
            type="search"
            inputMode="search"
            autocomplete="off"
            spellcheck={false}
            placeholder={t('searchPlaceholder')}
            aria-label={t('searchPlaceholder')}
            value={q}
            onInput={(e) => setQ(e.currentTarget.value)}
          />
        </label>
        {results.length === 0 && q.trim() !== '' && <p class="callout center">{t('noMatchTitle')}</p>}
        <ul class="card rows" hidden={results.length === 0}>
          {results.map((e) => (
            <li key={e.id}>
              <button type="button" class="row acc-row" data-id={e.id} onClick={() => setTarget(e)}>
                <Monogram title={e.title} />
                <span class="row-text">
                  <span class="row-title" dir="auto">
                    {e.title}
                  </span>
                  <span class="row-sub" dir="ltr">
                    {e.username || hostOf(e.url)}
                  </span>
                </span>
                <Icon name="chevron-right" size={16} class="row-chev" />
              </button>
            </li>
          ))}
        </ul>
        <Button variant="ghost" onClick={() => setView('save')}>
          {t('back')}
        </Button>
      </div>
    );
  } else {
    body = (
      <div class="gen">
        <GenPreview password={gen.password} busy={gen.busy} />
        <GenStrength gen={gen} />
        <div class="gen-quick">
          <Button variant="secondary" size="sm" icon="refresh-cw" onClick={gen.regenerate}>
            {t('newOne')}
          </Button>
          <Button variant="secondary" size="sm" icon="copy" disabled={!gen.password} onClick={() => copyText(gen.password) && toast(t('copied'), 'ok')}>
            {t('copy')}
          </Button>
        </div>
        <div class="gen-actions">
          <TargetPicker onPick={() => setPicked((n) => n + 1)} />
          <HostLangRow key={hostTarget ?? ''} target={hostTarget} os={osOf(hostTarget, app.device?.host.usbOs, app.ble)} />
          <Button full icon="keyboard" class="gen-type" disabled={!gen.password} onClick={() => typeIt(1)}>
            {t('typeIt')}
          </Button>
          <div class="gen-pair">
            <Button variant="tinted" class="gen-twice" disabled={!gen.password} onClick={() => typeIt(2)}>
              {t('typeTwice')}
            </Button>
            <Button variant="secondary" icon="download" class="gen-save" disabled={!gen.password} onClick={() => setView('save')}>
              {t('saveIt')}
            </Button>
          </div>
        </div>
        <GenOptions gen={gen} />
        <p class="caption gen-from">
          <Icon name="shield-check" size={16} />
          {t('genFrom')}
        </p>
      </div>
    );
  }

  return (
    <Sheet title={title} size="md" ctl={ctl} onClose={() => after.current()} dismissible={phase.kind !== 'ready'}>
      {body}
      {target && (
        <Alert
          title={t('swapTitle', { title: target.title })}
          body={t('swapBody')}
          actions={[{ label: t('swapConfirm'), run: () => void update() }]}
          onCancel={() => setTarget(null)}
        />
      )}
    </Sheet>
  );
}
