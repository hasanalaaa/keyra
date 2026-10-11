import { afterEach, describe, expect, it } from 'vitest';
import { ApiError } from '../src/lib/api';
import { recordError, recordErrorText } from '../src/lib/errors';
import { setLang, t } from '../src/lib/i18n';
import { eventText } from '../src/screens/Activity';

afterEach(() => setLang('en'));

// SPEC §5: the settings storage (NVS) erased at boot.
describe('nvs reset', () => {
  it('names both reasons in the activity log', () => {
    setLang('en');
    expect(eventText({ kind: 'nvs_reset', at: 1, detail: 0 })).toMatch(/erased.*full/);
    expect(eventText({ kind: 'nvs_reset', at: 1, detail: 1 })).toMatch(/erased.*newer Keyra/);
    setLang('ar');
    expect(eventText({ kind: 'nvs_reset', at: 1, detail: 1 })).toContain('إصدار أحدث');
  });

  it('the banner says what is safe, what is lost and the delay', () => {
    setLang('ar');
    expect(t('nvsResetBanner')).toBe(
      'اضطر Keyra لمسح تخزين الإعدادات. كلمات السر ومفاتيح المرور سليمة؛ ضاعت الإعدادات والاقترانات والمتصفحات الموثوقة. الفتح متأخر حتى 15 دقيقة.',
    );
    setLang('en');
    expect(t('nvsResetBanner')).toMatch(/passkeys are safe.*trusted browsers are lost.*15 minutes/);
  });
});

// SPEC §17, §18: a tokens or tags record this firmware cannot read.
describe('unreadable records', () => {
  it('says newer version or damaged, and leaves other errors alone', () => {
    expect(recordError(new ApiError(500, 'unsupported_format', ''))).toBe('recordNewer');
    expect(recordError(new ApiError(500, 'corrupt', ''))).toBe('recordCorrupt');
    expect(recordError(new ApiError(507, 'tokens_full', ''))).toBeNull();
    expect(recordError(new Error('x'))).toBeNull();
    setLang('ar');
    expect(recordErrorText(new ApiError(500, 'unsupported_format', ''))).toBe('حُفظت بإصدار أحدث، حدّث Keyra.');
    expect(recordErrorText(new ApiError(500, 'corrupt', ''))).toBe('ملف تالف.');
  });
});
