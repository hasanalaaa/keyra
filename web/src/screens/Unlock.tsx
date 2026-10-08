// Unlock and Locked (DESIGN §5.3, §5.11), plus "Forgot passphrase?" → factory reset with the button.
import { passphraseOk } from '../lib/limits';
import { useEffect, useRef, useState } from 'preact/hooks';
import { KeyGlyph, LogoTile } from '../components/Icon';
import { Button, SecretField, TextField, Notice } from '../components/ui';
import { QrPhoto } from '../components/QrPhoto';
import { combineShares, parseKey, parseShare, toHex } from '../lib/recovery';
import { Alert } from '../components/Sheet';
import { Ready } from '../components/Ready';
import { ApiError, api } from '../lib/api';
import { useNow, usePresence } from '../lib/actions';
import { errorText } from '../lib/errors';
import { clock, t } from '../lib/i18n';
import { replace } from '../lib/router';
import { toast, unlock, unlockRecovery, useApp, type LockReason } from '../lib/store';
import { LangButton, minHint } from './common';

export function Unlock({ reason }: { reason: LockReason }) {
  const app = useApp();
  const [revealed, setRevealed] = useState(reason === null);
  const [erase, setErase] = useState<'idle' | 'confirm' | 'wait'>('idle');
  const [recover, setRecover] = useState(false);
  const presence = usePresence('factory_reset', { doneOnDisconnect: true });

  useEffect(() => {
    const k = presence.phase.kind;
    if (k === 'done') {
      replace('/welcome');
    } else if (k === 'failed') {
      toast(t('genericError'), 'error');
      setErase('idle');
    } else if (k === 'expired' || k === 'cancelled') setErase('idle');
  }, [presence.phase.kind]);

  if (erase === 'wait' && presence.phase.kind === 'ready') {
    return (
      <div class="page glow-page">
        <div class="hero-col">
          <div class="hero-card">
            <Ready
              state="ready"
              deadline={presence.phase.deadline}
              total={presence.phase.total}
              title={t('eraseWaitTitle')}
              body={t('s3Body')}
              onCancel={() => {
                presence.abandon();
                setErase('idle');
              }}
            />
          </div>
        </div>
      </div>
    );
  }

  const reasonText =
    reason === 'idle'
      ? t('lockedIdle', { n: app.device?.autoLockMin ?? 15 })
      : reason === 'button'
        ? t('lockedButton')
        : reason === 'manual'
          ? t('lockedManual')
          : reason === 'session'
            ? t('lockedSession')
            : reason === 'unplugged'
              ? t('lockedUnplugged')
              : null;

  return (
    <div class={`page ${reason ? 'locked-page' : 'glow-page'}`}>
      <div class="top-actions">
        <LangButton />
      </div>
      <div class="hero-col">
        <div class="hero-card unlock">
          {reason ? <KeyGlyph size={72} class="locked-glyph" /> : <LogoTile size={72} />}
          <h1 class="display">{reason ? t('lockedTitle') : t('unlockTitle')}</h1>
          {reasonText && <p class="subtitle">{reasonText}</p>}
          {recover ? (
            <RecoverForm onBack={() => setRecover(false)} />
          ) : revealed ? (
            <UnlockForm onForgot={() => setErase('confirm')} onRecover={() => setRecover(true)} />
          ) : (
            <Button size="lg" full onClick={() => setRevealed(true)}>
              {t('unlock')}
            </Button>
          )}
        </div>
      </div>
      {erase === 'confirm' && (
        <Alert
          title={t('eraseTitle')}
          body={t('eraseBody')}
          actions={[
            {
              label: t('eraseConfirm'),
              variant: 'danger-confirm',
              run: () => {
                setErase('wait');
                void presence.start(api.factoryReset).then((ok) => !ok && setErase('idle'));
              },
            },
          ]}
          onCancel={() => setErase('idle')}
        />
      )}
    </div>
  );
}

