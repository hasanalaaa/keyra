// Settings → Trusted browsers (SPEC §8.2): browsers approved with the button to unlock
// through the home network. Removing one signs it out and makes it ask again.
import { useEffect, useState } from 'preact/hooks';
import { IconButton, Spinner } from '../components/ui';
import { Alert, Sheet } from '../components/Sheet';
import { api } from '../lib/api';
import { errorText, isLockedError } from '../lib/errors';
import { t } from '../lib/i18n';
import { toast, useApp } from '../lib/store';
import type { TrustedBrowser } from '../lib/types';
import { shortDate } from '../lib/wifi';

export function TrustedSheet({ onClose }: { onClose: () => void }) {
  const app = useApp();
  const [list, setList] = useState<TrustedBrowser[] | null>(null);
  const [confirm, setConfirm] = useState<TrustedBrowser | null>(null);

  const load = () =>
    api
      .trusted()
      .then(setList)
      .catch((e) => {
        setList([]);
        if (!isLockedError(e)) toast(errorText(e), 'error');
      });
  useEffect(() => void load(), []);

  const remove = async (b: TrustedBrowser) => {
    setConfirm(null);
    try {
      await api.revokeTrusted(b.id);
      toast(t('trustedRemoved'), 'ok');
      void load();
    } catch (e) {
      if (!isLockedError(e)) toast(errorText(e), 'error');
    }
  };

  return (
    <Sheet title={t('trustedRow')} size="md" onClose={onClose}>
      <div class="form trusted">
        <p class="callout">{t('trustedFoot')}</p>
        {list === null ? (
          <p class="waiting-row" role="status">
            <Spinner />
          </p>
        ) : list.length === 0 ? (
          <p class="callout center">{t('trustedNone')}</p>
        ) : (
          <ul class="card rows">
            {list.map((b) => (
              <li key={b.id}>
                <div class="row trusted-row">
                  <span class="row-label">
                    <bdi dir="ltr">{b.name}</bdi>
                    <span class="caption">{t('trustedSeen', { date: shortDate(b.lastSeen || b.created, app.lang) })}</span>
                  </span>
                  {b.current && <span class="chip chip-accent">{t('trustedThis')}</span>}
                  <IconButton icon="trash-2" label={`${t('trustedRemove')} ${b.name}`} onClick={() => setConfirm(b)} />
                </div>
              </li>
            ))}
          </ul>
        )}
      </div>
      {confirm && (
        <Alert
          title={t('trustedRemoveTitle')}
          body={t('trustedRemoveBody')}
          actions={[{ label: t('trustedRemove'), variant: 'danger-confirm', run: () => void remove(confirm) }]}
          onCancel={() => setConfirm(null)}
        />
      )}
    </Sheet>
  );
}
