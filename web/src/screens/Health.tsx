// Settings → Password health (SPEC §13): weak, reused and old passwords. Keyra
// works it out itself and sends only entry ids, so no password reaches the phone.
import { useEffect, useState } from 'preact/hooks';
import { Icon } from '../components/Icon';
import { Monogram, Spinner } from '../components/ui';
import { Sheet } from '../components/Sheet';
import { api } from '../lib/api';
import { errorText, isLockedError } from '../lib/errors';
import { accountCount, t } from '../lib/i18n';
import { go } from '../lib/router';
import { loadEntries, toast, useApp } from '../lib/store';
import type { EntrySummary, Health } from '../lib/types';
import { shortDate } from '../lib/wifi';

/** Accounts flagged at least once (a reused weak password counts once). */
export function issueCount(h: Health): number {
  return new Set([...h.weak.map((w) => w.id), ...h.reused.flat(), ...h.old.map((o) => o.id)]).size;
}

export function HealthSheet({ onClose }: { onClose: () => void }) {
  const app = useApp();
  const [h, setH] = useState<Health | null>(null);
  const [failed, setFailed] = useState(false);

  useEffect(() => {
    if (!app.entries) void loadEntries();
    api
      .health()
      .then(setH)
      .catch((e) => {
        setFailed(true);
        if (!isLockedError(e)) toast(errorText(e), 'error');
      });
  }, []);

  const byId = new Map((app.entries ?? []).map((e) => [e.id, e]));
  const row = (id: number, sub: string) => {
    const e = byId.get(id);
    return e ? <HealthRow key={id} e={e} sub={sub} /> : null;
  };

  let body;
  if (failed) body = <p class="callout center">{t('healthFailed')}</p>;
  else if (!h || !app.entries)
    body = (
      <p class="waiting-row" role="status">
        <Spinner />
      </p>
    );
  else {
    const n = issueCount(h);
    body = (
      <>
        <div class={`card health-summary ${n ? 'warn' : 'ok'}`} role="status">
          <Icon name={n ? 'triangle-alert' : 'shield-check'} size={28} />
          <span class="row-label">
            <strong>{n ? t('healthIssues', { accounts: accountCount(n) }) : t('healthAllGood')}</strong>
            <span class="caption">{t('healthChecked', { accounts: accountCount(h.checked) })}</span>
          </span>
        </div>
        {h.reused.length > 0 && (
          <section class="group">
            <h2 class="section-head">{t('healthReused')}</h2>
            <p class="group-foot">{t('healthReusedWhy')}</p>
            {h.reused.map((g) => (
              <ul class="card rows" key={g.join('-')}>
                {g.map((id) => row(id, g.length === 2 ? t('healthShared2') : t('healthShared', { n: g.length, accounts: accountCount(g.length) })))}
              </ul>
            ))}
          </section>
        )}
        {h.weak.length > 0 && (
          <section class="group">
            <h2 class="section-head">{t('healthWeak')}</h2>
            <p class="group-foot">{t('healthWeakWhy')}</p>
            <ul class="card rows">{h.weak.map((w) => row(w.id, t(w.level === 1 ? 'weak' : 'fair')))}</ul>
          </section>
        )}
        {h.old.length > 0 && (
          <section class="group">
            <h2 class="section-head">{t('healthOld')}</h2>
            <p class="group-foot">{t('healthOldWhy')}</p>
            <ul class="card rows">{h.old.map((o) => row(o.id, t('healthSince', { date: shortDate(o.since, app.lang) })))}</ul>
          </section>
        )}
        {!h.clock && <p class="caption">{t('healthNoClock')}</p>}
      </>
    );
  }

  return (
    <Sheet title={t('healthRow')} size="md" onClose={onClose}>
      <div class="form health">
        {body}
        <p class="caption">{t('healthPrivacy')}</p>
      </div>
    </Sheet>
  );
}

function HealthRow({ e, sub }: { e: EntrySummary; sub: string }) {
  return (
    <li>
      <button type="button" class="row acc-row" onClick={() => go(`/a/${e.id}`)}>
        <Monogram title={e.title} />
        <span class="row-text">
          <span class="row-title" dir="auto">
            {e.title}
          </span>
          <span class="row-sub">{sub}</span>
        </span>
        <Icon name="chevron-right" size={16} class="row-chev" />
      </button>
    </li>
  );
}
