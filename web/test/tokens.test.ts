import { describe, expect, it } from 'vitest';
import { eventText } from '../src/screens/Activity';
import { nameOk } from '../src/screens/Tokens';
import { setLang } from '../src/lib/i18n';
import { parseRoute } from '../src/lib/router';

// SPEC §17: access tokens in the activity log and the create form.
describe('access tokens', () => {
  const names = (id: number) => (id === 5 ? 'GitHub' : undefined);

  it('logs what a token did, naming the account by id', () => {
    setLang('en');
    expect(eventText({ kind: 'token_created', at: 1, detail: 0, title: 'Claude' })).toBe('Access token “Claude” created');
    expect(eventText({ kind: 'token_revoked', at: 1, detail: 0, title: 'Claude' })).toBe('Access token “Claude” revoked');
    expect(eventText({ kind: 'agent_armed', at: 1, detail: 1, id: 5, title: 'Claude' }, names)).toBe('Claude asked to type GitHub');
    // A deleted account is named by its id rather than dropped.
    expect(eventText({ kind: 'agent_armed', at: 1, detail: 1, id: 9, title: 'Claude' }, names)).toBe('Claude asked to type #9');
    expect(eventText({ kind: 'agent_saved', at: 1, detail: 0, id: 5, title: 'Phone' }, names)).toBe('Phone saved GitHub');
    // Repeated lists are one line with a count.
    expect(eventText({ kind: 'agent_listed', at: 1, detail: 0, n: 1, title: 'Claude' })).toBe('Claude looked at your account names');
    expect(eventText({ kind: 'agent_listed', at: 1, detail: 0, n: 4, title: 'Claude' })).toBe('Claude looked at your account names ×4');
    expect(eventText({ kind: 'agent_generated', at: 1, detail: 0, n: 2, title: 'Phone' })).toBe('Phone generated a password ×2');
  });

  it('speaks Arabic', () => {
    setLang('ar');
    expect(eventText({ kind: 'agent_armed', at: 1, detail: 1, id: 5, title: 'Claude' }, names)).toBe('طلب Claude كتابة GitHub');
    expect(eventText({ kind: 'token_created', at: 1, detail: 0, title: 'وكيل' })).toBe('أُنشئ رمز الوصول «وكيل»');
    setLang('en');
  });

  it('accepts names Keyra can store (1-48 bytes)', () => {
    expect(nameOk('Claude on my laptop')).toBe(true);
    expect(nameOk('   ')).toBe(false);
    expect(nameOk('a'.repeat(48))).toBe(true);
    expect(nameOk('a'.repeat(49))).toBe(false);
    expect(nameOk('و'.repeat(24))).toBe(true); // 2 bytes each
    expect(nameOk('و'.repeat(25))).toBe(false);
  });

  it('names the other site when an extension typed there (SPEC §9.4: detail + 4)', () => {
    setLang('en');
    expect(eventText({ kind: 'agent_armed', at: 1, detail: 5, id: 5, title: 'Chrome on Mac → evil.example' }, names)).toBe('Chrome on Mac asked to type GitHub on another site: evil.example');
    expect(eventText({ kind: 'agent_armed', at: 1, detail: 1, id: 5, title: 'A → B' }, names)).toBe('A → B asked to type GitHub');
  });

  it('opens the pairing screen from the extension’s link', () => {
    expect(parseRoute('#/connect?ext=Chrome%20on%20Mac&n=AAAAAAAAAAAAAAAAAAAAAA')).toEqual({ name: 'connect', ext: 'Chrome on Mac', n: 'AAAAAAAAAAAAAAAAAAAAAA' });
    expect(parseRoute('#/connect')).toEqual({ name: 'connect', ext: '', n: '' });
    expect(parseRoute('#/a/5?x=1')).toEqual({ name: 'account', id: 5 });
  });
});
