// Auto-type sequence editor (SPEC §10.4): the field, token buttons that insert at the caret,
// live validation with the firmware's grammar and a masked preview, one line per button press.
import { useId, useMemo, useRef } from 'preact/hooks';
import { Notice } from './ui';
import { t, type Key } from '../lib/i18n';
import { parseSequence, previewParts, sequenceNeeds, type SeqError } from '../lib/sequence';

const TOKENS: { token: string; label: Key }[] = [
  { token: '{USERNAME}', label: 'seqTokUser' },
  { token: '{PASSWORD}', label: 'seqTokPass' },
  { token: '{TOTP}', label: 'seqTokCode' },
  { token: '{TAB}', label: 'seqTokTab' },
  { token: '{ENTER}', label: 'seqTokEnter' },
  { token: '{SPACE}', label: 'seqTokSpace' },
  { token: '{DELAY 500}', label: 'seqTokDelay' },
  { token: '{PRESS}', label: 'seqTokPress' },
];

const ERRORS: Record<SeqError, Key> = {
  empty: 'seqErrEmpty',
  tooLong: 'seqErrTooLong',
  badText: 'seqErrBadText',
  unclosed: 'seqErrUnclosed',
  stray: 'seqErrStray',
  unknown: 'seqErrUnknown',
  delay: 'seqErrDelay',
  tooManySteps: 'seqErrSteps',
  tooManyPresses: 'seqErrPresses',
  pressPlacement: 'seqErrPress',
  tooMuchDelay: 'seqErrDelayTotal',
  tooMuchTyping: 'seqErrTyping',
};

/** The error to show for `value` ('' = no sequence, which is fine), or null. */
export function sequenceError(value: string): string | null {
  if (value === '') return null;
  const r = parseSequence(value);
  return r.ok ? null : t(ERRORS[r.error]);
}

/** A masked preview (firmware format) as one line per button press. */
export function SequencePreview({ preview, current }: { preview: string; current?: number }) {
  const parts = previewParts(preview);
  return (
    <ol class="seq-parts" dir="ltr">
      {parts.map((p, i) => (
        <li key={i} class={`seq-part${current === i + 1 ? ' current' : ''}${current !== undefined && i + 1 < current ? ' done' : ''}`}>
          {parts.length > 1 && <span class="seq-part-n" dir="auto">{t('seqPart', { i: i + 1 })}</span>}
          <code class="seq-code">{p}</code>
        </li>
      ))}
    </ol>
  );
}

export function SequenceEditor({
  value,
  onValue,
  label,
  have,
}: {
  value: string;
  onValue: (v: string) => void;
  label: string;
  /** The entry's fields, to warn when the sequence types one that is empty. */
  have?: { username: boolean; password: boolean; totp: boolean };
}) {
  const id = useId();
  const input = useRef<HTMLInputElement>(null);
  const parsed = useMemo(() => (value ? parseSequence(value) : null), [value]);
  const error = parsed && !parsed.ok ? t(ERRORS[parsed.error]) : null;
  const missing: string[] = [];
  if (parsed?.ok && have) {
    const need = sequenceNeeds(parsed.steps);
    if (need.username && !have.username) missing.push(t('seqTokUser'));
    if (need.password && !have.password) missing.push(t('seqTokPass'));
    if (need.totp && !have.totp) missing.push(t('seqTokCode'));
  }

  const insert = (token: string) => {
    const el = input.current;
    const start = el?.selectionStart ?? value.length;
    const end = el?.selectionEnd ?? value.length;
    onValue(value.slice(0, start) + token + value.slice(end));
    requestAnimationFrame(() => {
      if (!el) return;
      el.focus();
      el.setSelectionRange(start + token.length, start + token.length);
    });
  };

  return (
    <div class={`field seq-editor${error ? ' has-error' : ''}`}>
      <label class="field-label" for={id}>
        {label}
      </label>
      <div class="field-box">
        <input
          id={id}
          ref={input}
          type="text"
          class="input ltr mono-input"
          dir="ltr"
          value={value}
          placeholder="{USERNAME}{TAB}{PASSWORD}{ENTER}"
          onInput={(e) => onValue(e.currentTarget.value)}
          autocomplete="off"
          autocapitalize="off"
          autocorrect="off"
          spellcheck={false}
          aria-invalid={error ? true : undefined}
          aria-describedby={`${id}-h`}
        />
      </div>
      <div class="seq-tokens" role="group" aria-label={t('seqHelp')}>
        {TOKENS.map((k) => (
          <button key={k.token} type="button" class="chip chip-neutral seq-token" title={k.token} aria-label={t('seqInsert', { token: k.token })} onClick={() => insert(k.token)}>
            {t(k.label)}
          </button>
        ))}
      </div>
      <div class="field-help" id={`${id}-h`}>
        {error ? (
          <p class="field-error" role="alert">
            {error}
          </p>
        ) : (
          <span>{t('seqHelp')}</span>
        )}
      </div>
      {missing.length > 0 && <Notice tone="warn">{t('seqNeeds', { field: missing.join(' · ') })}</Notice>}
      {parsed?.ok && (
        <div class="seq-preview">
          <span class="caption">
            {t('seqPreview')} · {t('seqPreviewNote')}
          </span>
          <SequencePreview preview={parsed.preview} />
        </div>
      )}
    </div>
  );
}
