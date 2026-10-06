// Reads a QR code out of a photo, entirely in the browser (the page is plain HTTP, so there is no live camera).
// The picture is drawn to a scratch canvas, decoded, and wiped; it is never uploaded or stored.

export type ScanResult = { ok: true; text: string } | { ok: false };

/** Long sides to try: phone photos are 12 MP, which is slow and no easier to decode than ~1600 px. */
const SIDES = [1600, 2800, 800];

/** Decodes a QR from raw RGBA pixels. jsQR is loaded on first use. */
export async function decodeRgba(data: Uint8ClampedArray, width: number, height: number): Promise<string | null> {
  const mod = await import('jsqr');
  const jsQR = (mod.default as unknown as { default?: typeof mod.default }).default ?? mod.default;
  return jsQR(data, width, height)?.data ?? null;
}

const frame = () => new Promise<void>((r) => setTimeout(r, 30)); // let the "reading" state paint between heavy steps

export async function scanPhoto(file: File): Promise<ScanResult> {
  let bitmap: ImageBitmap;
  try {
    bitmap = await createImageBitmap(file); // applies the EXIF orientation
  } catch {
    return { ok: false };
  }
  const canvas = document.createElement('canvas');
  try {
    const ctx = canvas.getContext('2d', { willReadFrequently: true });
    if (!ctx) return { ok: false };
    const long = Math.max(bitmap.width, bitmap.height);
    const tried = new Set<number>();
    for (const side of SIDES) {
      const scale = Math.min(1, side / long);
      const w = Math.max(1, Math.round(bitmap.width * scale));
      if (tried.has(w)) continue;
      tried.add(w);
      canvas.width = w;
      canvas.height = Math.max(1, Math.round(bitmap.height * scale));
      ctx.drawImage(bitmap, 0, 0, canvas.width, canvas.height);
      await frame();
      const img = ctx.getImageData(0, 0, canvas.width, canvas.height);
      const text = await decodeRgba(img.data, img.width, img.height);
      if (text) return { ok: true, text };
    }
    return { ok: false };
  } finally {
    bitmap.close();
    canvas.width = canvas.height = 0; // releases the pixel buffer
  }
}
