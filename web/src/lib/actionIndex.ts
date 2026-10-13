// Search finds actions and settings too (typing "backup" offers the Backup screen). Labels match in
// both languages whatever the UI language; `words` adds the synonyms people type. Unit-tested in
// test/actionIndex.test.ts.
import type { IconName } from '../components/Icon';
import { both, type Key } from './i18n';
import { normalize } from './search';

export interface ActionItem {
  key: Key;
  icon: IconName;
  path: string;
  words?: string;
}

export const ACTIONS: ActionItem[] = [
  { key: 'addAccount', icon: 'plus', path: '/new', words: 'new create login جديد' },
  { key: 'generate', icon: 'wand-sparkles', path: '/generate', words: 'random generator strong مولد عشوائية' },
  { key: 'typeTextTitle', icon: 'type', path: '/type', words: 'keyboard text نص' },
  { key: 'typeTest', icon: 'zap', path: '/typing/test', words: 'test check تجربة' },
  { key: 'bothSeqRow', icon: 'list-ordered', path: '/typing/order', words: 'sequence order tab enter تسلسل' },
  { key: 'healthRow', icon: 'pulse', path: '/security/health', words: 'weak reused old audit ضعيفة مكررة' },
  { key: 'activityRow', icon: 'activity', path: '/security/activity', words: 'log history events سجل' },
  { key: 'passkeysRow', icon: 'key-round', path: '/security/passkeys', words: 'fido webauthn security key مفتاح' },
  { key: 'recoveryRow', icon: 'life-buoy', path: '/security/recovery', words: 'forgot recover shares استرداد نسيت' },
  { key: 'autoLock', icon: 'timer', path: '/security/autolock', words: 'timeout idle قفل' },
  { key: 'changePassphrase', icon: 'lock', path: '/security/passphrase', words: 'master password عبارة' },
  { key: 'backupNowRow', icon: 'download', path: '/backup', words: 'backup export save file نسخ النسخ احتياطي' },
  { key: 'restoreRow', icon: 'upload', path: '/restore', words: 'restore backup file استعادة النسخ نسخة' },
  { key: 'importRow', icon: 'import', path: '/import', words: 'csv chrome bitwarden 1password apple استيراد' },
  { key: 'keyraWifi', icon: 'wifi', path: '/device/wifi', words: 'wifi network ssid password شبكة' },
  { key: 'homeWifi', icon: 'house', path: '/device/home', words: 'wifi home network router منزل' },
  { key: 'trustedRow', icon: 'monitor', path: '/device/trusted', words: 'browser trust متصفح' },
  { key: 'blePairNew', icon: 'bluetooth', path: '/device/pair', words: 'bluetooth pair ipad phone بلوتوث' },
  { key: 'layoutUsb', icon: 'keyboard', path: '/device/layout-usb', words: 'keyboard layout azerty qwertz لوحة' },
  { key: 'appsRow', icon: 'bot', path: '/device/apps', words: 'token agent api extension رمز' },
  { key: 'tagsRow', icon: 'nfc', path: '/device/tags', words: 'nfc tag sticker وسم' },
  { key: 'updRow', icon: 'refresh-cw', path: '/device/update', words: 'firmware update version تحديث' },
  { key: 'language', icon: 'languages', path: '/device', words: 'language arabic english لغة' },
  { key: 'theme', icon: 'moon', path: '/device', words: 'dark light theme داكن فاتح' },
  { key: 'eraseRow', icon: 'trash-2', path: '/device/erase', words: 'erase reset factory مسح' },
];

const words = (s: string): string[] => normalize(s).split(/[\s\-–‑…«»"“”()/·.,]+/).filter(Boolean);

/**
 * Actions whose label (either language) or synonym has a word starting with `q`, whole-word matches
 * first, then in list order; at most `max`.
 */
export function searchActions(q: string, max = 4): ActionItem[] {
  const nq = normalize(q.trim());
  if (Array.from(nq).length < 2) return [];
  const ranked: [number, ActionItem][] = [];
  for (const a of ACTIONS) {
    const ws = words(`${both(a.key).join(' ')} ${a.words ?? ''}`);
    if (ws.includes(nq)) ranked.push([0, a]);
    else if (ws.some((w) => w.startsWith(nq))) ranked.push([1, a]);
  }
  return ranked
    .sort((x, y) => x[0] - y[0])
    .slice(0, max)
    .map(([, a]) => a);
}
