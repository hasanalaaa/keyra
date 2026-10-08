// Auto-type sequences (SPEC §10.4): the firmware grammar (keyra_vault sequence.hpp/.cpp)
// mirrored step for step, so the editor says what is wrong before the save round trip.
//
//   sequence := ( literal | token )+
//   token    := {USERNAME} {PASSWORD} {TOTP} {TAB} {ENTER} {SPACE} {DELAY n} (100..3000) {PRESS} {{} {}}
//   literal  := printable characters other than { and }

export type SeqKind = 'text' | 'username' | 'password' | 'totp' | 'tab' | 'enter' | 'space' | 'delay' | 'press';

export interface SeqStep {
  kind: SeqKind;
  text?: string; // kind 'text'
  ms?: number; // kind 'delay'
}

export type SeqError =
  | 'empty'
  | 'tooLong'
  | 'badText'
  | 'unclosed'
  | 'stray'
  | 'unknown'
  | 'delay'
  | 'tooManySteps'
  | 'tooManyPresses'
  | 'pressPlacement'
  | 'tooMuchDelay'
  | 'tooMuchTyping';

export type SeqResult = { ok: true; steps: SeqStep[]; parts: number; preview: string } | { ok: false; error: SeqError };

export const SEQ_MAX_SOURCE = 256; // UTF-8 bytes
export const SEQ_MAX_STEPS = 32;
export const SEQ_MAX_PRESSES = 4;
export const SEQ_MAX_TYPED = 1024;
export const SEQ_MIN_DELAY = 100;
export const SEQ_MAX_DELAY = 3000;
export const SEQ_MAX_TOTAL_DELAY = 10000;

// Most characters each token can type (vault kMaxUsername / kMaxPassword = 256, a TOTP code ≤ 10).
const TOKENS: Record<string, { kind: SeqKind; typed: number }> = {
  USERNAME: { kind: 'username', typed: 256 },
  PASSWORD: { kind: 'password', typed: 256 },
  TOTP: { kind: 'totp', typed: 10 },
  TAB: { kind: 'tab', typed: 1 },
  ENTER: { kind: 'enter', typed: 1 },
  SPACE: { kind: 'space', typed: 1 },
  PRESS: { kind: 'press', typed: 0 },
};

const enc = new TextEncoder();

/** Control characters (C0, DEL) and lone surrogates (not valid UTF-8) are refused, like the firmware. */
function printable(s: string): boolean {
  for (let i = 0; i < s.length; i++) {
    const c = s.charCodeAt(i);
    if (c < 0x20 || c === 0x7f) return false;
    if (c >= 0xd800 && c <= 0xdbff) {
      const n = s.charCodeAt(i + 1);
      if (!(n >= 0xdc00 && n <= 0xdfff)) return false;
      i++;
    } else if (c >= 0xdc00 && c <= 0xdfff) return false;
  }
  return true;
}

const chars = (s: string): number => Array.from(s).length; // code points

export function parseSequence(src: string): SeqResult {
  if (src === '') return { ok: false, error: 'empty' };
  if (enc.encode(src).length > SEQ_MAX_SOURCE) return { ok: false, error: 'tooLong' };
  if (!printable(src)) return { ok: false, error: 'badText' };
  const steps: SeqStep[] = [];
  const addText = (t: string) => {
    const last = steps[steps.length - 1];
    if (last?.kind === 'text') last.text += t;
    else steps.push({ kind: 'text', text: t });
  };
  let presses = 0;
  let typed = 0;
  let delay = 0;
  for (let i = 0; i < src.length; ) {
    const c = src[i];
    if (c === '}') return { ok: false, error: 'stray' };
    if (c !== '{') {
      let stop = i;
      while (stop < src.length && src[stop] !== '{' && src[stop] !== '}') stop++;
      const lit = src.slice(i, stop);
      addText(lit);
      typed += chars(lit);
      i = stop;
      continue;
    }
    const three = src.slice(i, i + 3);
    if (three === '{{}' || three === '{}}') {
      addText(three[1]);
      typed++;
      i += 3;
      continue;
    }
    const close = src.indexOf('}', i + 1);
    if (close < 0) return { ok: false, error: 'unclosed' };
    const body = src.slice(i + 1, close);
    i = close + 1;
    if (body.startsWith('DELAY ')) {
      const num = body.slice(6);
      if (!/^[1-9][0-9]{0,3}$/.test(num)) return { ok: false, error: 'delay' };
      const ms = Number(num);
      if (ms < SEQ_MIN_DELAY || ms > SEQ_MAX_DELAY) return { ok: false, error: 'delay' };
      delay += ms;
      steps.push({ kind: 'delay', ms });
      continue;
    }
    const tok = Object.prototype.hasOwnProperty.call(TOKENS, body) ? TOKENS[body] : undefined;
    if (!tok) return { ok: false, error: 'unknown' }; // includes anything chord-like: {CTRL}, {ALT+F4}, {WIN}
    if (tok.kind === 'press') presses++;
    typed += tok.typed;
    steps.push({ kind: tok.kind });
  }
  if (steps.length > SEQ_MAX_STEPS) return { ok: false, error: 'tooManySteps' };
  if (presses > SEQ_MAX_PRESSES) return { ok: false, error: 'tooManyPresses' };
  if (delay > SEQ_MAX_TOTAL_DELAY) return { ok: false, error: 'tooMuchDelay' };
  if (typed > SEQ_MAX_TYPED) return { ok: false, error: 'tooMuchTyping' };
  // Every part between {PRESS} tokens (and the whole) must type something.
  let typedInPart = false;
  for (const s of steps) {
    if (s.kind === 'press') {
      if (!typedInPart) return { ok: false, error: 'pressPlacement' };
      typedInPart = false;
    } else if (s.kind !== 'delay') typedInPart = true;
  }
  if (!typedInPart) return { ok: false, error: presses > 0 ? 'pressPlacement' : 'empty' };
  return { ok: true, steps, parts: presses + 1, preview: sequencePreview(steps) };
}

/** What a stored sequence may hold: empty (no custom sequence) or a valid one. */
export const sequenceValid = (src: string): boolean => src === '' || parseSequence(src).ok;

/** Like seq::preview: the tokens as written, literal text masked as one • per character. */
export function sequencePreview(steps: SeqStep[]): string {
  return steps
    .map((s) => (s.kind === 'text' ? '•'.repeat(chars(s.text ?? '')) : s.kind === 'delay' ? `{DELAY ${s.ms}}` : `{${s.kind.toUpperCase()}}`))
    .join('');
}

/** A masked preview split at each {PRESS}: one string per button press. */
export const previewParts = (preview: string): string[] => preview.split('{PRESS}');

/** The entry fields a sequence types (firmware refuses to arm it when one is empty). */
export function sequenceNeeds(steps: SeqStep[]): { username: boolean; password: boolean; totp: boolean } {
  return {
    username: steps.some((s) => s.kind === 'username'),
    password: steps.some((s) => s.kind === 'password'),
    totp: steps.some((s) => s.kind === 'totp'),
  };
}

/** What "Both" types without a custom order (seqrun::builtIn). */
export const builtInBoth = (enterBetween: boolean, submit: boolean): string =>
  `{USERNAME}${enterBetween ? '{ENTER}' : '{TAB}'}{PASSWORD}${submit ? '{ENTER}' : ''}`;