function UnlockForm({ onForgot, onRecover }: { onForgot: () => void; onRecover: () => void }) {
  const [pass, setPass] = useState('');
  // Home network (SPEC §8.2): a new browser is trusted with one press, then the unlock is retried.
  const trust = usePresence('trust_browser');
  const retried = useRef(false);
  const [busy, setBusy] = useState(false);
  const [slow, setSlow] = useState(false);
  const [wrong, setWrong] = useState(false);
  const [shake, setShake] = useState(0);
  const [retryUntil, setRetryUntil] = useState(0);
  const input = useRef<HTMLInputElement>(null);
  const now = useNow(retryUntil > Date.now(), 500);
  const waiting = retryUntil > now;

  useEffect(() => {
    if (!busy) return;
    const h = setTimeout(() => setSlow(true), 700);
    return () => clearTimeout(h);
  }, [busy]);

  useEffect(() => {
    const k = trust.phase.kind;
    if (k === 'done') {
      trust.abandon();
      if (retried.current) {
        // Approved, yet the device asked again: don't loop on the button.
        toast(t('trustFailed'), 'error');
        return;
      }
      retried.current = true;
      void attempt();
    } else if (k === 'failed' || k === 'expired' || k === 'cancelled') {
      trust.abandon();
      toast(t('trustFailed'), 'error');
    }
  }, [trust.phase.kind]);

  const attempt = async () => {
    setBusy(true);
    setSlow(false);
    setWrong(false);
    try {
      const sent = Date.now();
      const awaiting = await unlock(pass);
      if (awaiting) {
        trust.watch(sent, awaiting);
        setBusy(false);
      }
    } catch (err) {
      if (err instanceof ApiError && (err.code === 'wrong' || err.code === 'rate_limited')) {
        setWrong(err.code === 'wrong');
        if (err.retryAfterMs > 0) setRetryUntil(Date.now() + err.retryAfterMs);
        setShake(1);
        input.current?.select();
      } else if (err instanceof ApiError && err.code === 'busy') toast(errorText(err), 'error');
      else if (!(err instanceof ApiError && err.status === 0)) toast(t('genericError'), 'error');
      setBusy(false);
    }
  };

  const submit = (e: Event) => {
    e.preventDefault();
    if (!pass || busy || waiting) return;
    retried.current = false;
    void attempt();
  };

  if (trust.phase.kind === 'ready') {
    return (
      <Ready
        state="ready"
        deadline={trust.phase.deadline}
        total={trust.phase.total}
        title={t('trustTitle')}
        body={t('trustBody')}
        onCancel={trust.abandon}
      />
    );
  }

  const error = waiting ? t('rateLimited', { t: clock(retryUntil - now) }) : wrong ? t('wrongPassphrase') : null;

  return (
    <form class={`form unlock-form${shake ? ' shake' : ''}`} onSubmit={submit} onAnimationEnd={() => setShake(0)}>
      <SecretField
        label={t('unlockLabel')}
        value={pass}
        onValue={(v) => {
          setPass(v);
          setWrong(false);
        }}
        inputRef={input}
        autocomplete="current-password"
        autofocus
        enterkeyhint="go"
        error={error}
      />
      <Button type="submit" size="lg" full loading={busy} label={busy ? t('unlocking') : undefined} disabled={!pass || waiting}>
        {t('unlock')}
      </Button>
      <p class={`caption center slow-caption${slow ? ' in' : ''}`} aria-live="polite">
        {slow ? t('unlockSlow') : ''}
      </p>
      <Button variant="ghost" onClick={onRecover}>
        {t('useRecovery')}
      </Button>
      <Button variant="ghost" onClick={onForgot}>
        {t('forgot')}
      </Button>
    </form>
  );
}

/**
 * Forgotten passphrase (SPEC §12.2): the recovery key, typed or rebuilt here from Shamir shares,
 * sets a new passphrase and unlocks. Shares are combined in this page; only the key is sent.
 */
