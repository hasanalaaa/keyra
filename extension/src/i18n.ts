// UI words from _locales (built from strings.ts); the browser's language picks the file.
import { ar, en, type Key } from './strings';
import { ext } from './ext';

// The e2e builds pin a language (a test cannot always set Chromium's UI language, e.g. on macOS);
// in the real builds __LANG__ is '' and this table is dropped by the bundler.
declare const __LANG__: '' | 'en' | 'ar';
const pinned = __LANG__ ? { en, ar }[__LANG__] : null;

export function t(key: Key, vars: Record<string, string | number> = {}): string {
  const raw = (pinned ? pinned[key] : ext.i18n.getMessage(key)) || key;
  return raw.replace(/\{(\w+)\}/g, (m, k: string) => (k in vars ? String(vars[k]) : m));
}

/** 'rtl' for Arabic (and any other right-to-left UI language the browser runs in). */
export const uiDir = (): 'rtl' | 'ltr' => ((pinned ? __LANG__ === 'ar' : ext.i18n.getMessage('@@bidi_dir') === 'rtl') ? 'rtl' : 'ltr');
export const uiLang = (): string => (pinned ? __LANG__ : ext.i18n.getMessage('@@ui_locale').replace('_', '-') || 'en');

export type { Key };
