// The pairing listener (SPEC §9.4 step 4). Injected with scripting.executeScript only into the
// Keyra tab the extension opened, so it must stay self-contained: no imports, no outer names.
export function pairListener(nonce: string, origin: string): void {
  const w = window as unknown as { __keyraPair?: string };
  if (location.origin !== origin || w.__keyraPair === nonce) return;
  w.__keyraPair = nonce;
  const api = (globalThis as unknown as { browser?: typeof chrome }).browser ?? chrome;
  window.addEventListener('message', (e: MessageEvent) => {
    // Only Keyra's own page, posting to itself, with the nonce this extension made.
    if (e.source !== window || e.origin !== origin) return;
    const d = e.data as { type?: unknown; n?: unknown; token?: unknown } | null;
    if (!d || d.type !== 'keyra:token' || d.n !== nonce || typeof d.token !== 'string') return;
    void api.runtime.sendMessage({ t: 'paired', n: nonce, token: d.token });
  });
}
