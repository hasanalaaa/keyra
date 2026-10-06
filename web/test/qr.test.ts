import { describe, expect, it } from 'vitest';
import { PNG } from 'pngjs';
import { migrationUri, qrPng } from '../e2e/fixtures.mjs';
import { base32Encode, parseMigration, parseOtpauth, parseQrText, toOtpauth } from '../src/lib/qrImport';
import { decodeRgba } from '../src/lib/qrScan';
import { normalizeTotp } from '../src/lib/totp';

const KEY = 'JBSWY3DPEHPK3PXP'; // "Hello!\xDE\xAD\xBE\xEF"
const keyBytes = Buffer.from([0x48, 0x65, 0x6c, 0x6c, 0x6f, 0x21, 0xde, 0xad, 0xbe, 0xef]);

describe('base32Encode', () => {
  it('matches the RFC 4648 vectors (no padding)', () => {
    expect(base32Encode(Buffer.from(''))).toBe('');
    expect(base32Encode(Buffer.from('f'))).toBe('MY');
    expect(base32Encode(Buffer.from('fo'))).toBe('MZXQ');
    expect(base32Encode(Buffer.from('foobar'))).toBe('MZXW6YTBOI');
    expect(base32Encode(keyBytes)).toBe(KEY);
  });
});

describe('parseOtpauth', () => {
  it('reads label, issuer and defaults', () => {
    const r = parseOtpauth(`otpauth://totp/Example:alice%40example.com?secret=${KEY}&issuer=Example`);
    expect(r).toEqual({ ok: true, value: { issuer: 'Example', account: 'alice@example.com', secret: KEY, algorithm: 'SHA1', digits: 6, period: 30 } });
  });
  it('takes the issuer from the label prefix when the parameter is absent, and prefers the parameter otherwise', () => {
    expect(parseOtpauth(`otpauth://totp/Acme:bob?secret=${KEY}`)).toMatchObject({ ok: true, value: { issuer: 'Acme', account: 'bob' } });
    expect(parseOtpauth(`otpauth://totp/Old:bob?secret=${KEY}&issuer=New`)).toMatchObject({ ok: true, value: { issuer: 'New', account: 'bob' } });
  });
  it('is case-insensitive on scheme, type, names and algorithm; tolerates spaces, lower case and padding in the key', () => {
    const r = parseOtpauth(`OTPAUTH://TOTP/x?Secret=${KEY.toLowerCase().replace(/(....)/g, '$1 ')}&ALGORITHM=sha256&Digits=8&PERIOD=60`);
    expect(r).toMatchObject({ ok: true, value: { secret: KEY, algorithm: 'SHA256', digits: 8, period: 60 } });
    expect(parseOtpauth(`otpauth://totp/x?secret=MY======`)).toMatchObject({ ok: true, value: { secret: 'MY' } });
  });
  it('accepts a link with no label', () => {
    expect(parseOtpauth(`otpauth://totp?secret=${KEY}`)).toMatchObject({ ok: true, value: { issuer: '', account: '' } });
  });
  it.each([
    ['hotp', `otpauth://hotp/x?secret=${KEY}&counter=1`, 'hotp'],
    ['not otpauth', 'https://example.com', 'notOtp'],
    ['unknown type', `otpauth://steam/x?secret=${KEY}`, 'notOtp'],
    ['no query', 'otpauth://totp/x', 'secret'],
    ['no secret', 'otpauth://totp/x?issuer=a', 'secret'],
    ['bad base32', 'otpauth://totp/x?secret=ABC1', 'secret'],
    ['data after padding', 'otpauth://totp/x?secret=MY==MY', 'secret'],
    ['MD5', `otpauth://totp/x?secret=${KEY}&algorithm=MD5`, 'algorithm'],
    ['7 digits', `otpauth://totp/x?secret=${KEY}&digits=7`, 'digits'],
    ['15 s', `otpauth://totp/x?secret=${KEY}&period=15`, 'period'],
    ['bad escape', `otpauth://totp/x?secret=${KEY}&issuer=%E0%A4%A`, 'badData'],
    ['pair without =', `otpauth://totp/x?secret=${KEY}&oops`, 'badData'],
  ])('rejects %s', (_n, uri, error) => {
    expect(parseOtpauth(uri)).toEqual({ ok: false, error });
  });
  it('round-trips through toOtpauth, adding only non-default parameters', () => {
    const a = { issuer: 'Ex ample', account: 'a:b@c.d', secret: KEY, algorithm: 'SHA1', digits: 6, period: 30 } as const;
    const uri = toOtpauth(a);
    expect(uri).toBe(`otpauth://totp/Ex%20ample:a%3Ab%40c.d?secret=${KEY}&issuer=Ex%20ample`);
    expect(parseOtpauth(uri)).toEqual({ ok: true, value: a });
    const b = { ...a, issuer: '', account: 'b@c.d', algorithm: 'SHA512', digits: 8, period: 60 } as const;
    expect(toOtpauth(b)).toBe(`otpauth://totp/b%40c.d?secret=${KEY}&algorithm=SHA512&digits=8&period=60`);
    expect(parseOtpauth(toOtpauth(b))).toEqual({ ok: true, value: b });
  });
});

