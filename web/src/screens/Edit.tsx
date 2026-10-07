// Add / Edit account (DESIGN §5.6) and the password generator sheet (§4.8).
import { useEffect, useMemo, useRef, useState } from 'preact/hooks';
import { Button, ColoredSecret, Notice, SecretField, Slider, StrengthMeter, SwitchRow, TextField } from '../components/ui';
import { QR_ERRORS, QrPhoto } from '../components/QrPhoto';
import { Alert, Sheet, type SheetCtl } from '../components/Sheet';
import { ApiError, api } from '../lib/api';
import { copyText } from '../lib/clipboard';
import { DEFAULT_GEN, generatePassword, toTypeable, untypeable, type GenOptions } from '../lib/generator';
import { t } from '../lib/i18n';
import { back, replace } from '../lib/router';
import { parseQrText, titleOf, toOtpauth, type OtpAccount } from '../lib/qrImport';
import { normalizeTotp } from '../lib/totp';
import { loadEntries, toast } from '../lib/store';
import type { EntryInput } from '../lib/types';

const EMPTY: EntryInput = { title: '', url: '', username: '', password: '', totp: '', notes: '', favorite: false };

export function EditAccount({ id }: { id?: number }) {
  const [initial, setInitial] = useState<EntryInput | null>(id ? null : EMPTY);
  const [form, setForm] = useState<EntryInput>(EMPTY);
  const [touched, setTouched] = useState<Record<string, boolean>>({});
  const [saving, setSaving] = useState(false);
  const [gen, setGen] = useState(false);
  const [confirm, setConfirm] = useState<'discard' | 'delete' | null>(null);
  const [qrError, setQrError] = useState<string | null>(null);
  const ctl = useRef<SheetCtl | null>(null);
  const after = useRef<() => void>(() => back(id ? `/a/${id}` : '/'));

  useEffect(() => {
    if (!id) return;
    api
      .entry(id)
      .then((e) => {
        const v: EntryInput = { title: e.title, url: e.url, username: e.username, password: e.password, totp: e.totp, notes: e.notes, favorite: e.favorite };
        setInitial(v);
        setForm(v);
      })
      .catch(() => toast(t('genericError'), 'error'));
  }, [id]);

  const set = <K extends keyof EntryInput>(k: K) => (v: EntryInput[K]) => setForm((f) => ({ ...f, [k]: v }));
  const blur = (k: string) => () => setTouched((x) => ({ ...x, [k]: true }));
  const dirty = initial !== null && (Object.keys(form) as (keyof EntryInput)[]).some((k) => form[k] !== initial[k]);
  const totpNorm = normalizeTotp(form.totp);
  const nameErr = !form.title.trim() ? t('nameRequired') : null;
  const totpErr = totpNorm === null ? t('totpError') : null;
  const bad = useMemo(() => untypeable(form.password), [form.password]);

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
    if (nameErr || totpErr || saving || !initial) return;
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
        <Button variant="ghost" size="sm" class="save-btn" disabled={!form.title.trim() || saving || !initial} onClick={() => void save()}>
          {t('save')}
        </Button>
      }
    >
      <form
        class="form edit-form"
        onSubmit={(e) => {
          e.preventDefault();
          void save();
        }}
      >
        <TextField label={t('name')} placeholder={t('namePh')} value={form.title} onValue={set('title')} onBlur={blur('title')} error={touched.title ? nameErr : null} enterkeyhint="next" />
        <TextField label={t('websiteOpt')} placeholder="example.com" value={form.url} onValue={set('url')} ltr inputMode="url" autocapitalize="off" spellcheck={false} enterkeyhint="next" />
        <TextField label={t('username')} value={form.username} onValue={(v) => set('username')(toTypeable(v))} ltr autocorrect="off" autocapitalize="off" spellcheck={false} enterkeyhint="next" />
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
          <Button variant="ghost" size="sm" icon="wand-sparkles" onClick={() => setGen(true)}>
            {t('createPassword')}
          </Button>
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
            error={touched.totp ? totpErr : null}
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
        </div>
        <div class="card">
          <SwitchRow label={t('addToFavorites')} checked={form.favorite} onChange={set('favorite')} />
        </div>
        {id && (
          <Button variant="danger" full icon="trash-2" class="delete-btn" onClick={() => setConfirm('delete')}>
            {t('deleteAccount')}
          </Button>
        )}
        <button type="submit" hidden />
      </form>
      {gen && (
        <GeneratorSheet
          onClose={() => setGen(false)}
          onUse={(pw) => {
            set('password')(pw);
            setGen(false);
          }}
        />
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

function GeneratorSheet({ onClose, onUse }: { onClose: () => void; onUse: (pw: string) => void }) {
  const [opts, setOpts] = useState<GenOptions>(DEFAULT_GEN);
  const [pw, setPw] = useState('');
  const [failed, setFailed] = useState(false);
  const ctl = useRef<SheetCtl | null>(null);

  const regen = (o: GenOptions) => {
    try {
      setPw(generatePassword(o));
    } catch {
      setFailed(true);
    }
  };
  useEffect(() => regen(opts), [opts]);

  const on = [opts.upper, opts.lower, opts.digits, opts.symbols].filter(Boolean).length;
  const toggle = (k: 'upper' | 'lower' | 'digits' | 'symbols' | 'avoidLookAlikes', label: string) => (
    <SwitchRow label={label} checked={opts[k]} disabled={k !== 'avoidLookAlikes' && opts[k] && on === 1} onChange={(v) => setOpts({ ...opts, [k]: v })} />
  );

  return (
    <Sheet title={t('createPassword')} size="md" ctl={ctl} onClose={onClose}>
      {failed ? (
        <Notice tone="err">{t('noRandom')}</Notice>
      ) : (
        <div class="gen">
          <button type="button" class="gen-preview" onClick={() => copyText(pw) && toast(t('copied'), 'ok')} aria-label={`${t('copy')} ${pw}`}>
            <ColoredSecret value={pw} />
          </button>
          <StrengthMeter value={pw} />
          <div class="card">
            <div class="row slider-row">
              <span class="row-label">{t('length')}</span>
              <Slider value={opts.length} min={12} max={40} label={t('length')} onInput={(n) => setOpts({ ...opts, length: n })} />
              <span class="row-value mono">{opts.length}</span>
            </div>
            {toggle('upper', t('upper'))}
            {toggle('lower', t('lower'))}
            {toggle('digits', t('digits'))}
            {toggle('symbols', t('symbols'))}
            {toggle('avoidLookAlikes', t('lookAlikes'))}
          </div>
          <div class="sheet-foot">
            <Button variant="secondary" icon="refresh-cw" onClick={() => regen(opts)}>
              {t('newOne')}
            </Button>
            <Button onClick={() => onUse(pw)}>{t('usePassword')}</Button>
          </div>
        </div>
      )}
    </Sheet>
  );
}