function RecoverForm({ onBack }: { onBack: () => void }) {
  const [mode, setMode] = useState<'key' | 'shares'>('key');
  const [keyText, setKeyText] = useState('');
  const [shares, setShares] = useState(['', '']);
  const [pass, setPass] = useState('');
  const [pass2, setPass2] = useState('');
  const [error, setError] = useState<string | null>(null);
  const [busy, setBusy] = useState(false);
  const [retryUntil, setRetryUntil] = useState(0);
  const trust = usePresence('trust_browser');
  const lastKey = useRef('');
  const now = useNow(retryUntil > Date.now(), 500);
  const waiting = retryUntil > now;
  const passOk = passphraseOk(pass) && pass === pass2;
  // A key or share problem belongs to the key, not to the passphrase fields;
  // it goes away as soon as the key or shares change.
  useEffect(() => setError(null), [keyText, shares, mode]);

  const send = async (hex: string) => {
    setBusy(true);
    try {
      const sent = Date.now();
      const awaiting = await unlockRecovery(hex, pass);
      if (awaiting) trust.watch(sent, awaiting);
      else toast(t('recoverDone'), 'ok');
    } catch (err) {
      if (err instanceof ApiError && (err.code === 'wrong' || err.code === 'rate_limited')) {
        setError(err.code === 'wrong' ? t('recoverWrongKey') : null);
        if (err.retryAfterMs > 0) setRetryUntil(Date.now() + err.retryAfterMs);
      } else if (!(err instanceof ApiError && err.status === 0)) toast(errorText(err), 'error');
    } finally {
      setBusy(false);
    }
  };

  useEffect(() => {
    const k = trust.phase.kind;
    if (k === 'idle' || k === 'ready') return;
    trust.abandon();
    if (k === 'done') void send(lastKey.current);
    else toast(t('trustFailed'), 'error');
  }, [trust.phase.kind]);

  const submit = async (e: Event) => {
    e.preventDefault();
    if (busy || waiting || !passOk) return;
    setError(null);
    let key: Uint8Array;
    if (mode === 'key') {
      const k = parseKey(keyText);
      if (typeof k === 'string') return setError(t('recoverBadKey'));
      key = k;
    } else {
      const parsed = [];
      for (const [i, text] of shares.entries()) {
        if (!text.trim()) continue;
        const p = parseShare(text);
        if (typeof p === 'string') return setError(t('shareBad', { i: i + 1 }));
        parsed.push(p);
      }
      const k = await combineShares(parsed);
      if (k === 'too_few') return setError(t('sharesTooFew', { k: parsed[0]?.threshold ?? 2 }));
      if (k === 'mismatch') return setError(t('sharesMismatch'));
      key = k;
    }
    lastKey.current = toHex(key);
    await send(lastKey.current);
  };

  if (trust.phase.kind === 'ready') {
    return <Ready state="ready" deadline={trust.phase.deadline} total={trust.phase.total} title={t('trustTitle')} body={t('trustBody')} onCancel={trust.abandon} />;
  }

  return (
    <form class="form unlock-form recover-form" onSubmit={(e) => void submit(e)}>
      <h2 class="t3">{t('recoverTitle')}</h2>
      {mode === 'key' ? (
        <>
          <TextField
            label={t('recoveryKeyLabel')}
            value={keyText}
            onValue={setKeyText}
            ltr
            class="mono-input"
            autocapitalize="characters"
            spellcheck={false}
            helper={t('recoverKeyHelp')}
            placeholder="XXXX-XXXX-…"
          />
          <QrPhoto label={t('scanQr')} onText={setKeyText} onNone={() => setError(t('qrNone'))} />
        </>
      ) : (
        <>
          {shares.map((v, i) => (
            <TextField
              key={i}
              label={t('recoverShareLabel', { i: i + 1 })}
              value={v}
              onValue={(x) => setShares(shares.map((y, j) => (j === i ? x : y)))}
              ltr
              class="mono-input"
              autocapitalize="characters"
              spellcheck={false}
            />
          ))}
          <QrPhoto label={t('scanQr')} onText={(x) => setShares([...shares.filter((y) => y.trim()), x])} onNone={() => setError(t('qrNone'))} />
          {shares.length < 10 && (
            <Button variant="ghost" size="sm" onClick={() => setShares([...shares, ''])}>
              {t('recoverAddShare')}
            </Button>
          )}
        </>
      )}
      <Button variant="ghost" size="sm" onClick={() => setMode(mode === 'key' ? 'shares' : 'key')}>
        {mode === 'key' ? t('recoverUseShares') : t('recoverUseKey')}
      </Button>
      <SecretField label={t('newPassphrase')} value={pass} onValue={setPass} autocomplete="new-password" helper={minHint(pass, 10) ?? undefined} />
      <SecretField
        label={t('recoverConfirm')}
        value={pass2}
        onValue={setPass2}
        autocomplete="new-password"
        error={pass2 && pass !== pass2 ? t('mismatch') : null}
      />
      {(waiting || error) && <Notice tone="err">{waiting ? t('rateLimited', { t: clock(retryUntil - now) }) : error}</Notice>}
      <Button type="submit" size="lg" full loading={busy} disabled={!passOk || waiting}>
        {t('recoverSubmit')}
      </Button>
      <Button variant="ghost" onClick={onBack}>
        {t('back')}
      </Button>
    </form>
  );
}
