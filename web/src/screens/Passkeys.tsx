// Settings → Passkeys (docs/FIDO.md): the discoverable FIDO credentials stored on
// Keyra. Websites create them over USB; here they can only be reviewed and deleted.
import { useEffect, useRef, useState } from 'preact/hooks';
import { IconButton, Spinner } from '../components/ui';
import { Alert, Sheet } from '../components/Sheet';
import { Ready } from '../components/Ready';
import { usePresence } from '../lib/actions';
import { api } from '../lib/api';
import { errorText, isLockedError } from '../lib/errors';
import { t } from '../lib/i18n';
import { toast, useApp } from '../lib/store';
import type { Passkey } from '../lib/types';
import { shortDate } from '../lib/wifi';

export function PasskeysSheet({ onClose }: { onClose: () => void }) {
  const app = useApp();
  const [list, setList] = useState<Passkey[] | null>(null);
  const [max, setMax] = useState(50);
  const [confirm, setConfirm] = useState<Passkey | null>(null);

  const load = () =>
    api
      .passkeys()
      .then((r) => {
        setList(r.passkeys);
        setMax(r.max);
      })
      .catch((e) => {
        setList([]);
        if (!isLockedError(e)) toast(errorText(e), 'error');
      });
  useEffect(() => void load(), []);

  // Deleting a passkey is a press of Keyra's button (SPEC §5); cancelled or
  // expired leaves it in place.
  const del = usePresence('delete_passkey');
  const deleting = useRef(0);
  useEffect(() => {
    const k = del.phase.kind;
    if (k === 'idle' || k === 'ready') return;
    del.abandon();
    if (k === 'failed') toast(t('genericError'), 'error');
    if (k !== 'done') return;
    toast(t('passkeyDeleted'), 'ok');
    setList((l) => l && l.filter((x) => x.id !== deleting.current)); // not shown again while the list reloads
    void load();
  }, [del.phase.kind]);

  const remove = (p: Passkey) => {
    setConfirm(null);
    deleting.current = p.id;
    void del.start(() => api.deletePasskey(p.id));
  };

  return (
    <Sheet title={t('passkeysRow')} size="md" onClose={onClose}>
      {del.phase.kind === 'ready' ? (
        <Ready state="ready" deadline={del.phase.deadline} total={del.phase.total} title={t('passkeyDeletePress')} body={t('deletePressBody')} onCancel={del.abandon} />
      ) : (
      <div class="form passkeys">
        <p class="callout">{t('passkeysFoot')}</p>
        {list === null ? (
          <p class="waiting-row" role="status">
            <Spinner />
          </p>
        ) : list.length === 0 ? (
          <p class="callout center">{t('passkeysNone')}</p>
        ) : (
          <>
            <ul class="card rows">
              {list.map((p) => (
                <li key={p.id}>
                  <div class="row passkey-row">
                    <span class="row-label">
                      <bdi dir="ltr">{p.rpId || '—'}</bdi>
                      <span class="caption">
                        <bdi dir="auto">{p.userName || p.displayName}</bdi>
                        {p.created ? ` · ${t('passkeysCreated', { date: shortDate(p.created, app.lang) })}` : ''}
                      </span>
                    </span>
                    <IconButton icon="trash-2" label={`${t('passkeyDelete')} ${p.rpId}`} onClick={() => setConfirm(p)} />
                  </div>
                </li>
              ))}
            </ul>
            <p class="caption center">{t('passkeysCount', { n: list.length, max })}</p>
          </>
        )}
        <p class="caption">{t('passkeysLimits')}</p>
      </div>
      )}
      {confirm && (
        <Alert
          title={t('passkeyDeleteTitle')}
          body={t('passkeyDeleteBody', { site: confirm.rpId })}
          actions={[{ label: t('passkeyDelete'), variant: 'danger-confirm', run: () => remove(confirm) }]}
          onCancel={() => setConfirm(null)}
        />
      )}
    </Sheet>
  );
}
