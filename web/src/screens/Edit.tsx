// Add / Edit account (DESIGN §5.6) with the password generator inline (§4.8, SPEC §9.1).
import { useEffect, useMemo, useRef, useState } from 'preact/hooks';
import { Button, Notice, SecretField, StrengthMeter, SwitchRow, TextField } from '../components/ui';
import { InlineGenerator, Stepper } from '../components/Generator';
import { QR_ERRORS, QrPhoto } from '../components/QrPhoto';
import { Alert, Sheet, type SheetCtl } from '../components/Sheet';
import { Ready } from '../components/Ready';
import { Icon } from '../components/Icon';
import { SequenceEditor, sequenceError } from '../components/SequenceEditor';
import { ApiError, api } from '../lib/api';
import { usePressGate } from '../lib/actions';
import { errorText, isLockedError } from '../lib/errors';
import { takeDraftPassword } from '../lib/draft';
import { toTypeable, untypeable } from '../lib/generator';
import { t } from '../lib/i18n';
import { back, replace } from '../lib/router';
import { parseQrText, titleOf, toOtpauth, type OtpAccount } from '../lib/qrImport';
import { normalizeTotp } from '../lib/totp';
import { loadEntries, toast } from '../lib/store';
import type { EntryInput } from '../lib/types';
import { ENTRY_MAX, bytes } from '../lib/limits';
import { parseSequence } from '../lib/sequence';

const EMPTY: EntryInput = { title: '', url: '', username: '', password: '', totp: '', notes: '', favorite: false, burnAfter: 0, sequence: '' };

