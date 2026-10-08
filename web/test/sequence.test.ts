// Cases from firmware/components/keyra_vault/host_test/test_sequence.cpp and
// keyra_api/host_test/sequence_test.cpp: the editor must agree with the device.
import { describe, expect, it } from 'vitest';
import { builtInBoth, parseSequence, previewParts, sequenceNeeds, sequenceValid, type SeqError } from '../src/lib/sequence';

const err = (s: string): SeqError | null => {
  const r = parseSequence(s);
  return r.ok ? null : r.error;
};

describe('sequence grammar', () => {
  it('accepts the grammar', () => {
    let r = parseSequence('{USERNAME}{TAB}{PASSWORD}{ENTER}');
    expect(r.ok && r.steps.map((s) => s.kind)).toEqual(['username', 'tab', 'password', 'enter']);
    r = parseSequence('me@corp.com{TAB}{TOTP}{SPACE}x{{}y{}}z');
    expect(r.ok).toBe(true);
    if (r.ok) {
      expect(r.steps.length).toBe(5);
      expect(r.steps[0]).toEqual({ kind: 'text', text: 'me@corp.com' });
      expect(r.steps[2].kind).toBe('totp');
      expect(r.steps[4]).toEqual({ kind: 'text', text: 'x{y}z' }); // escapes merge into the literal
    }
    r = parseSequence('{PASSWORD}{DELAY 100}{DELAY 3000}{ENTER}');
    expect(r.ok && [r.steps[1], r.steps[2]]).toEqual([
      { kind: 'delay', ms: 100 },
      { kind: 'delay', ms: 3000 },
    ]);
    r = parseSequence('{USERNAME}{ENTER}{PRESS}{PASSWORD}{ENTER}');
    expect(r.ok && r.steps[2].kind).toBe('press');
    expect(r.ok && r.parts).toBe(2);
    expect(r.ok && r.preview).toBe('{USERNAME}{ENTER}{PRESS}{PASSWORD}{ENTER}');
    expect(err('^v+a%b~')).toBeNull(); // KeePass modifier characters are plain text here
    expect(err('كلمة')).toBeNull(); // Arabic literal
    r = parseSequence('ab{TAB}ك');
    expect(r.ok && r.preview).toBe('••{TAB}•'); // literals masked per character
  });

  it('rejects chords and unknown tokens', () => {
    for (const s of ['{CTRL}', '{CTRL+V}', '{ALT}', '{ALT+F4}', '{WIN}', '{LWIN}', '{CMD}', '{SHIFT}', '{F1}', '{DELETE}', '{UP}', '{username}', '{USERNAME }', '{ USERNAME}', '{}', '{URL}', '{NOTES}', '{S:pin}', '{VKEY 13}', '{TAB 3}', '{constructor}'])
      expect(err(s), s).toBe('unknown');
    expect(err('{TAB')).toBe('unclosed');
    expect(err('a}b')).toBe('stray');
    expect(err('a\nb')).toBe('badText');
    expect(err('a\tb')).toBe('badText');
    expect(err('a\x7f')).toBe('badText');
    expect(err('a\ud800b')).toBe('badText'); // not valid UTF-8
  });

  it('enforces the limits', () => {
    expect(err('')).toBe('empty');
    expect(err('{DELAY 500}')).toBe('empty');
    expect(err('a'.repeat(256))).toBeNull();
    expect(err('a'.repeat(257))).toBe('tooLong');
    expect(err('ك'.repeat(129))).toBe('tooLong'); // bytes, not characters
    for (const s of ['{DELAY 99}', '{DELAY 3001}', '{DELAY 0100}', '{DELAY }', '{DELAY -5}', '{DELAY 1e3}', '{DELAY 100 }', '{DELAY  100}'])
      expect(err(s), s).toBe('delay');
    expect(err('{TAB}'.repeat(33))).toBe('tooManySteps');
    expect(err('a{PRESS}b{PRESS}c{PRESS}d{PRESS}e')).toBeNull();
    expect(err('a{PRESS}b{PRESS}c{PRESS}d{PRESS}e{PRESS}f')).toBe('tooManyPresses');
    expect(err('{PRESS}{PASSWORD}')).toBe('pressPlacement');
    expect(err('{PASSWORD}{PRESS}')).toBe('pressPlacement');
    expect(err('{PASSWORD}{PRESS}{DELAY 200}{PRESS}{ENTER}')).toBe('pressPlacement');
    expect(err('a{DELAY 3000}{DELAY 3000}{DELAY 3000}{DELAY 1000}')).toBeNull();
    expect(err('a{DELAY 3000}{DELAY 3000}{DELAY 3000}{DELAY 1001}')).toBe('tooMuchDelay');
    // Expansion: fields count at their maximum length (256 each).
    expect(err('{PASSWORD}{PASSWORD}{PASSWORD}{PASSWORD}')).toBeNull();
    expect(err('{PASSWORD}{PASSWORD}{PASSWORD}{PASSWORD}x')).toBe('tooMuchTyping');
    expect(sequenceValid('')).toBe(true);
    expect(sequenceValid('{CTRL}')).toBe(false);
  });
});

describe('sequence helpers', () => {
  it('splits the preview into one part per press', () => {
    const r = parseSequence('{USERNAME}{ENTER}{PRESS}pin{TAB}{PASSWORD}{ENTER}');
    expect(r.ok && previewParts(r.preview)).toEqual(['{USERNAME}{ENTER}', '•••{TAB}{PASSWORD}{ENTER}']);
  });
  it('names the fields a sequence types', () => {
    const r = parseSequence('{USERNAME}{TAB}{TOTP}');
    expect(r.ok && sequenceNeeds(r.steps)).toEqual({ username: true, password: false, totp: true });
  });
  it('builds the built-in Both order like seqrun::builtIn', () => {
    expect(builtInBoth(false, false)).toBe('{USERNAME}{TAB}{PASSWORD}');
    expect(builtInBoth(true, true)).toBe('{USERNAME}{ENTER}{PASSWORD}{ENTER}');
    expect(sequenceValid(builtInBoth(true, true))).toBe(true);
  });
});
