import { useEffect } from 'preact/hooks';
import { IconSprite, Icon, LogoTile } from './components/Icon';
import { Spinner } from './components/ui';
import { dismissToast, useApp } from './lib/store';
import { replace, useRoute, type Route } from './lib/router';
import { t, type Key } from './lib/i18n';
import { Welcome } from './screens/Welcome';
import { Setup, setupInProgress } from './screens/Setup';
import { Unlock } from './screens/Unlock';
import { Vault } from './screens/Vault';

const TITLES: Partial<Record<Route['name'], Key>> = {
  setup: 'welcomeTitle',
  unlock: 'unlockTitle',
  list: 'accounts',
  new: 'newAccount',
  import: 'importTitle',
  backup: 'backupTitle',
  settings: 'settings',
};

export function App() {
  const app = useApp();
  const route = useRoute();
  const d = app.device;

  useEffect(() => {
    const k = TITLES[route.name];
    document.title = k ? `${t(k)} · Keyra` : 'Keyra';
  }, [route.name, app.lang]);

  let screen;
  let banner = !app.online;
  if (!d) {
    screen = (
      <div class="splash">
        <LogoTile size={72} />
        {app.online && <Spinner size={24} />}
      </div>
    );
  } else if (route.name === 'setup' && (!d.initialized || setupInProgress())) {
    screen = <Setup step={route.step} />;
    banner = false; // the setup screens explain the Wi-Fi restart themselves
  } else if (!d.initialized) {
    screen = <Welcome />;
  } else if (!app.authed) {
    screen = <Unlock reason={app.lockReason} />;
  } else {
    screen = <Vault route={route} />;
  }

  useEffect(() => {
    if (!d) return;
    if (!d.initialized && route.name !== 'welcome' && route.name !== 'setup') replace('/welcome');
    else if (d.initialized && app.authed && ['welcome', 'setup', 'unlock'].includes(route.name)) replace('/');
  }, [d?.initialized, app.authed, route.name]);

  return (
    <>
      <IconSprite />
      <div id="main" class="main">
        {banner && (
          <div class="conn-banner" role="alert">
            <Icon name="wifi" size={20} />
            <span class="conn-text">{t('offline')}</span>
            <span class="conn-retry">
              <Spinner size={16} />
              {t('reconnecting')}
            </span>
          </div>
        )}
        {screen}
      </div>
      {app.toast && (
        <div class="toast-region">
          <div
            key={app.toast.id}
            class={`toast toast-${app.toast.kind}`}
            role={app.toast.kind === 'error' ? 'alert' : 'status'}
            onClick={dismissToast}
          >
            <Icon name={app.toast.kind === 'error' ? 'triangle-alert' : app.toast.kind === 'ok' ? 'check' : 'shield-check'} size={20} />
            <span>{app.toast.text}</span>
            {app.toast.action && (
              <button
                type="button"
                class="toast-action"
                onClick={(e) => {
                  e.stopPropagation();
                  app.toast?.action?.run();
                  dismissToast();
                }}
              >
                {app.toast.action.label}
              </button>
            )}
          </div>
        </div>
      )}
    </>
  );
}