describe('normalizeTotp (the Edit field)', () => {
  it('keeps the paste paths and applies the firmware rules to links', () => {
    expect(normalizeTotp('')).toBe('');
    expect(normalizeTotp('jbsw y3dp ehpk 3pxp')).toBe(KEY);
    expect(normalizeTotp('short')).toBeNull();
    const uri = `otpauth://totp/x?secret=${KEY}`;
    expect(normalizeTotp(` ${uri} `)).toBe(uri);
    expect(normalizeTotp(`otpauth://hotp/x?secret=${KEY}`)).toBeNull();
    expect(normalizeTotp(`otpauth://totp/x?secret=${KEY}&digits=7`)).toBeNull();
  });
});

describe('parseMigration', () => {
  const accounts = [
    { secret: keyBytes, name: 'Example:alice@example.com', issuer: 'Example' },
    { secret: Buffer.from('12345678901234567890'), name: 'bob', algorithm: 3, digits: 2 },
    { secret: Buffer.from('counter-based-key'), name: 'Old:hotp', issuer: 'Old', type: 1 },
    { secret: Buffer.from('legacy-md5-key'), name: 'md5', algorithm: 4 },
    { secret: Buffer.alloc(0), name: 'empty' },
    { secret: Buffer.from('unspecified'), name: 'Zed', algorithm: 0, digits: 0 },
  ];

  it('decodes accounts and counts the ones Keyra cannot make codes for', () => {
    const r = parseMigration(migrationUri(accounts, { size: 3, index: 1, id: 4294967295 }));
    expect(r.ok).toBe(true);
    if (!r.ok) return;
    expect(r.value).toMatchObject({ skipped: 3, batchSize: 3, batchIndex: 1, batchId: 4294967295 });
    expect(r.value.accounts).toEqual([
      { issuer: 'Example', account: 'alice@example.com', secret: KEY, algorithm: 'SHA1', digits: 6, period: 30 },
      { issuer: '', account: 'bob', secret: base32Encode(Buffer.from('12345678901234567890')), algorithm: 'SHA512', digits: 8, period: 30 },
      { issuer: '', account: 'Zed', secret: base32Encode(Buffer.from('unspecified')), algorithm: 'SHA1', digits: 6, period: 30 },
    ]);
  });
  it('handles non-ASCII names and a data parameter with raw "+" and "/" characters', () => {
    const uri = migrationUri([{ secret: keyBytes, name: 'زين:حسن', issuer: 'زين' }]);
    const r = parseMigration(decodeURIComponent(uri.replace('%3D', '=')));
    expect(r).toMatchObject({ ok: true, value: { accounts: [{ issuer: 'زين', account: 'حسن' }] } });
  });
  it('rejects damaged data instead of guessing', () => {
    const good = migrationUri(accounts);
    const data = decodeURIComponent(good.split('data=')[1]);
    const cut = (n: number) => `otpauth-migration://offline?data=${encodeURIComponent(Buffer.from(data, 'base64').subarray(0, n).toString('base64'))}`;
    expect(parseMigration(cut(20))).toEqual({ ok: false, error: 'badData' });
    expect(parseMigration('otpauth-migration://offline')).toEqual({ ok: false, error: 'badData' });
    expect(parseMigration('otpauth-migration://offline?data=%%%')).toEqual({ ok: false, error: 'badData' });
    expect(parseMigration('otpauth-migration://offline?data=!!!!')).toEqual({ ok: false, error: 'badData' });
    expect(parseMigration('otpauth-migration://offline?data=')).toEqual({ ok: false, error: 'badData' });
  });
  it('classifies QR text', () => {
    expect(parseQrText(migrationUri([accounts[0]]))).toMatchObject({ ok: true, value: { kind: 'migration' } });
    expect(parseQrText(`otpauth://totp/x?secret=${KEY}`)).toMatchObject({ ok: true, value: { kind: 'totp' } });
    expect(parseQrText('hello')).toEqual({ ok: false, error: 'notOtp' });
  });
});

describe('decoding a QR picture', () => {
  const decodePng = (png: Buffer) => {
    const img = PNG.sync.read(png);
    return decodeRgba(new Uint8ClampedArray(img.data), img.width, img.height);
  };
  it('reads a plain otpauth QR', async () => {
    const uri = `otpauth://totp/Example:alice?secret=${KEY}&issuer=Example`;
    expect(await decodePng(await qrPng(uri))).toBe(uri);
  });
  it('reads a Google Authenticator export QR end to end', async () => {
    const uri = migrationUri([
      { secret: keyBytes, name: 'Example:alice', issuer: 'Example' },
      { secret: Buffer.from('12345678901234567890'), name: 'GitHub:bob', issuer: 'GitHub' },
    ]);
    const text = await decodePng(await qrPng(uri));
    expect(text).toBe(uri);
    expect(parseQrText(text!)).toMatchObject({ ok: true, value: { kind: 'migration', migration: { accounts: [{ issuer: 'Example' }, { issuer: 'GitHub' }] } } });
  });
  it('returns null when the picture has no QR', async () => {
    const blank = new Uint8ClampedArray(200 * 200 * 4).fill(255);
    expect(await decodeRgba(blank, 200, 200)).toBeNull();
  });
});
