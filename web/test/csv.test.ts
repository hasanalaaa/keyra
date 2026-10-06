import { describe, expect, it } from 'vitest';
import { dupKey, hostOf, parseCsv, parseExport } from '../src/lib/csv';

describe('parseCsv (RFC 4180)', () => {
  it('splits plain fields and rows', () => {
    expect(parseCsv('a,b,c\n1,2,3')).toEqual([
      ['a', 'b', 'c'],
      ['1', '2', '3'],
    ]);
  });

  it('handles quoted fields with commas, escaped quotes and empty values', () => {
    expect(parseCsv('"a,b","say ""hi""",,""\n')).toEqual([['a,b', 'say "hi"', '', '']]);
  });

  it('keeps line breaks inside quotes (LF, CRLF and CR)', () => {
    expect(parseCsv('"line 1\nline 2","x\r\ny","p\rq"\r\nnext,row')).toEqual([
      ['line 1\nline 2', 'x\r\ny', 'p\rq'],
      ['next', 'row'],
    ]);
  });

  it('accepts CRLF, LF and lone CR row endings', () => {
    expect(parseCsv('a,b\r\nc,d\re,f\ng,h')).toEqual([
      ['a', 'b'],
      ['c', 'd'],
      ['e', 'f'],
      ['g', 'h'],
    ]);
  });

  it('strips a leading UTF-8 BOM', () => {
    expect(parseCsv('﻿name,url\nx,y')[0]).toEqual(['name', 'url']);
  });

  it('does not invent a trailing empty row, but keeps a final row without newline', () => {
    expect(parseCsv('a\nb\n')).toEqual([['a'], ['b']]);
    expect(parseCsv('a\nb')).toEqual([['a'], ['b']]);
    expect(parseCsv('')).toEqual([]);
  });

  it('keeps empty trailing fields', () => {
    expect(parseCsv('a,,\n')).toEqual([['a', '', '']]);
  });

  it('treats a quote inside an unquoted field literally', () => {
    expect(parseCsv('ab"c,d')).toEqual([['ab"c', 'd']]);
  });
});

describe('parseExport', () => {
  it('reads an Apple Passwords export (Title, URL, Username, Password, Notes, OTPAuth)', () => {
    const csv =
      'Title,URL,Username,Password,Notes,OTPAuth\r\n' +
      'GitHub (hasan),https://github.com/,hasan,"p,a""ss",,otpauth://totp/GitHub:hasan?secret=JBSWY3DPEHPK3PXP\r\n' +
      ',https://www.example.com/login,me@example.com,pw,"two\nlines",\r\n';
    const r = parseExport(csv)!;
    expect(r.source).toBe('apple');
    expect(r.entries).toHaveLength(2);
    expect(r.entries[0]).toEqual({
      title: 'GitHub (hasan)',
      url: 'https://github.com/',
      username: 'hasan',
      password: 'p,a"ss',
      totp: 'otpauth://totp/GitHub:hasan?secret=JBSWY3DPEHPK3PXP',
      notes: '',
      favorite: false,
    });
    expect(r.entries[1].title).toBe('example.com'); // falls back to the URL host
    expect(r.entries[1].notes).toBe('two\nlines');
  });

  it('reads a Chrome export (name, url, username, password, note) with a BOM', () => {
    const csv = '﻿name,url,username,password,note\nبنك الرافدين,https://rafidain-bank.gov.iq/,0771,  spaced pw  ,ملاحظة\n';
    const r = parseExport(csv)!;
    expect(r.source).toBe('chrome');
    expect(r.entries[0]).toMatchObject({ title: 'بنك الرافدين', username: '0771', password: '  spaced pw  ', notes: 'ملاحظة', totp: '' });
  });

  it('reads a Bitwarden export and skips non-login items', () => {
    const csv = [
      'folder,favorite,type,name,notes,fields,reprompt,login_uri,login_username,login_password,login_totp',
      'Work,1,login,Slack,,,0,https://keyra.slack.com,hasan@keyra.dev,s3cret,JBSWY3DPEHPK3PXP',
      ',,note,Wi-Fi codes,"secret note",,0,,,,',
      ',,card,Visa,,,0,,,,',
      ',,login,Steam,,,0,https://store.steampowered.com,gamer,pw2,',
    ].join('\n');
    const r = parseExport(csv)!;
    expect(r.source).toBe('bitwarden');
    expect(r.entries.map((e) => e.title)).toEqual(['Slack', 'Steam']);
    expect(r.entries[0]).toMatchObject({ favorite: true, totp: 'JBSWY3DPEHPK3PXP', url: 'https://keyra.slack.com' });
    expect(r.entries[1].favorite).toBe(false);
  });

  it('reads a 1Password export (Title, Url, Username, Password, OTPAuth, Favorite, Archived, Tags, Notes)', () => {
    const csv =
      'Title,Url,Username,Password,OTPAuth,Favorite,Archived,Tags,Notes\n' +
      'Netflix,netflix.com,family@hasan.iq,Movie-Night,,true,false,streaming,\n' +
      'Router,192.168.1.1,admin,pw,,false,false,,"TP-Link"\n';
    const r = parseExport(csv)!;
    expect(r.source).toBe('1password');
    expect(r.entries).toHaveLength(2);
    expect(r.entries[0]).toMatchObject({ title: 'Netflix', favorite: true });
    expect(r.entries[1]).toMatchObject({ favorite: false, notes: 'TP-Link' });
  });

  it('drops all-empty rows and rows with no title, username or password', () => {
    const csv = 'name,url,username,password\n,,,\n\n,https://x.com,,\nOK,,u,\n';
    const r = parseExport(csv)!;
    expect(r.entries.map((e) => e.title)).toEqual(['x.com', 'OK']);
  });

  it('rejects files that are not password exports', () => {
    expect(parseExport('date,amount\n2026-01-01,5')).toBeNull();
    expect(parseExport('')).toBeNull();
  });
});

describe('helpers', () => {
  it('hostOf strips scheme and www, tolerates bare hosts and junk', () => {
    expect(hostOf('https://www.google.com/a?b')).toBe('google.com');
    expect(hostOf('github.com/login')).toBe('github.com');
    expect(hostOf('')).toBe('');
    expect(hostOf('not a url')).toBe('not a url');
  });

  it('dupKey matches the device rule (title + username + url)', () => {
    expect(dupKey({ title: 'a', username: 'b', url: 'c' })).toBe(dupKey({ title: 'a', username: 'b', url: 'c' }));
    expect(dupKey({ title: 'a', username: 'b', url: 'c' })).not.toBe(dupKey({ title: 'a', username: 'bc', url: '' }));
  });
});
