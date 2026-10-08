// Backup & restore (DESIGN §5.8).
import { useEffect, useState } from 'preact/hooks';
import { Button, Notice, SecretField, Segmented, StrengthMeter } from '../components/ui';
import { Alert, Sheet } from '../components/Sheet';
import { Ready } from '../components/Ready';
import { ApiError, api, isAwaiting } from '../lib/api';
import { usePresence, usePressGate } from '../lib/actions';
import { errorText, isLockedError } from '../lib/errors';
import { accountCount, passkeyCount, t } from '../lib/i18n';
import { back } from '../lib/router';
import { getState, loadEntries, setState, toast } from '../lib/store';
import { minHint } from './common';

export function BackupSheet() {
  return (
    <Sheet title={t('backupTitle')} size="md" onClose={() => back('/')}>
      <div class="backup">
        <BackupPart />
        <RestorePart />
      </div>
    </Sheet>
  );
}

function BackupPart() {
  const [pass, setPass] = useState('');
  const [busy, setBusy] = useState(false);
  // settings.passkeysInBackup decides whether the file holds passkeys; null until known,
  // so the text never claims the wrong thing.
  const [passkeys, setPasskeys] = useState<boolean | null>(null);
  const ok = Array.from(pass).length >= 12;
  // The whole vault leaves Keyra only after a press (SPEC §12.3).
  const gate = usePressGate('backup');
  useEffect(() => {
    api
      .settings()
      .then((s) => setPasskeys(s.passkeysInBackup))
      .catch((e) => !isLockedError(e) && toast(errorText(e), 'error'));
  }, []);

  const download = async () => {
    setBusy(true);
    try {
      const blob = await gate.run(() => api.backup(pass));
      if (!blob) return;
      const d = new Date();
      const name = `keyra-backup-${d.getFullYear()}${String(d.getMonth() + 1).padStart(2, '0')}${String(d.getDate()).padStart(2, '0')}.json`;
      const a = document.createElement('a');
      a.href = URL.createObjectURL(blob);
      a.download = name;
      document.body.appendChild(a);
      a.click();
      a.remove();
      setTimeout(() => URL.revokeObjectURL(a.href), 10000);
      setState({ backupAt: Math.floor(Date.now() / 1000) });
      toast(t('backupSaved'), 'ok');
      setPass('');
    } catch (e) {
      if (!isLockedError(e)) toast(errorText(e), 'error');
    } finally {
      setBusy(false);
    }
  };

  if (gate.phase.kind === 'ready') {
    return (
      <Ready
        state="ready"
        deadline={gate.phase.deadline}
        total={gate.phase.total}
        title={t('backupPressTitle')}
        body={t('backupPressBody')}
        onCancel={gate.cancel}
      />
    );
  }

  return (
    <section class="group">
      <h3 class="section-head">{t('backupHead')}</h3>
      <div class="card pad form">
        {passkeys !== null && <p class="callout">{t(passkeys ? 'backupBody' : 'backupBodyNoPasskeys')}</p>}
        <SecretField
          label={t('backupLabel')}
          value={pass}
          onValue={setPass}
          autocomplete="new-password"
          helper={
            <>
              <StrengthMeter value={pass} />
              {minHint(pass, 12) ?? t('backupHelper')}
            </>
          }
        />
        <Button icon="download" full loading={busy} disabled={!ok} onClick={() => void download()}>
          {t('backupButton')}
        </Button>
      </div>
    </section>
  );
}

