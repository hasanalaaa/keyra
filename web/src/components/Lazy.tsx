// Screens used rarely load on first use (DESIGN Appendix A): Keyra serves each as its own hashed file,
// cached by the browser for good, so the vault list opens without them. A failed load never leaves a
// blank screen: it says so and offers a retry.
import { h, type ComponentType } from 'preact';
import { useEffect, useState } from 'preact/hooks';
import { Alert } from './Sheet';
import { Spinner } from './ui';
import { t } from '../lib/i18n';
import { back } from '../lib/router';
import { getState } from '../lib/store';

export function lazy<P extends object>(load: () => Promise<ComponentType<P>>): ComponentType<P> {
  let loaded: ComponentType<P> | null = null;
  let inflight: Promise<void> | null = null;
  const fetchOnce = (): Promise<void> =>
    (inflight ??= load().then(
      (c) => {
        loaded = c;
      },
      (e: unknown) => {
        inflight = null;
        throw e;
      },
    ));

  return function Lazy(props: P) {
    const [failed, setFailed] = useState(false);
    const [, setReady] = useState(false);
    const attempt = (onFail: () => void) => {
      setFailed(false);
      fetchOnce().then(() => setReady(true), onFail);
    };
    useEffect(() => {
      if (!loaded) attempt(() => setFailed(true));
    }, []);

    if (loaded) return h(loaded, props);
    if (failed) {
      const onClose = (props as { onClose?: unknown }).onClose;
      return (
        <Alert
          title={t('loadFailTitle')}
          body={t('loadFailBody')}
          actions={[
            {
              label: t('tryAgain'),
              // A browser may remember a failed module fetch, and after a firmware update the old
              // file name is gone: when Keyra answers but the file still won't load, reload the app
              // (the session and the route survive a reload).
              run: () => attempt(() => (getState().online ? location.reload() : setFailed(true))),
            },
          ]}
          onCancel={() => (typeof onClose === 'function' ? onClose() : back('/'))}
        />
      );
    }
    return (
      <div class="lazy-wait" role="status" aria-label={t('loading')}>
        <Spinner size={28} />
      </div>
    );
  };
}
