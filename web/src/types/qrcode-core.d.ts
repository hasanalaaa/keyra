// The encoder core of `qrcode` (its typings cover only the package root).
declare module 'qrcode/lib/core/qrcode' {
  import type { QRCode, QRCodeOptions } from 'qrcode';
  export function create(text: string, options?: QRCodeOptions): QRCode;
}
