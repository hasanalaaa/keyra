// Settings → Activity (SPEC §15): what happened on this Keyra, newest first.
// The device keeps it encrypted, never with a password in it, and offers no way
// to clear it, so a borrowed session cannot hide what it did.
import { useEffect, useState } from 'preact/hooks';
import { Icon, type IconName } from '../components/Icon';
import { Spinner } from '../components/ui';
import { Sheet } from '../components/Sheet';
import { api } from '../lib/api';
import { errorText, isLockedError } from '../lib/errors';
import { t, type Key } from '../lib/i18n';
import { toast, useApp } from '../lib/store';
import type { ActivityEvent } from '../lib/types';

const LOCK_TEXT: Key[] = ['actLock', 'actLockIdle', 'actLockUsb', 'actLockBle', 'actLockButton'];

/** One line of text for an event (exported for tests). `account` names an entry id (agent events carry the id only). */
export function eventText(e: ActivityEvent, account: (id: number) => string | undefined = () => undefined): string {
  const title = e.title ?? '';
  const acc = () => account(e.id ?? 0) ?? `#${e.id ?? 0}`;
  const times = (e.n ?? 0) > 1 ? ` ×${e.n}` : '';
  const over = e.detail === 1 ? t('actOverBle') : '';
  switch (e.kind) {
    case 'unlock': return t(e.detail === 1 ? 'actUnlockRecovery' : 'actUnlock');
    case 'failed_unlocks': return t('actFailed', { n: e.n ?? 0 });
    case 'lock': return t(LOCK_TEXT[e.detail] ?? 'actLock');
    case 'typed': return t('actTyped', { title }) + over;
    case 'text_typed': return t('actTextTyped') + over;
    case 'revealed': return t('actRevealed', { title });
    case 'backup': return t('actBackup');
    case 'restore': return t('actRestore', { n: e.n ?? 0 });
    case 'passphrase': return t('actPassphrase');
    case 'recovery_created': return t('actRecoveryCreated');
    case 'recovery_removed': return t('actRecoveryRemoved');
    case 'ble_forgot': return t('actBleForgot', { title });
    case 'ble_pairing': return t('actBlePairing');
    case 'rotate_started': return t('actRotateStarted');
    case 'rotate_ended': return t('actRotateEnded');
    case 'trusted_removed': return t('actTrustedRemoved', { title });
    case 'entry_deleted': return t('actDeleted', { title });
    case 'entry_burned': return t('actBurned', { title });
    case 'token_created': return t('actTokenCreated', { title });
    case 'token_revoked': return t('actTokenRevoked', { title });
    case 'agent_listed': return t('actAgentListed', { title }) + times;
    case 'agent_armed': {
      // SPEC §9.4: detail + 4 = typed on another site's page, title "<token> → <page host>".
      const [name, host] = e.detail >= 4 ? title.split(' → ') : [title];
      return host ? t('actAgentArmedHost', { title: name, account: acc(), host }) : t('actAgentArmed', { title, account: acc() });
    }
    case 'agent_saved': return t('actAgentSaved', { title, account: acc() });
    case 'agent_generated': return t('actAgentGenerated', { title }) + times;
    case 'tag_created': return t('actTagCreated', { title });
    case 'tag_revoked': return t('actTagRevoked', { title });
    case 'tag_tapped': return t('actTagTapped', { title, account: acc() });
    case 'tag_refused': return t('actTagRefused', { title, why: t(e.detail === 1 ? 'tagWhyReplay' : e.detail === 2 ? 'tagWhyChip' : 'tagWhyWrong') }) + times;
    default: return t('actOther');
  }
}

function iconOf(e: ActivityEvent): IconName {
  switch (e.kind) {
    case 'failed_unlocks': return 'triangle-alert';
    case 'unlock':
    case 'lock': return 'lock';
    case 'typed':
    case 'text_typed': return 'keyboard';
    case 'revealed': return 'eye';
    case 'backup': return 'download';
    case 'restore': return 'upload';
    case 'passphrase':
    case 'rotate_started':
    case 'rotate_ended':
    case 'recovery_created':
    case 'recovery_removed': return 'key-round';
    case 'ble_forgot':
    case 'ble_pairing': return 'bluetooth';
    case 'entry_deleted':
    case 'entry_burned': return 'trash-2';
    case 'agent_armed':
    case 'tag_tapped': return 'keyboard';
    case 'token_created':
    case 'token_revoked':
    case 'agent_listed':
    case 'agent_saved':
    case 'agent_generated':
    case 'tag_created':
    case 'tag_revoked':
    case 'tag_refused': return 'key-round';
    default: return 'shield-check';
  }
}

function when(at: number, lang: 'ar' | 'en'): string {
  if (!at) return t('actNoClock');
  return new Date(at * 1000).toLocaleString(lang === 'ar' ? 'ar-u-nu-latn' : 'en', {
    day: 'numeric',
    month: 'short',
    hour: 'numeric',
    minute: '2-digit',
  });
}

export function ActivitySheet({ onClose }: { onClose: () => void }) {
  const app = useApp();
  const [events, setEvents] = useState<ActivityEvent[] | null>(null);
  const [max, setMax] = useState(200);

  useEffect(() => {
    api
      .activity()
      .then((r) => {
        setEvents(r.events);
        setMax(r.max);
      })
      .catch((e) => {
        setEvents([]);
        if (!isLockedError(e)) toast(errorText(e), 'error');
      });
  }, []);

  return (
    <Sheet title={t('activityRow')} size="md" onClose={onClose}>
      <div class="form activity">
        {events === null ? (
          <p class="waiting-row" role="status">
            <Spinner />
          </p>
        ) : events.length === 0 ? (
          <p class="callout center">{t('activityNone')}</p>
        ) : (
          <ul class="card rows">
            {events.map((e, i) => (
              <li key={i}>
                <div class={`row activity-row${e.kind === 'failed_unlocks' ? ' warn' : ''}`}>
                  <Icon name={iconOf(e)} size={20} class="row-icon" />
                  <span class="row-label">
                    <bdi dir="auto">{eventText(e, (id) => app.entries?.find((x) => x.id === id)?.title)}</bdi>
                    <span class="caption">{when(e.at, app.lang)}</span>
                  </span>
                </div>
              </li>
            ))}
          </ul>
        )}
        <p class="caption">{t('activityFoot', { max })}</p>
      </div>
    </Sheet>
  );
}
