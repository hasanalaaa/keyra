// Dev-only builders for QR test material: a Google Authenticator export payload (protobuf written by hand, independently of
// the app's decoder) and QR PNGs. Used by test/qr*.test.ts and e2e/run.mjs.
import QRCode from 'qrcode';

const varint = (n) => {
  const out = [];
  while (n > 127) {
    out.push((n % 128) | 128);
    n = Math.floor(n / 128);
  }
  out.push(n);
  return Buffer.from(out);
};
const tag = (field, wire) => varint(field * 8 + wire);
const bytes = (field, buf) => Buffer.concat([tag(field, 2), varint(buf.length), buf]);
const num = (field, n) => Buffer.concat([tag(field, 0), varint(n)]);

/**
 * @param {{ secret: Buffer, name: string, issuer?: string, algorithm?: number, digits?: number, type?: number }[]} accounts
 * enums as in Google's migration.proto: algorithm 1 SHA1·2 SHA256·3 SHA512·4 MD5, digits 1 six·2 eight, type 1 HOTP·2 TOTP
 * @param {{ size: number, index: number, id: number }} [batch]
 */
export function migrationUri(accounts, batch = { size: 1, index: 0, id: 1234567 }) {
  const parts = accounts.map((a) =>
    bytes(
      1,
      Buffer.concat([
        bytes(1, a.secret),
        bytes(2, Buffer.from(a.name)),
        ...(a.issuer ? [bytes(3, Buffer.from(a.issuer))] : []),
        num(4, a.algorithm ?? 1),
        num(5, a.digits ?? 1),
        num(6, a.type ?? 2),
      ]),
    ),
  );
  const payload = Buffer.concat([...parts, num(2, 1), num(3, batch.size), num(4, batch.index), num(5, batch.id)]);
  return `otpauth-migration://offline?data=${encodeURIComponent(payload.toString('base64'))}`;
}

/** A QR as PNG bytes (large modules and a quiet zone, like a screenshot). */
export const qrPng = (text) => QRCode.toBuffer(text, { type: 'png', errorCorrectionLevel: 'M', margin: 4, scale: 6 });