export function EditAccount({ id }: { id?: number }) {
  const [initial, setInitial] = useState<EntryInput | null>(id ? null : EMPTY);
  // A password made in the Generate sheet arrives prefilled (still unsaved, so the form is dirty).
  const [form, setForm] = useState<EntryInput>(() => (id ? EMPTY : { ...EMPTY, password: takeDraftPassword() }));
  const [touched, setTouched] = useState<Record<string, boolean>>({});
  const [saving, setSaving] = useState(false);
  const [gen, setGen] = useState(false);
  const [confirm, setConfirm] = useState<'discard' | 'delete' | null>(null);
  const [qrError, setQrError] = useState<string | null>(null);
  const [seqOpen, setSeqOpen] = useState(false); // advanced: collapsed until asked for
  const ctl = useRef<SheetCtl | null>(null);
  const after = useRef<() => void>(() => back(id ? `/a/${id}` : '/'));

  // Editing shows the password: one press of Keyra's button (SPEC §12.3), none inside the grace minute.
  const gate = usePressGate('reveal');
  useEffect(() => {
    if (!id) return;
    let live = true;
    gate
      .run(() => api.reveal(id))
      .then((e) => {
        if (!live) return;
        if (!e) return ctl.current?.close(); // cancelled or not pressed: nothing to edit
        const v: EntryInput = { title: e.title, url: e.url, username: e.username, password: e.password ?? '', totp: e.totp ?? '', notes: e.notes, favorite: e.favorite, burnAfter: e.burnAfter ?? 0, sequence: e.sequence ?? '' };
        setInitial(v);
        setForm(v);
      })
      .catch((err) => {
        if (live && !isLockedError(err)) toast(errorText(err), 'error');
      });
    return () => {
      live = false;
    };
  }, [id]);

  const set = <K extends keyof EntryInput>(k: K) => (v: EntryInput[K]) => setForm((f) => ({ ...f, [k]: v }));
  const blur = (k: string) => () => setTouched((x) => ({ ...x, [k]: true }));
  const dirty = initial !== null && (Object.keys(form) as (keyof EntryInput)[]).some((k) => form[k] !== initial[k]);
  const totpNorm = normalizeTotp(form.totp);
  // The device's per-field limits (bytes): say which field is too long instead
  // of a generic "couldn't save" after the round trip.
  const over = (k: keyof typeof ENTRY_MAX) => (bytes(form[k]) > ENTRY_MAX[k] ? t('tooLongField', { n: ENTRY_MAX[k] }) : null);
  const tooLong = (['title', 'url', 'username', 'password', 'totp', 'notes'] as const).some((k) => over(k));
  const nameErr = !form.title.trim() ? t('nameRequired') : over('title');
  const totpErr = totpNorm === null ? t('totpError') : null;
  const bad = useMemo(() => untypeable(form.password), [form.password]);
  const seqErr = sequenceError(form.sequence);
  // Masked: literal text in a sequence may be a secret.
  const seqPreview = useMemo(() => {
    const r = form.sequence ? parseSequence(form.sequence) : null;
    return r?.ok ? r.preview : '';
  }, [form.sequence]);

  const onQr = (text: string) => {
    const r = parseQrText(text);
    if (!r.ok) return setQrError(t(QR_ERRORS[r.error]));
    if (r.value.kind === 'migration') {
      const { accounts } = r.value.migration;
      if (accounts.length !== 1) return setQrError(t('qrMany', { n: accounts.length }));
      return fill(accounts[0]);
    }
    fill(r.value.account);
  };
  const fill = (a: OtpAccount) => {
    setQrError(null);
    setTouched((x) => ({ ...x, totp: true }));
    setForm((f) => ({ ...f, totp: toOtpauth(a), title: f.title.trim() ? f.title : titleOf(a), username: f.username.trim() ? f.username : a.account }));
    toast(t('qrFilled'), 'ok');
  };

  const save = async () => {
    setTouched({ title: true, totp: true });
    if (seqErr) setSeqOpen(true);
    if (nameErr || totpErr || seqErr || tooLong || saving || !initial) return;
    setSaving(true);
    const body: EntryInput = { ...form, title: form.title.trim(), url: form.url.trim(), username: form.username.trim(), totp: totpNorm ?? '' };
    try {
      let newId = id;
      if (id) {
        const patch: Partial<EntryInput> = {};
        for (const k of Object.keys(body) as (keyof EntryInput)[]) if (body[k] !== initial[k]) (patch as Record<string, unknown>)[k] = body[k];
        await api.update(id, patch);
      } else newId = (await api.create(body)).id;
      toast(t('saved'), 'ok');
      await loadEntries();
      after.current = () => {
        if (id) back(`/a/${id}`);
        else replace('/');
        requestAnimationFrame(() => document.querySelector(`[data-id="${newId}"]`)?.scrollIntoView({ block: 'center' }));
      };
      setInitial(form); // no longer dirty
      ctl.current?.close();
    } catch (e) {
      setSaving(false);
      if (e instanceof ApiError && e.code === 'full') toast(t('full'), 'error');
      else if (!(e instanceof ApiError && e.status === 401)) toast(t('saveError'), 'error');
    }
  };

  const remove = async () => {
    if (!id) return;
    try {
      await api.remove(id);
      toast(t('deleted'), 'ok');
      await loadEntries();
      after.current = () => replace('/');
      setInitial(form);
      setConfirm(null);
      ctl.current?.close();
    } catch {
      toast(t('genericError'), 'error');
    }
  };

  return (
    <Sheet
      title={id ? t('editAccount') : t('newAccount')}
      size="lg"
      tall
      ctl={ctl}
      onClose={() => after.current()}
      canClose={() => {
        if (!dirty) return true;
        setConfirm('discard');
        return false;
      }}
      start={
        <Button variant="ghost" size="sm" class="save-btn" disabled={!form.title.trim() || tooLong || !!seqErr || saving || !initial} onClick={() => void save()}>
          {t('save')}
        </Button>
      }
    >
      {gate.phase.kind === 'ready' ? (
        <Ready
          state="ready"
          deadline={gate.phase.deadline}
          total={gate.phase.total}
          title={t('editRevealTitle')}
          body={t('editRevealBody')}
          onCancel={() => {
            gate.cancel();
            ctl.current?.close();
          }}
        />
      ) : (
      <form
        class="form edit-form"
        onSubmit={(e) => {
          e.preventDefault();
          void save();
        }}
      >
        <TextField label={t('name')} placeholder={t('namePh')} value={form.title} onValue={set('title')} onBlur={blur('title')} error={touched.title ? nameErr : null} enterkeyhint="next" />
        <TextField label={t('websiteOpt')} placeholder="example.com" value={form.url} onValue={set('url')} error={over('url')} ltr inputMode="url" autocapitalize="off" spellcheck={false} enterkeyhint="next" />
        <TextField label={t('username')} value={form.username} onValue={(v) => set('username')(toTypeable(v))} error={over('username')} ltr autocorrect="off" autocapitalize="off" spellcheck={false} enterkeyhint="next" />
        <div class="pw-block">
          <SecretField
            label={t('password')}
            value={form.password}
            onValue={(v) => set('password')(toTypeable(v))}
            autocomplete="off"
            enterkeyhint="next"
            helper={form.password ? <StrengthMeter value={form.password} /> : undefined}
          />
          {bad.length > 0 && <Notice tone="warn">{t('unsupportedChar', { c: bad.join(' ') })}</Notice>}
          {over('password') && <Notice tone="err">{over('password')}</Notice>}
          <Button variant="ghost" size="sm" icon="wand-sparkles" class="gen-toggle" onClick={() => setGen(!gen)}>
            {t('createPassword')}
          </Button>
          {gen && (
            <InlineGenerator
              onUse={(pw) => {
                set('password')(toTypeable(pw));
                setGen(false);
              }}
            />
          )}
        </div>
        <div class="pw-block">
          <TextField
            label={t('totpKey')}
            value={form.totp}
            onValue={set('totp')}
            onBlur={blur('totp')}
            ltr
            class="mono-input"
            autocapitalize="off"
            spellcheck={false}
            helper={t('totpHelper')}
            error={(touched.totp ? totpErr : null) ?? over('totp')}
            enterkeyhint="next"
          />
          <QrPhoto label={t('scanQr')} onText={onQr} onNone={() => setQrError(t('qrNone'))} />
          {qrError && <Notice tone="err">{qrError}</Notice>}
        </div>
        <div class="field">
          <label class="field-label" for="notes">
            {t('notes')}
          </label>
          <textarea id="notes" class="input textarea" dir="auto" rows={4} value={form.notes} onInput={(e) => {
            const el = e.currentTarget;
            set('notes')(el.value);
            el.style.blockSize = 'auto';
            el.style.blockSize = `${Math.min(el.scrollHeight, 8 * 27 + 28)}px`;
          }} />
          {over('notes') && (
            <p class="field-help field-error" role="alert">
              {over('notes')}
            </p>
          )}
        </div>
        <div class="card">
          <SwitchRow label={t('addToFavorites')} checked={form.favorite} onChange={set('favorite')} />
          <SwitchRow label={t('burnToggle')} checked={form.burnAfter > 0} onChange={(on) => set('burnAfter')(on ? 1 : 0)} />
          {form.burnAfter > 0 && (
            <Stepper label={t('burnTimes')} value={form.burnAfter} min={1} max={99} onChange={set('burnAfter')} />
          )}
        </div>
        {form.burnAfter > 0 && <p class="group-foot burn-foot">{t('burnHelp', { n: form.burnAfter })}</p>}
        <div class={`card seq-block${seqOpen ? ' open' : ''}`}>
          <button type="button" class="row nav-row seq-toggle" aria-expanded={seqOpen} onClick={() => setSeqOpen(!seqOpen)}>
            <span class="row-label">
              {t('seqToggle')}
              <span class="caption">{seqPreview ? <bdi dir="ltr" class="seq-sum mono">{seqPreview}</bdi> : t('seqToggleSub')}</span>
            </span>
            <Icon name="chevron-right" size={16} class="row-chev" />
          </button>
          {seqOpen && (
            <div class="seq-body">
              <SequenceEditor
                label={t('seqLabel')}
                value={form.sequence}
                onValue={set('sequence')}
                have={{ username: !!form.username.trim(), password: !!form.password, totp: !!form.totp.trim() }}
              />
              {form.sequence && (
                <Button variant="ghost" size="sm" icon="trash-2" onClick={() => set('sequence')('')}>
                  {t('seqClear')}
                </Button>
              )}
            </div>
          )}
        </div>
        {id && (
          <Button variant="danger" full icon="trash-2" class="delete-btn" onClick={() => setConfirm('delete')}>
            {t('deleteAccount')}
          </Button>
        )}
        <button type="submit" hidden />
      </form>
      )}
      {confirm === 'discard' && (
        <Alert
          title={t('discardTitle')}
          actions={[
            {
              label: t('discard'),
              variant: 'danger-confirm',
              run: () => {
                setConfirm(null);
                setInitial(form);
                ctl.current?.close();
              },
            },
          ]}
          cancelLabel={t('keepEditing')}
          onCancel={() => setConfirm(null)}
        />
      )}
      {confirm === 'delete' && (
        <Alert
          title={t('deleteTitle', { title: initial?.title ?? '' })}
          body={t('deleteBody')}
          actions={[{ label: t('delete'), variant: 'danger-confirm', run: () => void remove() }]}
          onCancel={() => setConfirm(null)}
        />
      )}
    </Sheet>
  );
}
