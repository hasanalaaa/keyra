// Registers the embedded Readex Pro subset (DESIGN §3) from bytes rather than an @font-face data: URL:
// the device CSP has no font-src, so default-src 'self' would block a data: font. A FontFace built from
// an ArrayBuffer involves no fetch, so CSP does not apply.
import fontUrl from '../fonts/keyra-sans.woff2?inline';

export function loadFont(): void {
  if (typeof FontFace === 'undefined') return; // system fallback stack still renders everything
  const b64 = fontUrl.slice(fontUrl.indexOf(',') + 1);
  const bin = atob(b64);
  const bytes = new Uint8Array(bin.length);
  for (let i = 0; i < bin.length; i++) bytes[i] = bin.charCodeAt(i);
  const face = new FontFace('Keyra Sans', bytes.buffer, { weight: '400 700', style: 'normal', display: 'swap' });
  document.fonts.add(face);
  face.load().catch(() => {
    // A broken font must never blank the UI; the fallback stack takes over.
  });
}
