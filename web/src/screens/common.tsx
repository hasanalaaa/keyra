// Small pieces shared by several screens.
import { Button } from '../components/ui';
import { t } from '../lib/i18n';
import { setLangPref, useApp } from '../lib/store';

/** Ghost "English" / "العربية" switch for the pre-unlock screens. */
export function LangButton() {
  const app = useApp();
  return (
    <Button variant="ghost" size="sm" onClick={() => setLangPref(app.lang === 'ar' ? 'en' : 'ar')}>
      <span lang={app.lang === 'ar' ? 'en' : 'ar'}>{t('otherLang')}</span>
    </Button>
  );
}

/** Characters remaining hint, e.g. "At least 10 characters (3 more)". */
export function minHint(value: string, min: 10 | 12): string | null {
  const left = min - Array.from(value).length;
  return left > 0 ? t(min === 10 ? 'hint10' : 'hint12', { n: left }) : null;
}