function RestorePart() {
  const [file, setFile] = useState<{ name: string; data: unknown } | null>(null);
  const [badFile, setBadFile] = useState(false);
  const [pass, setPass] = useState('');
  const [mode, setMode] = useState<'merge' | 'replace'>('merge');
  const [busy, setBusy] = useState(false);
  const [error, setError] = useState<string | null>(null);
  const [full, setFull] = useState(false);
  const [confirm, setConfirm] = useState(false);
  const presence = usePresence('restore');

  useEffect(() => {
    const k = presence.phase.kind;
    if (k === 'done') {
      // Replace: the vault now is exactly the backup. Passkeys are the backup's only when it
      // had them, so name how many Keyra holds rather than claim they were restored.
      void Promise.all([loadEntries(), api.passkeys().catch(() => null)]).then(([, pk]) => {
        const c = accountCount(getState().entries?.length ?? 0);
        const n = pk?.passkeys.length ?? 0;
        toast(n ? t('restoreReplacedPasskeys', { c, p: passkeyCount(n) }) : t('restoreReplaced', { c }), 'ok');
      });
      setPass('');
      setFile(null);
    } else if (k === 'failed') setError(t('restoreWrong'));
    else if (k === 'expired' || k === 'cancelled') toast(t('cancelled'));
  }, [presence.phase.kind]);

  const pick = async (f: File | undefined) => {
    setBadFile(false);
    setError(null);
    setFull(false);
    if (!f) return;
    try {
      setFile({ name: f.name, data: JSON.parse(await f.text()) });
    } catch {
      setFile(null);
      setBadFile(true);
    }
  };

  const restore = async () => {
    if (!file) return;
    setConfirm(false);
    setBusy(true);
    setError(null);
    setFull(false);
    const sent = Date.now();
    try {
      const r = await api.restore(pass, file.data, mode);
      if (isAwaiting(r)) presence.watch(sent, r);
      else {
        await loadEntries();
        const counts = { a: r.added, u: r.updated, p: passkeyCount(r.passkeys) };
        toast(t(r.passkeys > 0 ? 'restoreResultPasskeys' : 'restoreResult', counts), 'ok');
        setPass('');
        setFile(null);
      }
    } catch (e) {
      if (e instanceof ApiError && ['wrong', 'corrupt', 'invalid'].includes(e.code)) setError(t('restoreWrong'));
      else if (e instanceof ApiError && e.code === 'passkeys_full') setFull(true);
      else if (!isLockedError(e)) toast(errorText(e), 'error');
    } finally {
      setBusy(false);
    }
  };

  if (presence.phase.kind === 'ready') {
    return (
      <Ready
        state="ready"
        deadline={presence.phase.deadline}
        total={presence.phase.total}
        title={t('pressToConfirm')}
        body={t('replaceHelper')}
        onCancel={presence.abandon}
      />
    );
  }

  return (
    <section class="group">
      <h3 class="section-head">{t('restoreHead')}</h3>
      <div class="card pad form">
        <label class="btn btn-secondary btn-md btn-full file-btn">
          {file ? <span dir="ltr">{file.name}</span> : t('chooseBackup')}
          <input type="file" accept=".json,application/json" class="sr-only" onChange={(e) => void pick(e.currentTarget.files?.[0])} />
        </label>
        {badFile && <Notice tone="err">{t('restoreWrong')}</Notice>}
        <SecretField label={t('backupPassLabel')} value={pass} onValue={setPass} autocomplete="off" error={error} />
        <Segmented
          label={t('restoreHead')}
          options={[
            { value: 'merge', label: t('merge') },
            { value: 'replace', label: t('replace') },
          ]}
          value={mode}
          onChange={setMode}
        />
        <p class="field-help">{mode === 'merge' ? t('mergeHelper') : t('replaceHelper')}</p>
        {full && <Notice tone="err">{t('passkeysFull')}</Notice>}
        <Button
          icon="upload"
          full
          loading={busy}
          disabled={!file || !pass}
          onClick={() => (mode === 'replace' ? setConfirm(true) : void restore())}
        >
          {t('restore')}
        </Button>
      </div>
      {confirm && (
        <Alert
          title={t('replaceTitle')}
          body={t('replaceHelper')}
          actions={[{ label: t('replace'), variant: 'danger-confirm', run: () => void restore() }]}
          onCancel={() => setConfirm(false)}
        />
      )}
    </section>
  );
}
