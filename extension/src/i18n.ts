// UI words from _locales (built from strings.ts); the browser's language picks the file.
import type { Key } from './strings';
import { ext } from './ext';

export function t(key: Key, vars: Record<string, string | number> = {}): string {
  const raw = ext.i18n.getMessage(key) || key;
  return raw.replace(/\{(\w+)\}/g, (m, k: string) => (k in vars ? String(vars[k]) : m));
}

/** 'rtl' for Arabic (and any other right-to-left UI language the browser runs in). */
export const uiDir = (): 'rtl' | 'ltr' => (ext.i18n.getMessage('@@bidi_dir') === 'rtl' ? 'rtl' : 'ltr');
export const uiLang = (): string => ext.i18n.getMessage('@@ui_locale').replace('_', '-') || 'en';

export type { Key };
