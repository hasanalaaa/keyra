// "Scan QR from a photo": a file picker that opens the camera app (or the photo library) and reports the QR text.
import { useRef, useState } from 'preact/hooks';
import { t, type Key } from '../lib/i18n';
import type { OtpError } from '../lib/qrImport';
import { scanPhoto } from '../lib/qrScan';
import { Icon } from './Icon';
import { Spinner } from './ui';

export const QR_ERRORS: Record<OtpError, Key> = {
  notOtp: 'qrNotOtp',
  hotp: 'qrHotp',
  algorithm: 'qrAlgorithm',
  digits: 'qrDigits',
  period: 'qrPeriod',
  secret: 'qrSecret',
  badData: 'qrBad',
};

export function QrPhoto({ label, variant = 'ghost', onText, onNone }: { label: string; variant?: 'ghost' | 'primary'; onText: (text: string) => void; onNone: () => void }) {
  const input = useRef<HTMLInputElement>(null);
  const [busy, setBusy] = useState(false);

  const pick = async (f: File | undefined) => {
    if (!f) return;
    setBusy(true);
    try {
      const r = await scanPhoto(f);
      if (r.ok) onText(r.text);
      else onNone();
    } finally {
      setBusy(false);
      if (input.current) input.current.value = ''; // so the same picture can be chosen again
    }
  };

  return (
    <label class={`btn btn-${variant} ${variant === 'primary' ? 'btn-lg btn-full' : 'btn-sm'} file-btn`} aria-busy={busy || undefined}>
      {busy ? <Spinner /> : <Icon name="qr-code" size={variant === 'primary' ? 22 : 20} />}
      {busy ? t('qrReading') : label}
      <input ref={input} type="file" accept="image/*" capture="environment" class="sr-only" disabled={busy} onChange={(e) => void pick(e.currentTarget.files?.[0])} />
    </label>
  );
}
