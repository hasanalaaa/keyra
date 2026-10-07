// Splits an import into request bodies the device accepts (SPEC §5: 64 KiB per
// body). Counting entries alone let long notes push a batch past the limit,
// and the device then closes the connection mid-upload.
const enc = new TextEncoder();

export const MAX_BATCH = 50;
export const MAX_BATCH_BYTES = 48 * 1024; // headroom under the device's 64 KiB

export function batches<T>(items: readonly T[], maxCount = MAX_BATCH, maxBytes = MAX_BATCH_BYTES): T[][] {
  const out: T[][] = [];
  let cur: T[] = [];
  let bytes = 0;
  for (const item of items) {
    const size = enc.encode(JSON.stringify(item)).length + 1; // + the comma
    if (cur.length > 0 && (cur.length >= maxCount || bytes + size > maxBytes)) {
      out.push(cur);
      cur = [];
      bytes = 0;
    }
    cur.push(item);
    bytes += size;
  }
  if (cur.length > 0) out.push(cur);
  return out;
}
