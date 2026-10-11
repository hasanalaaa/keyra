// Registers the Readex Pro subset (DESIGN §3) as its own hashed file, served by Keyra with a long cache
// (and preloaded by index.html, so it downloads alongside the app). `load()` starts it at once rather than
// when text first needs it, so screens never flash the fallback font.
import fontUrl from '../fonts/keyra-sans.woff2?url';

export function loadFont(): void {
  if (typeof FontFace === 'undefined') return; // system fallback stack still renders everything
  const face = new FontFace('Keyra Sans', `url(${fontUrl}) format('woff2')`, { weight: '400 700', style: 'normal', display: 'swap' });
  document.fonts.add(face);
  face.load().catch(() => {
    // A broken font must never blank the UI; the fallback stack takes over.
  });
}
