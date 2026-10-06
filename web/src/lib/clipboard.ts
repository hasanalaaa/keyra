// Copy that works on http://keyra.local (not a secure context, so navigator.clipboard is absent).
// Must be called synchronously inside the user gesture for the execCommand fallback to work.

export function copyText(text: string): boolean {
  if (window.isSecureContext && navigator.clipboard) {
    navigator.clipboard.writeText(text).catch(() => legacyCopy(text));
    return true;
  }
  return legacyCopy(text);
}

function legacyCopy(text: string): boolean {
  const ta = document.createElement('textarea');
  ta.value = text;
  ta.setAttribute('readonly', '');
  // Off-screen but selectable; 16px avoids iOS zoom-on-focus.
  ta.style.cssText = 'position:fixed;inset-block-start:0;inset-inline-start:-9999px;opacity:0;font-size:16px';
  document.body.appendChild(ta);
  const active = document.activeElement as HTMLElement | null;
  ta.select();
  ta.setSelectionRange(0, text.length);
  let ok = false;
  try {
    ok = document.execCommand('copy');
  } catch {
    ok = false;
  }
  ta.remove();
  active?.focus({ preventScroll: true });
  return ok;
}
