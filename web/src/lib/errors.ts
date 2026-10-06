// One place that turns API failures into user-facing copy (SPEC §5 error codes).
import { ApiError } from './api';
import { t, type Key } from './i18n';

export function errorText(e: unknown, fallback: Key = 'genericError'): string {
  if (e instanceof ApiError) {
    if (e.code === 'busy') return t(e.status === 409 ? 'busyOther' : 'busyNow');
    if (e.code === 'full') return t('full');
    if (e.code === 'too_large') return t('tooLarge');
  }
  return t(fallback);
}

/** 401s are handled globally (the app shows Unlock), so callers should not toast them. */
export const isLockedError = (e: unknown): boolean => e instanceof ApiError && e.status === 401 && e.code === 'locked';
